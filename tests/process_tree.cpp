#include "test.h"
#include <map>
#include "token_fixture.h"
extern imports_t imports;
namespace process_live_test { extern bool active; bool owns(DWORD); }

namespace process_test {
struct process { DWORD pid, parent; ULONGLONG born, exited; bool running, denied, times_fail, wait_fail, terminate_fail; unsigned opens, refs; DWORD access; };
struct handle { process *object; bool closed; std::vector<PROCESSENTRY32> entries; std::vector<THREADENTRY32> threads; size_t index; };
static bool active, swap_on_close, swap_before_open, vanish_before_open, no_id, no_owner, snapshot_fail, first_fail, next_fail;
static bool exit_on_attach, change_window_owner;
static unsigned diagnostics, posts, thread_posts, attaches, events, detaches, handlers;
static DWORD window_pid;
static ULONGLONG clock_value = 1000;
static std::vector<process *> objects, killed;
static std::vector<handle *> handles;
static std::map<DWORD, process *> current;
static std::map<DWORD, process *> threads;
static std::vector<DWORD> visited;
static process *old_child, *replacement;

static FILETIME filetime(ULONGLONG value) { FILETIME ft = {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)}; return ft; }
static process *add(DWORD pid, DWORD parent, ULONGLONG born, ULONGLONG exited = 0) {
  process *p = new process(); p->pid = pid; p->parent = parent; p->born = born; p->exited = exited; p->running = ! exited;
  objects.push_back(p); current[pid] = p; return p;
}
static HANDLE pin(process *p) {
  handle *h = new handle(); h->object = p; if (p) p->refs++; handles.push_back(h); return h;
}
static handle *lookup(HANDLE value) {
  handle *h = static_cast<handle *>(value);
  REQUIRE(h && ! h->closed); return h;
}
static HANDLE WINAPI open(DWORD access, BOOL inherit, DWORD pid) {
  if (process_live_test::active && ! process_live_test::owns(pid)) { CHECK(false); SetLastError(ERROR_ACCESS_DENIED); return NULL; }
  if (! active) return OpenProcess(access, inherit, pid);
  if (swap_before_open && old_child && pid == old_child->pid) { current[pid] = replacement; swap_before_open = false; }
  if (vanish_before_open) { current[pid] = NULL; vanish_before_open = false; }
  process *p = current[pid];
  if (! p || p->denied) { SetLastError(ERROR_ACCESS_DENIED); return NULL; }
  p->opens++; p->access = access; return pin(p);
}
static BOOL WINAPI close(HANDLE value) {
  if (token_test::active) return token_test::close(value);
  if (! active) return CloseHandle(value);
  handle *h = lookup(value); h->closed = true;
  if (h->object) {
    h->object->refs--;
    if (swap_on_close && h->object == old_child && ! old_child->refs) {
      old_child->running = false; old_child->exited = clock_value;
      current[old_child->pid] = replacement; swap_on_close = false;
    }
  }
  return TRUE;
}
static BOOL WINAPI times(HANDLE value, LPFILETIME born, LPFILETIME exited, LPFILETIME kernel, LPFILETIME user) {
  if (! active) return GetProcessTimes(value, born, exited, kernel, user);
  process *p = lookup(value)->object; REQUIRE(p);
  if (p->times_fail) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  *born = filetime(p->born); *exited = filetime(p->exited); *kernel = *user = filetime(0); return TRUE;
}
static DWORD WINAPI id(HANDLE value) { return active ? lookup(value)->object->pid : 0; }
static DWORD WINAPI owner(HANDLE value) { return lookup(value)->object->parent; }
static FARPROC WINAPI symbol(HMODULE module, LPCSTR name) {
  if (active && ! strcmp(name, "GetProcessId")) return no_id ? NULL : reinterpret_cast<FARPROC>(id);
  if (active && ! strcmp(name, "GetProcessIdOfThread")) return no_owner ? NULL : reinterpret_cast<FARPROC>(owner);
  return GetProcAddress(module, name);
}
static BOOL WINAPI code(HANDLE value, LPDWORD result) {
  if (! active) return GetExitCodeProcess(value, result);
  process *p = lookup(value)->object; REQUIRE(p); *result = p->running ? STILL_ACTIVE : 0; return TRUE;
}
static DWORD WINAPI wait(HANDLE value, DWORD timeout) {
  if (! active) return WaitForSingleObject(value, timeout);
  process *p = lookup(value)->object;
  return p->wait_fail ? WAIT_FAILED : p->running ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}
static BOOL WINAPI terminate(HANDLE value, UINT result) {
  if (process_live_test::active) {
    typedef DWORD (WINAPI *id_fn)(HANDLE);
    id_fn fn = reinterpret_cast<id_fn>(GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetProcessId"));
    REQUIRE(fn);
    if (! process_live_test::owns(fn(value))) { CHECK(false); SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  }
  if (! active) return TerminateProcess(value, result);
  process *p = lookup(value)->object; REQUIRE(p);
  if (p->terminate_fail) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  killed.push_back(p); p->running = false; p->exited = clock_value; return TRUE;
}
static HANDLE WINAPI snapshot(DWORD flags, DWORD pid) {
  if (! active) return CreateToolhelp32Snapshot(flags, pid);
  if (snapshot_fail) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
  HANDLE value = pin(NULL); handle *h = lookup(value);
  if (flags == TH32CS_SNAPTHREAD) {
    for (std::map<DWORD, process *>::iterator i = threads.begin(); i != threads.end(); ++i) {
      THREADENTRY32 entry = {}; entry.dwSize = sizeof(entry); entry.th32ThreadID = i->first;
      entry.th32OwnerProcessID = 10; // Snapshot can contain a stale TID/owner pair.
      h->threads.push_back(entry);
    }
    return value;
  }
  REQUIRE(flags == TH32CS_SNAPPROCESS);
  for (std::map<DWORD, process *>::iterator i = current.begin(); i != current.end(); ++i) {
    if (! i->second) continue;
    PROCESSENTRY32 entry = {}; entry.dwSize = sizeof(entry); entry.th32ProcessID = i->first; entry.th32ParentProcessID = i->second->parent;
    h->entries.push_back(entry);
  }
  return value;
}
static BOOL first(HANDLE value, LPPROCESSENTRY32 entry) {
  if (! active) {
    BOOL result = Process32First(value, entry);
    while (result && process_live_test::active && ! process_live_test::owns(entry->th32ProcessID)) result = Process32Next(value, entry);
    return result;
  }
  handle *h = lookup(value); h->index = 0;
  if (first_fail) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  if (h->entries.empty()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
  *entry = h->entries[0]; return TRUE;
}
static BOOL next(HANDLE value, LPPROCESSENTRY32 entry) {
  if (! active) {
    BOOL result = Process32Next(value, entry);
    while (result && process_live_test::active && ! process_live_test::owns(entry->th32ProcessID)) result = Process32Next(value, entry);
    return result;
  }
  handle *h = lookup(value);
  if (next_fail) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  if (++h->index == h->entries.size()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
  *entry = h->entries[h->index]; return TRUE;
}
static void WINAPI now(LPFILETIME value) { if (active) *value = filetime(clock_value); else GetSystemTimeAsFileTime(value); }
static void log(unsigned short level, unsigned long, ...) { if (active && level == EVENTLOG_ERROR_TYPE) diagnostics++; }
static HANDLE WINAPI open_thread(DWORD access, BOOL inherit, DWORD tid) {
  if (! active) return OpenThread(access, inherit, tid);
  process *p = threads[tid];
  if (! p || p->denied) return NULL;
  p->access = access; return pin(p);
}
static BOOL WINAPI thread_times(HANDLE value, LPFILETIME born, LPFILETIME exited, LPFILETIME kernel, LPFILETIME user) {
  return active ? times(value, born, exited, kernel, user) : GetThreadTimes(value, born, exited, kernel, user);
}
static BOOL WINAPI thread_first(HANDLE value, LPTHREADENTRY32 entry) {
  if (! active) return Thread32First(value, entry);
  handle *h = lookup(value); h->index = 0;
  if (h->threads.empty()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
  *entry = h->threads[0]; return TRUE;
}
static BOOL WINAPI thread_next(HANDLE value, LPTHREADENTRY32 entry) {
  if (! active) return Thread32Next(value, entry);
  handle *h = lookup(value);
  if (++h->index == h->threads.size()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
  *entry = h->threads[h->index]; return TRUE;
}
static BOOL WINAPI post_thread(DWORD tid, UINT message, WPARAM w, LPARAM l) {
  if (! active) return PostThreadMessage(tid, message, w, l);
  process *p = threads[tid]; REQUIRE(p && p->refs && p->parent == 10 && p->running);
  CHECK(message == WM_QUIT); thread_posts++; return TRUE;
}
static DWORD WINAPI window_owner(HWND hwnd, LPDWORD pid) {
  if (! active) return GetWindowThreadProcessId(hwnd, pid);
  *pid = window_pid; return 777;
}
static BOOL WINAPI post(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (! active) return PostMessage(hwnd, message, w, l);
  CHECK(current[10]->refs && current[10]->running && window_pid == 10); posts++;
  if (change_window_owner) window_pid = 999;
  return TRUE;
}
static BOOL WINAPI attach(DWORD pid) {
  CHECK(active && pid == 10 && current[10]->refs); attaches++;
  if (exit_on_attach) current[10]->running = false;
  return TRUE;
}
static BOOL WINAPI console_handler(PHANDLER_ROUTINE handler, BOOL add) {
  if (! active) return SetConsoleCtrlHandler(handler, add);
  handlers += add ? 1 : -1; return TRUE;
}
static BOOL WINAPI console_event(DWORD event, DWORD group) {
  if (! active) return GenerateConsoleCtrlEvent(event, group);
  CHECK(current[10]->refs && current[10]->running); events++; return TRUE;
}
static BOOL WINAPI detach() { if (! active) return FreeConsole(); detaches++; return TRUE; }
static int await(SERVICE_STATUS_HANDLE h, SERVICE_STATUS *s, HANDLE p, TCHAR *name, TCHAR *function, unsigned long timeout) {
  if (! active) return await_single_handle(h, s, p, name, function, timeout);
  return wait(p, timeout) == WAIT_OBJECT_0 ? 0 : 1;
}
static int visit(nssm_service_t *, kill_t *k) { visited.push_back(k->pid); return 1; }

struct fixture {
  kill_t context;
  process *root;
  AttachConsole_ptr saved_attach;
  fixture() : context() {
    active = true;
    swap_on_close = swap_before_open = vanish_before_open = no_id = no_owner = snapshot_fail = first_fail = next_fail = false;
    exit_on_attach = change_window_owner = false;
    diagnostics = posts = thread_posts = attaches = events = detaches = handlers = 0; window_pid = 10;
    saved_attach = imports.AttachConsole; imports.AttachConsole = attach;
    old_child = replacement = NULL; clock_value = 1000;
    root = add(10, 1, 100);
    context.name = _T("Process identity test"); context.pid = root->pid;
    context.process_handle = pin(root); context.creation_time = filetime(100); context.exit_time = filetime(1000);
    context.stop_method = NSSM_STOP_METHOD_TERMINATE;
  }
  ~fixture() {
    // Root is borrowed by the walker; the fixture remains its owner.
    if (context.process_handle) close(context.process_handle);
    for (size_t i = 0; i < handles.size(); i++) { CHECK(handles[i]->closed); delete handles[i]; }
    for (size_t i = 0; i < objects.size(); i++) { CHECK(! objects[i]->refs); delete objects[i]; }
    handles.clear(); objects.clear(); killed.clear(); current.clear(); threads.clear(); visited.clear(); active = false;
    imports.AttachConsole = saved_attach;
  }
};
}

#define OpenProcess process_test::open
#define CloseHandle process_test::close
#define GetProcessTimes process_test::times
#define GetExitCodeProcess process_test::code
#define GetProcAddress process_test::symbol
#define WaitForSingleObject process_test::wait
#define TerminateProcess process_test::terminate
#define CreateToolhelp32Snapshot process_test::snapshot
#undef Process32First
#undef Process32Next
#define Process32First process_test::first
#define Process32Next process_test::next
#define GetSystemTimeAsFileTime process_test::now
#define log_event process_test::log
#define OpenThread process_test::open_thread
#define GetThreadTimes process_test::thread_times
#define Thread32First process_test::thread_first
#define Thread32Next process_test::thread_next
#undef PostThreadMessage
#define PostThreadMessage process_test::post_thread
#define GetWindowThreadProcessId process_test::window_owner
#undef PostMessage
#define PostMessage process_test::post
#define SetConsoleCtrlHandler process_test::console_handler
#define GenerateConsoleCtrlEvent process_test::console_event
#define FreeConsole process_test::detach
#define await_single_handle process_test::await
#define OpenThreadToken token_test::open
#define ImpersonateSelf token_test::impersonate
#undef LookupPrivilegeValue
#define LookupPrivilegeValue token_test::lookup
#define AdjustTokenPrivileges token_test::adjust
#define RevertToSelf token_test::revert
#define nssm_exit token_test::exit
#include "../src/process.cpp"
#undef OpenThreadToken
#undef ImpersonateSelf
#undef LookupPrivilegeValue
#undef AdjustTokenPrivileges
#undef RevertToSelf
#undef nssm_exit
#undef OpenProcess
#undef CloseHandle
#undef GetProcessTimes
#undef GetExitCodeProcess
#undef GetProcAddress
#undef WaitForSingleObject
#undef TerminateProcess
#undef CreateToolhelp32Snapshot
#undef Process32First
#undef Process32Next
#undef GetSystemTimeAsFileTime
#undef log_event
#undef OpenThread
#undef GetThreadTimes
#undef Thread32First
#undef Thread32Next
#undef PostThreadMessage
#undef GetWindowThreadProcessId
#undef PostMessage
#undef SetConsoleCtrlHandler
#undef GenerateConsoleCtrlEvent
#undef FreeConsole
#undef await_single_handle

void test_process_identity() {
  using namespace process_test;
  {
    process_test::fixture f;
    process_test::old_child = process_test::add(20, 10, 200);
    process_test::replacement = process_test::add(20, 999, 2000);
    process_test::current[20] = process_test::old_child;
    process_test::swap_on_close = true;
    HANDLE root_handle = f.context.process_handle;
    CHECK(kill_process_tree(&f.context, f.context.pid) == 0);
    CHECK(f.root->opens == 0);
    CHECK(f.context.process_handle == root_handle);
    CHECK(process_test::killed.size() == 2);
    CHECK(process_test::killed.back() == process_test::old_child);
    CHECK(old_child->opens == 1 && ! replacement->opens);
    // The legacy walker overwrites this borrowed field with a closed handle.
    f.context.process_handle = root_handle;
  }
  {
    process_test::fixture f;
    process_test::close(f.context.process_handle); f.context.process_handle = NULL;
    process_test::add(10, 999, 2000);
    CHECK(kill_process_tree(&f.context, f.context.pid) != 0);
    CHECK(process_test::killed.empty());
    f.context.process_handle = NULL;
  }
  for (unsigned scenario = 0; scenario < 6; scenario++) {
    fixture f;
    if (scenario == 0) f.context.creation_time = filetime(101);
    if (scenario == 1) f.context.pid = 999;
    if (scenario == 2) f.context.creation_time = filetime(0);
    if (scenario == 3) no_id = true;
    if (scenario == 4) f.root->times_fail = true;
    if (scenario == 5) f.root->wait_fail = true;
    CHECK(kill_process_tree(&f.context, f.context.pid) != 0);
    CHECK(killed.empty() && diagnostics);
  }
  for (unsigned scenario = 0; scenario < 2; scenario++) {
    fixture f;
    old_child = add(20, 10, 200);
    replacement = add(20, 999, scenario ? 200 : 2000);
    current[20] = old_child; swap_before_open = true;
    CHECK(kill_process_tree(&f.context, f.context.pid) == 1);
    CHECK(killed.size() == 1 && killed[0] == f.root);
    CHECK(replacement->opens == 1 && ! old_child->opens);
  }
  for (unsigned scenario = 0; scenario < 6; scenario++) {
    fixture f;
    process *child = add(20, 10, 200);
    process *sibling = add(30, 10, 250);
    if (scenario == 0) child->denied = true;
    if (scenario == 1) vanish_before_open = true;
    if (scenario == 2) child->times_fail = true;
    if (scenario == 3) child->born = 50;
    if (scenario == 4) { f.root->running = false; f.root->exited = 300; child->born = 400; }
    if (scenario == 5) child->born = 2000;
    CHECK(kill_process_tree(&f.context, f.context.pid) == 1);
    CHECK(child->running && ! sibling->running && diagnostics);
  }
  {
    fixture f;
    f.root->running = false; f.root->exited = 150;
    process *child = add(20, 10, 120);
    process *grandchild = add(30, 20, 200); // Born after the ROOT exited, before its own parent exited.
    kill_t original = f.context;
    CHECK(kill_process_tree(&f.context, f.context.pid) == 0);
    CHECK(! child->running && ! grandchild->running && killed.size() == 2);
    CHECK(! memcmp(&original, &f.context, sizeof(original)));
  }
  {
    fixture f;
    process *child = add(20, 10, 200);
    process *grandchild = add(30, 20, 150); // Predates its immediate parent, but not the root.
    CHECK(kill_process_tree(&f.context, f.context.pid) == 1);
    CHECK(! child->running && grandchild->running);
  }
  {
    fixture f;
    process *child = add(20, 10, 200);
    CHECK(walk_process_tree(NULL, visit, &f.context, f.context.pid) == 0);
    CHECK(visited.size() == 2 && killed.empty());
    CHECK(!(child->access & PROCESS_TERMINATE) && (child->access & PROCESS_VM_READ));
  }
  for (unsigned scenario = 0; scenario < 4; scenario++) {
    fixture f;
    snapshot_fail = scenario == 0; first_fail = scenario == 1; next_fail = scenario == 2;
    f.root->terminate_fail = scenario == 3;
    CHECK(kill_process_tree(&f.context, f.context.pid) != 0);
    CHECK(diagnostics);
  }
  {
    fixture f;
    for (unsigned i = 1; i <= 130; i++) add(10 + i, 9 + i, 100 + i);
    CHECK(kill_process_tree(&f.context, f.context.pid) == 1);
    CHECK(killed.size() == 128 && current[138]->running);
  }
  for (unsigned scenario = 0; scenario < 8; scenario++) {
    fixture f;
    process *thread = add(777, 10, 200); current.erase(777); threads[777] = thread;
    if (scenario == 1) thread->parent = 999;
    if (scenario == 2) thread->born = 2000;
    if (scenario == 3) thread->denied = true;
    if (scenario == 4) thread->times_fail = true;
    if (scenario == 5) thread->running = false;
    if (scenario == 6) no_owner = true;
    if (scenario == 7) f.root->running = false;
    CHECK(kill_threads(&f.context) == (scenario == 0 ? 1 : 0));
    CHECK(thread_posts == (scenario == 0 ? 1U : 0U));
  }
  for (unsigned scenario = 0; scenario < 3; scenario++) {
    fixture f;
    change_window_owner = scenario == 1;
    if (scenario == 2) f.root->running = false;
    kill_window(reinterpret_cast<HWND>(1), reinterpret_cast<LPARAM>(&f.context));
    CHECK(posts == (scenario == 0 ? 2U : scenario == 1 ? 1U : 0U));
  }
  for (unsigned scenario = 0; scenario < 3; scenario++) {
    fixture f;
    exit_on_attach = scenario == 1;
    if (scenario == 2) f.root->running = false;
    kill_console(&f.context);
    CHECK(events == (scenario == 0 ? 1U : 0U));
    CHECK(attaches == detaches && attaches == (scenario == 2 ? 0U : 1U) && ! handlers);
  }
}
