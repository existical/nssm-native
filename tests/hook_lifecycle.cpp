#include "test.h"

namespace hook_test {
struct allocation { void *pointer; SIZE_T bytes; bool freed; };
struct handle {
  bool worker, primary, closed, done;
  DWORD result;
  LPTHREAD_START_ROUTINE routine;
  void *argument;
};
static std::vector<allocation> memory;
static std::vector<handle *> handles;
static bool active, fail_thread, deferred, fail_process, fail_exit_query, fail_worker_exit;
static bool fail_worker_wait, timeout_worker_wait;
static DWORD process_wait, process_exit;
static int fail_after = -1, hook_lookup;
static std::vector<unsigned long> events;
static bool live;
static HANDLE cleanup_gate, cleanup_entered;

static HANDLE make_handle(bool worker, bool primary) {
  handle *h = new handle();
  h->worker = worker;
  h->primary = primary;
  handles.push_back(h);
  return reinterpret_cast<HANDLE>(h);
}
static handle *lookup(HANDLE value) {
  for (size_t i = 0; i < handles.size(); i++) {
    if (reinterpret_cast<HANDLE>(handles[i]) == value) {
      CHECK(! handles[i]->closed);
      return handles[i];
    }
  }
  throw std::runtime_error("Unexpected hook handle");
}
static void readable(const void *pointer) {
  REQUIRE(pointer);
  for (size_t i = 0; i < memory.size(); i++) {
    uintptr_t start = reinterpret_cast<uintptr_t>(memory[i].pointer);
    uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (address >= start && address - start < memory[i].bytes) CHECK(! memory[i].freed);
  }
}
static void *copy(void *to, const void *from, size_t bytes) {
  if (active) readable(from);
  return memmove(to, from, bytes);
}
static LPVOID WINAPI allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (! active) return HeapAlloc(heap, flags, bytes);
  if (! fail_after) return NULL;
  if (fail_after > 0) fail_after--;
  void *p = HeapAlloc(heap, flags, bytes);
  if (p) { allocation a = {p, bytes, false}; memory.push_back(a); }
  return p;
}
static BOOL WINAPI release(HANDLE heap, DWORD flags, LPVOID pointer) {
  if (! active) return HeapFree(heap, flags, pointer);
  for (size_t i = 0; i < memory.size(); i++) {
    if (memory[i].pointer == pointer) {
      CHECK(! memory[i].freed);
      memory[i].freed = true;
      return TRUE; // Quarantine lets the unsafe baseline report duplicate frees.
    }
  }
  CHECK(false);
  return FALSE;
}
static BOOL WINAPI close(HANDLE value) {
  if (! active) return CloseHandle(value);
  lookup(value)->closed = true;
  return TRUE;
}
static DWORD WINAPI wait(HANDLE value, DWORD timeout) {
  if (! active) return WaitForSingleObject(value, timeout);
  handle *h = lookup(value);
  if (! h->worker) return process_wait;
  if (fail_worker_wait) return WAIT_FAILED;
  if (timeout_worker_wait) return WAIT_TIMEOUT;
  if (! h->done && timeout) {
    h->result = h->routine(h->argument);
    h->done = true;
  }
  return h->done ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
static HANDLE WINAPI thread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD id) {
  if (! active) return CreateThread(security, stack, fn, arg, flags, id);
  if (fail_thread) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
  HANDLE value = make_handle(true, false);
  handle *h = lookup(value);
  h->routine = fn;
  h->argument = arg;
  *id = 123;
  if (! deferred) { h->result = fn(arg); h->done = true; }
  return value;
}
static BOOL WINAPI process(LPCTSTR app, LPTSTR cmd, LPSECURITY_ATTRIBUTES ps, LPSECURITY_ATTRIBUTES ts,
  BOOL inherit, DWORD flags, LPVOID env, LPCTSTR dir, LPSTARTUPINFO si, LPPROCESS_INFORMATION pi) {
  if (! active) return CreateProcess(app, cmd, ps, ts, inherit, flags | (live ? CREATE_NO_WINDOW : 0), env, dir, si, pi);
  if (fail_process) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
  pi->hProcess = make_handle(false, false);
  pi->hThread = make_handle(false, true);
  pi->dwProcessId = 123;
  return TRUE;
}
static BOOL WINAPI process_code(HANDLE value, LPDWORD code) {
  if (! active) return GetExitCodeProcess(value, code);
  lookup(value);
  if (fail_exit_query) return FALSE;
  *code = process_exit;
  return TRUE;
}
static BOOL WINAPI thread_code(HANDLE value, LPDWORD code) {
  if (! active) return GetExitCodeThread(value, code);
  handle *h = lookup(value);
  if (fail_worker_exit) return FALSE;
  *code = h->done ? h->result : STILL_ACTIVE;
  return TRUE;
}
static int await(SERVICE_STATUS_HANDLE sh, SERVICE_STATUS *status, HANDLE h, TCHAR *name, TCHAR *function, unsigned long timeout) {
  if (! active) return await_single_handle(sh, status, h, name, function, timeout);
  readable(name);
  REQUIRE(timeout > 0 && timeout != INFINITE);
  DWORD result = wait(h, timeout);
  return result == WAIT_OBJECT_0 ? 0 : result == WAIT_TIMEOUT ? 1 : -1;
}
static void kill(kill_t *k, unsigned long pid) {
  if (live) {
    // The live helper has no descendants. Test ownership without invoking the
    // separately reviewed process-tree walker against other local processes.
    if (cleanup_gate) { SetEvent(cleanup_entered); CHECK(WaitForSingleObject(cleanup_gate, 5000) == WAIT_OBJECT_0); }
    DWORD code;
    REQUIRE(GetExitCodeProcess(k->process_handle, &code));
    if (code == STILL_ACTIVE) {
      CHECK(TerminateProcess(k->process_handle, 0));
      CHECK(WaitForSingleObject(k->process_handle, 5000) == WAIT_OBJECT_0);
    }
    return;
  }
  if (! active) { kill_process_tree(k, pid); return; }
  readable(k->name);
  lookup(k->process_handle);
}
static int creation(HANDLE h, FILETIME *time) {
  if (! active) return get_process_creation_time(h, time);
  lookup(h);
  ZeroMemory(time, sizeof(*time));
  return 0;
}
static void set_env(nssm_service_t *service) { if (! active) set_service_environment(service); }
static void unset_env(nssm_service_t *service) { if (! active) unset_service_environment(service); }
static BOOL WINAPI variable(LPCTSTR name, LPCTSTR value) { return active ? TRUE : SetEnvironmentVariable(name, value); }
static int get(TCHAR *name, TCHAR *event, TCHAR *action, TCHAR *buffer, unsigned long bytes) {
  if (! active) return get_hook(name, event, action, buffer, bytes);
  _tcscpy_s(buffer, bytes / sizeof(TCHAR), hook_lookup == 1 ? _T("") : _T("test-hook"));
  return hook_lookup == 2 ? 1 : 0;
}
static void log(unsigned short type, unsigned long event, ...) { if (active) events.push_back(event); }

struct fixture {
  nssm_service_t *service;
  hook_thread_t list;
  fixture() : service(new nssm_service_t()), list() {
    active = true;
    fail_thread = deferred = fail_process = fail_exit_query = fail_worker_exit = false;
    fail_worker_wait = timeout_worker_wait = false;
    process_wait = WAIT_OBJECT_0;
    process_exit = 0;
    fail_after = -1;
    hook_lookup = 0;
    events.clear();
    _tcscpy_s(service->name, _T("Ownership test"));
    InitializeCriticalSection(&service->hook_section);
  }
  ~fixture() {
    for (size_t i = 0; i < handles.size(); i++) { CHECK(handles[i]->closed); delete handles[i]; }
    for (size_t i = 0; i < memory.size(); i++) {
      CHECK(memory[i].freed);
      HeapFree(GetProcessHeap(), 0, memory[i].pointer);
    }
    handles.clear(); memory.clear(); active = false;
    DeleteCriticalSection(&service->hook_section);
    delete service;
  }
  int run(bool async = true, unsigned long deadline = 20) {
    return nssm_hook(&list, service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, NULL, deadline, async);
  }
  void drain() { await_hook_threads(&list, NULL, NULL, 100); }
};
}

#define HeapAlloc hook_test::allocate
#define HeapFree hook_test::release
#define memmove hook_test::copy
#define CloseHandle hook_test::close
#define WaitForSingleObject hook_test::wait
#define CreateThread hook_test::thread
#undef CreateProcess
#define CreateProcess hook_test::process
#define GetExitCodeProcess hook_test::process_code
#define GetExitCodeThread hook_test::thread_code
#define await_single_handle hook_test::await
#define kill_process_tree hook_test::kill
#define get_process_creation_time hook_test::creation
#define set_service_environment hook_test::set_env
#define unset_service_environment hook_test::unset_env
#undef SetEnvironmentVariable
#define SetEnvironmentVariable hook_test::variable
#define get_hook hook_test::get
#define log_event hook_test::log
#include "../src/hook.cpp"
#undef HeapAlloc
#undef HeapFree
#undef memmove
#undef CloseHandle
#undef WaitForSingleObject
#undef CreateThread
#undef CreateProcess
#undef GetExitCodeProcess
#undef GetExitCodeThread
#undef await_single_handle
#undef kill_process_tree
#undef get_process_creation_time
#undef set_service_environment
#undef unset_service_environment
#undef SetEnvironmentVariable
#undef get_hook
#undef log_event

void test_hook_lifecycle() {
  { hook_test::fixture f; CHECK(f.run() == NSSM_HOOK_STATUS_SUCCESS); f.drain(); }
  { hook_test::fixture f; CHECK(f.run(false) == NSSM_HOOK_STATUS_SUCCESS); f.drain(); }
  { hook_test::fixture f; hook_test::fail_thread = true; CHECK(f.run() == NSSM_HOOK_STATUS_SUCCESS); f.drain(); }
  const DWORD codes[] = {0, 1, NSSM_HOOK_STATUS_ABORT};
  const int results[] = {NSSM_HOOK_STATUS_SUCCESS, NSSM_HOOK_STATUS_FAILED, NSSM_HOOK_STATUS_ABORT};
  for (size_t i = 0; i < _countof(codes); i++) {
    for (int mode = 0; mode < 3; mode++) {
      hook_test::fixture f;
      hook_test::process_exit = codes[i];
      hook_test::fail_thread = mode == 1;
      if (mode == 2) hook_test::fail_after = 1; // Tracking allocation fails.
      CHECK(f.run(false) == results[i]);
      if (mode == 1) CHECK(hook_test::events.back() == NSSM_EVENT_CREATETHREAD_FAILED);
      f.drain();
    }
  }
  for (int fallback = 0; fallback < 2; fallback++) {
    hook_test::fixture f;
    hook_test::fail_thread = fallback != 0;
    hook_test::process_wait = WAIT_TIMEOUT;
    CHECK(f.run(false) == NSSM_HOOK_STATUS_TIMEOUT);
    f.drain();
  }
  { hook_test::fixture f; hook_test::process_wait = WAIT_FAILED; CHECK(f.run(false) == NSSM_HOOK_STATUS_ERROR); f.drain(); }
  { hook_test::fixture f; hook_test::fail_exit_query = true; CHECK(f.run(false) == NSSM_HOOK_STATUS_ERROR); f.drain(); }
  { hook_test::fixture f; hook_test::fail_worker_exit = true; CHECK(f.run(false) == NSSM_HOOK_STATUS_ERROR); f.drain(); }
  { hook_test::fixture f; hook_test::fail_after = 0; CHECK(f.run() == NSSM_HOOK_STATUS_ERROR); f.drain(); }
  { hook_test::fixture f; hook_test::fail_process = true; CHECK(f.run() == NSSM_HOOK_STATUS_NOTRUN); f.drain(); }
  { hook_test::fixture f; hook_test::hook_lookup = 1; CHECK(f.run() == NSSM_HOOK_STATUS_NOTFOUND); f.drain(); }
  { hook_test::fixture f; hook_test::hook_lookup = 2; CHECK(f.run() == NSSM_HOOK_STATUS_ERROR); f.drain(); }
  { hook_test::fixture f; CHECK(f.run(false, MAXDWORD) == NSSM_HOOK_STATUS_SUCCESS); f.drain(); }
  { hook_test::fixture f; CHECK(f.run(false, 0) == NSSM_HOOK_STATUS_SUCCESS); f.drain(); }
  { hook_test::fixture f; CHECK(nssm_hook(NULL, f.service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, NULL, 20, true) == 0); }
  for (int failure = 0; failure < 2; failure++) {
    hook_test::fixture f;
    hook_test::deferred = true;
    hook_test::fail_worker_wait = failure != 0;
    hook_test::timeout_worker_wait = ! failure;
    CHECK(f.run(false) == (failure ? NSSM_HOOK_STATUS_ERROR : NSSM_HOOK_STATUS_TIMEOUT));
    CHECK(f.list.num_threads == 1);
    await_hook_threads(&f.list, NULL, NULL, 0);
    CHECK(f.list.num_threads == 1); // Failed/unfinished waits keep ownership.
    hook_test::fail_worker_wait = hook_test::timeout_worker_wait = false;
    f.drain();
    CHECK(f.list.num_threads == 0);
  }
  {
    hook_test::fixture f;
    hook_test::deferred = true;
    CHECK(f.run() == 0 && f.run() == 0 && f.list.num_threads == 2);
    hook_test::wait(f.list.data[0].thread_handle, 100);
    HANDLE retained = f.list.data[1].thread_handle;
    hook_test::fail_after = 0;
    await_hook_threads(&f.list, NULL, NULL, 0);
    CHECK(f.list.num_threads == 1 && f.list.data[0].thread_handle == retained);
    f.drain(); // Compaction/cleanup requires no allocation even under failure.
  }
  {
    hook_test::fixture f;
    hook_test::deferred = true;
    CHECK(f.run() == 0);
    hook_test::fail_after = 1;
    CHECK(f.run() == 0); // Synchronous fallback preserves the existing worker.
    CHECK(f.list.num_threads == 1);
    f.drain();
  }
}

namespace hook_test {
struct concurrent_context { hook_thread_t *list; nssm_service_t *service; HANDLE entered; };
static DWORD WINAPI reap_live(void *argument) {
  concurrent_context *c = static_cast<concurrent_context *>(argument);
  EnterCriticalSection(&c->service->hook_section);
  SetEvent(c->entered);
  await_hook_threads(c->list, NULL, NULL, 5000);
  LeaveCriticalSection(&c->service->hook_section);
  return 0;
}
static DWORD WINAPI launch_live(void *argument) {
  concurrent_context *c = static_cast<concurrent_context *>(argument);
  return nssm_hook(c->list, c->service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, NULL, 5000, true);
}
}

void test_hook_live_handles() {
  registry_fixture registry;
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("NSSM-Live-Hook-Ownership"));
  _tcscpy_s(service->dir, _T("."));
  service->initial_env = copy_environment();
  REQUIRE(service->initial_env);
  InitializeCriticalSection(&service->hook_section);
  hook_thread_t list = {};
  TCHAR exe[PATH_LENGTH];
  REQUIRE(GetModuleFileName(NULL, exe, _countof(exe)));
  std::basic_string<TCHAR> command = _T("\"") + std::basic_string<TCHAR>(exe) + _T("\" --hook-child \"final quoted argument\"");
  REQUIRE(set_hook(service->name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, &command[0]) == 0);
  hook_test::live = true;
  DWORD before = 0, after = 0;
  for (unsigned i = 0; i < 202; i++) {
    CHECK(nssm_hook(&list, service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, NULL, 5000, i % 2 != 0) == 0);
    EnterCriticalSection(&service->hook_section);
    await_hook_threads(&list, NULL, NULL, 5000);
    LeaveCriticalSection(&service->hook_section);
    REQUIRE(list.num_threads == 0 && list.data == NULL);
    if (i == 1) before = test_handle_count();
  }
  after = test_handle_count();
  CHECK(after == before);
  printf("Hook handles after 200 launches: before=%lu after=%lu\n", before, after);

  hook_test::cleanup_gate = CreateEvent(NULL, TRUE, FALSE, NULL);
  hook_test::cleanup_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
  REQUIRE(hook_test::cleanup_gate && hook_test::cleanup_entered);
  CHECK(nssm_hook(&list, service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, NULL, 5000, true) == 0);
  REQUIRE(WaitForSingleObject(hook_test::cleanup_entered, 5000) == WAIT_OBJECT_0);
  hook_test::concurrent_context context = { &list, service, CreateEvent(NULL, TRUE, FALSE, NULL) };
  REQUIRE(context.entered);
  HANDLE reaper = CreateThread(NULL, 0, hook_test::reap_live, &context, 0, NULL);
  REQUIRE(reaper && WaitForSingleObject(context.entered, 5000) == WAIT_OBJECT_0);
  HANDLE launcher = CreateThread(NULL, 0, hook_test::launch_live, &context, 0, NULL);
  REQUIRE(launcher);
  SetEvent(hook_test::cleanup_gate);
  REQUIRE(WaitForSingleObject(reaper, 5000) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(launcher, 5000) == WAIT_OBJECT_0);
  DWORD result;
  CHECK(GetExitCodeThread(launcher, &result) && result == 0);
  EnterCriticalSection(&service->hook_section);
  await_hook_threads(&list, NULL, NULL, 5000);
  LeaveCriticalSection(&service->hook_section);
  REQUIRE(list.num_threads == 0);
  CloseHandle(reaper); CloseHandle(launcher); CloseHandle(context.entered);
  CloseHandle(hook_test::cleanup_gate); CloseHandle(hook_test::cleanup_entered);
  hook_test::cleanup_gate = hook_test::cleanup_entered = NULL;
  hook_test::live = false;
  DeleteCriticalSection(&service->hook_section);
  HeapFree(GetProcessHeap(), 0, service->initial_env);
  delete service;
}
