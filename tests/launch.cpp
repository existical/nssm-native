#include "launch_fixture.h"

namespace launch_test {
static bool cleanup_race;
static volatile LONG cleanup_calls;
static HANDLE cleanup_entered, cleanup_release, stop_entered, cleanup_stopped;
static bool active, create_ok, terminate_ok, resume_ok, abort_hook, stopped;
static bool process_closed, thread_closed, resumed, affinity_ok = true;
static unsigned closes, output_closes, creates, resumes, waits, tree_calls;
static DWORD wait_result, create_error, launch_flags;
static int process_token, thread_token;
static HANDLE process_handle = &process_token, thread_handle = &thread_token;
static int duplicate_token, registration_token;
static HANDLE startup_handle = &duplicate_token, registration_handle = &registration_token;
static bool duplicate_ok, duplicate_open, startup_race, startup_waiting, waited_handle_closed;
static HANDLE startup_entered, startup_release, waited_handle;
static unsigned registrations, invalid_registrations, unregistrations;
static const TCHAR *cancel_hook_action;
static std::basic_string<TCHAR> live_image;
const TCHAR *imagepath() { return live_image.empty() ? nssm_imagepath() : &live_image[0]; }

static void reset() {
  active = create_ok = terminate_ok = resume_ok = true;
  abort_hook = stopped = process_closed = thread_closed = resumed = false;
  closes = output_closes = creates = resumes = waits = tree_calls = 0;
  wait_result = WAIT_OBJECT_0;
  create_error = ERROR_ACCESS_DENIED;
  launch_flags = 0;
  duplicate_ok = true; duplicate_open = startup_race = startup_waiting = waited_handle_closed = false;
  registrations = invalid_registrations = unregistrations = 0; cancel_hook_action = NULL;
}
BOOL WINAPI create(LPCTSTR app, LPTSTR cmd, LPSECURITY_ATTRIBUTES ps, LPSECURITY_ATTRIBUTES ts,
  BOOL inherit, DWORD flags, LPVOID env, LPCTSTR dir, LPSTARTUPINFO si, LPPROCESS_INFORMATION pi) {
  if (! active) return CreateProcess(app, cmd, ps, ts, inherit, flags | (live_image.empty() ? 0 : CREATE_NO_WINDOW), env, dir, si, pi);
  creates++;
  if (! create_ok) { SetLastError(create_error); return FALSE; }
  launch_flags = flags;
  pi->hProcess = process_handle; pi->hThread = thread_handle; pi->dwProcessId = 321;
  return TRUE;
}
BOOL WINAPI close(HANDLE h) {
  if (active && startup_waiting && h == waited_handle) waited_handle_closed = true;
  if (active && h == startup_handle) { CHECK(duplicate_open); duplicate_open = false; closes++; return TRUE; }
  if (! active || (h != process_handle && h != thread_handle)) return CloseHandle(h);
  if (h == process_handle) { CHECK(! process_closed); process_closed = true; }
  else { CHECK(! thread_closed); thread_closed = true; }
  closes++;
  return TRUE;
}
BOOL WINAPI duplicate(HANDLE source, HANDLE h, HANDLE target, LPHANDLE out, DWORD access, BOOL inherit, DWORD options) {
  if (! active) return DuplicateHandle(source, h, target, out, access, inherit, options);
  CHECK(h == process_handle && ! process_closed && ! duplicate_open);
  CHECK(access == SYNCHRONIZE && ! inherit && ! options);
  if (! duplicate_ok) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
  *out = startup_handle; duplicate_open = true; return TRUE;
}
BOOL WINAPI register_wait(PHANDLE out, HANDLE h, WAITORTIMERCALLBACK callback, PVOID context, ULONG timeout, ULONG flags) {
  if (! active) return RegisterWaitForSingleObject(out, h, callback, context, timeout, flags);
  registrations++;
  if (h != process_handle || process_closed) invalid_registrations++;
  *out = registration_handle; return TRUE;
}
BOOL WINAPI unregister_wait(HANDLE h) {
  if (! active) return UnregisterWait(h);
  unregistrations++;
  CHECK(h == registration_handle); return TRUE;
}
BOOL WINAPI terminate(HANDLE h, UINT result) {
  if (! active) return TerminateProcess(h, result);
  CHECK(h == process_handle && ! process_closed);
  stopped = terminate_ok;
  if (! terminate_ok) SetLastError(ERROR_ACCESS_DENIED);
  return terminate_ok;
}
DWORD WINAPI wait(HANDLE h, DWORD timeout) {
  if (! active) return WaitForSingleObject(h, timeout);
  CHECK((h == process_handle && ! process_closed) || (h == startup_handle && duplicate_open));
  CHECK(timeout <= 5000);
  waits++;
  if (startup_race) {
    waited_handle = h; startup_waiting = true;
    SetEvent(startup_entered);
    REQUIRE(WaitForSingleObject(startup_release, 10000) == WAIT_OBJECT_0);
    startup_waiting = false;
    return WAIT_OBJECT_0;
  }
  return wait_result;
}
DWORD WINAPI resume(HANDLE h) {
  if (! active) return ResumeThread(h);
  CHECK(h == thread_handle && ! thread_closed);
  resumes++;
  resumed = resume_ok;
  if (! resume_ok) SetLastError(ERROR_ACCESS_DENIED);
  return resume_ok ? 1 : (DWORD) -1;
}
BOOL WINAPI code(HANDLE h, LPDWORD value) {
  if (! active) return GetExitCodeProcess(h, value);
  CHECK(h == process_handle && ! process_closed);
  *value = stopped ? 0 : STILL_ACTIVE;
  return TRUE;
}
BOOL WINAPI affinity(HANDLE h, PDWORD_PTR process, PDWORD_PTR system) {
  if (! active) return GetProcessAffinityMask(h, process, system);
  *process = *system = 1; return TRUE;
}
BOOL WINAPI set_affinity(HANDLE h, DWORD_PTR mask) { if (! active) return SetProcessAffinityMask(h, mask); if (! affinity_ok) SetLastError(ERROR_ACCESS_DENIED); return affinity_ok; }
BOOL WINAPI status(SERVICE_STATUS_HANDLE h, LPSERVICE_STATUS value) {
  if (cleanup_race && value->dwCurrentState == SERVICE_STOPPED) SetEvent(cleanup_stopped);
  return active ? TRUE : SetServiceStatus(h, value);
}
BOOL WINAPI free_console() { return active ? TRUE : FreeConsole(); }
int parameters(nssm_service_t *service, STARTUPINFO *si) { return active ? 0 : get_parameters(service, si); }
int output(nssm_service_t *service, STARTUPINFO *si) { return active ? 0 : get_output_handles(service, si); }
void close_output(STARTUPINFO *si) { if (active) output_closes++; else close_output_handles(si); }
void cleanup(nssm_service_t *service) {
  if (cleanup_race) {
    InterlockedIncrement(&cleanup_calls);
    SetEvent(cleanup_entered);
    REQUIRE(WaitForSingleObject(cleanup_release, 5000) == WAIT_OBJECT_0);
  }
  if (! active) cleanup_loggers(service);
}
void environment(TCHAR *env) { if (! active) duplicate_environment_strings(env); }
int time(HANDLE h, FILETIME *value) { if (! active) return get_process_creation_time(h, value); ZeroMemory(value, sizeof(*value)); return 0; }
int exit_time(HANDLE h, FILETIME *value) { if (! active) return get_process_exit_time(h, value); ZeroMemory(value, sizeof(*value)); return 0; }
int kill(kill_t *k) { if (! active) return kill_process(k); CHECK(k->process_handle == process_handle); stopped = true; return 1; }
unsigned long kill_tree(kill_t *k, unsigned long pid) {
  if (! active) return kill_process_tree(k, pid);
  CHECK(k->process_handle == process_handle && ! process_closed && k->pid == pid);
  tree_calls++; return 0;
}
int hook(hook_thread_t *list, nssm_service_t *service, TCHAR *event, TCHAR *action, unsigned long *control, unsigned long deadline, bool async) {
  if (! active) return nssm_hook(list, service, event, action, control, deadline, async);
  if (cancel_hook_action && ! _tcscmp(event, NSSM_HOOK_EVENT_START) && ! _tcscmp(action, cancel_hook_action)) {
    cancel_hook_action = NULL;
    stop_service(service, 0, true, true);
  }
  return abort_hook && ! _tcscmp(event, NSSM_HOOK_EVENT_START) && ! _tcscmp(action, NSSM_HOOK_ACTION_PRE) ? NSSM_HOOK_STATUS_ABORT : NSSM_HOOK_STATUS_NOTFOUND;
}
int hook(hook_thread_t *l, nssm_service_t *s, TCHAR *e, TCHAR *a, unsigned long *c, unsigned long d) { return hook(l,s,e,a,c,d,true); }
int hook(hook_thread_t *l, nssm_service_t *s, TCHAR *e, TCHAR *a, unsigned long *c) { return hook(l,s,e,a,c,NSSM_HOOK_DEADLINE,true); }

static void environment_case(bool created, bool terminated, DWORD waited, int expected) {
  reset(); create_ok = created; terminate_ok = terminated; wait_result = waited;
  TCHAR block[] = _T("A=1\0");
  CHECK(test_environment(block) == expected);
  CHECK(closes == (created ? 2U : 0U));
  CHECK(waits == (created ? 1U : 0U));
  if (created) CHECK(launch_flags & CREATE_SUSPENDED);
  active = false;
}
static void service_case(bool suspended, bool created, bool resume_success, bool abort, unsigned long throttle = 1, bool duplicate_success = true) {
  reset(); create_ok = created; resume_ok = resume_success; abort_hook = abort; wait_result = WAIT_TIMEOUT; duplicate_ok = duplicate_success;
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("Launch test")); _tcscpy_s(service->exe, _T("fake.exe"));
  service->allow_restart = true; service->no_console = true; service->throttle_delay = throttle;
  service->affinity = suspended ? 1 : 0;
  service->kill_process_tree = true;
  InitializeCriticalSection(&service->hook_section);
  int result = start_service(service);
  if (abort || ! created || (suspended && ! resume_success) || ! duplicate_success) {
    CHECK(result != 0);
    CHECK(service->status.dwCurrentState == SERVICE_STOPPED);
    CHECK(! service->process_handle);
  }
  else {
    CHECK(result == 0 && service->status.dwCurrentState == SERVICE_RUNNING);
    CHECK(thread_closed && ! process_closed); // Process ownership stays with service.
    CHECK(resumes == (suspended ? 1U : 0U));
    CHECK(waits == 1 && service->throttle == 0);
    CHECK(stop_service(service, 0, true, true) == 0);
    CHECK(tree_calls == 1 && process_closed);
  }
  CHECK(output_closes == 1);
  unsigned expected = created && ! abort ? 2U : 0U;
  if (expected && (! suspended || resume_success) && duplicate_success) expected++; // Owned startup observer.
  CHECK(closes == expected && ! duplicate_open);
  DeleteCriticalSection(&service->hook_section);
  delete service; active = false;
}
}

void test_launch_events() {
  using namespace launch_test;
  event_test::records.clear(); event_test::active = true;
  reset(); create_ok = false;
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("Event test")); _tcscpy_s(service->exe, _T("fake.exe"));
  service->allow_restart = true; service->no_console = true;
  InitializeCriticalSection(&service->hook_section);
  int result = monitor_service(service);
  CHECK(result != 0);
  REQUIRE(! event_test::records.empty());
  const event_test::record &last = event_test::records.back();
  CHECK(last.id == NSSM_EVENT_START_SERVICE_FAILED && last.type == EVENTLOG_ERROR_TYPE);
  REQUIRE(last.strings.size() == 3);
  CHECK(last.strings[0] == _T("fake.exe") && last.strings[1] == _T("Event test"));
  CHECK(last.strings[2] == std::to_wstring(result));
  DeleteCriticalSection(&service->hook_section); delete service; active = false;
  event_test::records.clear(); affinity_ok = false;
  service_case(true, true, true, false);
  bool found = false;
  for (const auto &entry : event_test::records) if (entry.id == NSSM_EVENT_SETPROCESSAFFINITYMASK_FAILED) {
    found = true; REQUIRE(entry.strings.size() == 2);
    CHECK(entry.strings[0] == _T("Launch test"));
    CHECK(entry.strings[1] == error_string(ERROR_ACCESS_DENIED));
  }
  CHECK(found); affinity_ok = true; event_test::active = false;
}

static DWORD WINAPI exit_cleanup_thread(void *service) {
  end_service(service, true);
  return 0;
}
static DWORD WINAPI stop_cleanup_thread(void *service) {
  SetEvent(launch_test::stop_entered);
  return stop_service(static_cast<nssm_service_t *>(service), 0, true, true);
}
static void test_cleanup_overlap() {
  using namespace launch_test;
  reset(); cleanup_race = true; cleanup_calls = 0;
  cleanup_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
  cleanup_release = CreateEvent(NULL, TRUE, FALSE, NULL);
  stop_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
  cleanup_stopped = CreateEvent(NULL, TRUE, FALSE, NULL);
  REQUIRE(cleanup_entered && cleanup_release && stop_entered && cleanup_stopped);
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("Cleanup overlap test"));
  InitializeCriticalSection(&service->hook_section); service->hook_section_initialised = true;
  HANDLE exiting = CreateThread(NULL, 0, exit_cleanup_thread, service, 0, NULL); REQUIRE(exiting);
  REQUIRE(WaitForSingleObject(cleanup_entered, 5000) == WAIT_OBJECT_0);
  HANDLE stopping = CreateThread(NULL, 0, stop_cleanup_thread, service, 0, NULL); REQUIRE(stopping);
  REQUIRE(WaitForSingleObject(stop_entered, 5000) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(stopping, 200) == WAIT_TIMEOUT);
  CHECK(WaitForSingleObject(cleanup_stopped, 0) == WAIT_TIMEOUT);
  SetEvent(cleanup_release);
  REQUIRE(WaitForSingleObject(exiting, 5000) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(stopping, 5000) == WAIT_OBJECT_0);
  CHECK(cleanup_calls == 1 && WaitForSingleObject(cleanup_stopped, 0) == WAIT_OBJECT_0);
  CloseHandle(exiting); CloseHandle(stopping);
  CloseHandle(cleanup_entered); CloseHandle(cleanup_release); CloseHandle(stop_entered); CloseHandle(cleanup_stopped);
  DeleteCriticalSection(&service->hook_section); delete service;
  cleanup_race = active = false;
}

static DWORD WINAPI startup_monitor_thread(void *service) { return monitor_service(static_cast<nssm_service_t *>(service)); }
static DWORD WINAPI startup_stop_thread(void *service) { return stop_service(static_cast<nssm_service_t *>(service), 0, true, true); }
static nssm_service_t *startup_fixture() {
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("Startup cancellation test")); _tcscpy_s(service->exe, _T("fake.exe"));
  service->allow_restart = true; service->no_console = true; service->throttle_delay = 1;
  InitializeCriticalSection(&service->hook_section); service->hook_section_initialised = true;
  return service;
}
static void test_startup_cancellation() {
  using namespace launch_test;
  reset(); startup_race = true;
  startup_entered = CreateEvent(NULL, TRUE, FALSE, NULL); startup_release = CreateEvent(NULL, TRUE, FALSE, NULL);
  REQUIRE(startup_entered && startup_release);
  nssm_service_t *service = startup_fixture();
  HANDLE starting = CreateThread(NULL, 0, startup_monitor_thread, service, 0, NULL); REQUIRE(starting);
  REQUIRE(WaitForSingleObject(startup_entered, 5000) == WAIT_OBJECT_0);
  HANDLE stopping = CreateThread(NULL, 0, startup_stop_thread, service, 0, NULL); REQUIRE(stopping);
  CHECK(WaitForSingleObject(stopping, 2000) == WAIT_OBJECT_0); // Startup wait must not hold the cleanup lock.
  SetEvent(startup_release);
  REQUIRE(WaitForSingleObject(starting, 5000) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(stopping, 5000) == WAIT_OBJECT_0);
  CHECK(! waited_handle_closed);
  CHECK(! registrations && ! invalid_registrations && ! service->wait_handle);
  CHECK(service->status.dwCurrentState == SERVICE_STOPPED && ! service->process_handle && ! duplicate_open);
  CloseHandle(starting); CloseHandle(stopping); CloseHandle(startup_entered); CloseHandle(startup_release);
  DeleteCriticalSection(&service->hook_section); delete service;
  for (const TCHAR *action : {NSSM_HOOK_ACTION_PRE, NSSM_HOOK_ACTION_POST}) {
    reset(); wait_result = WAIT_TIMEOUT; cancel_hook_action = action; service = startup_fixture();
    CHECK(monitor_service(service) == 0);
    CHECK(! registrations && ! invalid_registrations && ! service->wait_handle);
    if (! _tcscmp(action, NSSM_HOOK_ACTION_PRE)) CHECK(! creates);
    CHECK(service->status.dwCurrentState == SERVICE_STOPPED && ! service->process_handle);
    if (service->process_handle) { service->stopping = false; stop_service(service, 0, true, true); }
    DeleteCriticalSection(&service->hook_section); delete service;
  }
  reset(); service = startup_fixture(); service->allow_restart = false; service->stopping = true;
  service->status.dwCurrentState = SERVICE_STOPPED;
  CHECK(monitor_service(service) == 0 && ! creates && ! registrations);
  CHECK(service->stopping && service->status.dwCurrentState == SERVICE_STOPPED);
  DeleteCriticalSection(&service->hook_section); delete service;
  reset(); wait_result = WAIT_TIMEOUT; service = startup_fixture(); service->wait_handle = registration_handle;
  CHECK(monitor_service(service) == 0 && registrations == 1 && unregistrations == 1);
  CHECK(! invalid_registrations && service->wait_handle == registration_handle);
  CHECK(stop_service(service, 0, true, true) == 0 && unregistrations == 2);
  DeleteCriticalSection(&service->hook_section); delete service;
  active = false;
}

void test_launch_handles() {
  test_startup_cancellation();
  test_cleanup_overlap();
  launch_test::environment_case(true, true, WAIT_OBJECT_0, 0);
  launch_test::environment_case(true, false, WAIT_FAILED, -1);
  launch_test::environment_case(true, true, WAIT_TIMEOUT, -1);
  launch_test::environment_case(true, false, WAIT_OBJECT_0, 0); // Already exited.
  launch_test::environment_case(false, false, WAIT_FAILED, -1);
  launch_test::service_case(false, true, true, false);
  launch_test::service_case(false, true, true, false, 0);
  launch_test::service_case(true, true, true, false);
  launch_test::service_case(false, false, true, false);
  launch_test::service_case(false, true, true, true);
  launch_test::service_case(true, true, false, false);
  launch_test::service_case(false, true, true, false, 1, false); // Failed observer duplication must clean up.
  launch_test::reset();
  launch_test::create_ok = false;
  launch_test::create_error = ERROR_INVALID_PARAMETER;
  TCHAR empty[] = { 0, 0 };
  CHECK(test_environment(empty) == 1);
  CHECK(! launch_test::closes);
  launch_test::active = false;
  for (unsigned i = 0; i < 200; i++) launch_test::service_case(i % 2 != 0, true, true, false);

  TCHAR exe[PATH_LENGTH];
  REQUIRE(GetModuleFileName(NULL, exe, _countof(exe)));
  launch_test::live_image = _T("\"") + std::basic_string<TCHAR>(exe) + _T("\"");
  TCHAR block[] = _T("NSSM_VALIDATION_TEST=1\0");
  REQUIRE(test_environment(block) == 0); // Warm-up before handle accounting.
  DWORD before, after;
  before = test_handle_count();
  for (unsigned i = 0; i < 200; i++) CHECK(test_environment(block) == 0);
  after = test_handle_count();
  CHECK(after == before);
  printf("Validator handles after 200 launches: before=%lu after=%lu\n", before, after);
  launch_test::live_image.clear();
}
