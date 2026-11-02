#include "launch_fixture.h"

namespace launch_test {
static bool active, create_ok, terminate_ok, resume_ok, abort_hook, stopped;
static bool process_closed, thread_closed, resumed;
static unsigned closes, output_closes, creates, resumes, waits, tree_calls;
static DWORD wait_result, create_error, launch_flags;
static int process_token, thread_token;
static HANDLE process_handle = &process_token, thread_handle = &thread_token;
static std::basic_string<TCHAR> live_image;
const TCHAR *imagepath() { return live_image.empty() ? nssm_imagepath() : &live_image[0]; }

static void reset() {
  active = create_ok = terminate_ok = resume_ok = true;
  abort_hook = stopped = process_closed = thread_closed = resumed = false;
  closes = output_closes = creates = resumes = waits = tree_calls = 0;
  wait_result = WAIT_OBJECT_0;
  create_error = ERROR_ACCESS_DENIED;
  launch_flags = 0;
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
  if (! active || (h != process_handle && h != thread_handle)) return CloseHandle(h);
  if (h == process_handle) { CHECK(! process_closed); process_closed = true; }
  else { CHECK(! thread_closed); thread_closed = true; }
  closes++;
  return TRUE;
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
  CHECK(h == process_handle && ! process_closed);
  CHECK(timeout && timeout <= 5000);
  waits++;
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
BOOL WINAPI set_affinity(HANDLE h, DWORD_PTR mask) { return active ? TRUE : SetProcessAffinityMask(h, mask); }
BOOL WINAPI status(SERVICE_STATUS_HANDLE h, LPSERVICE_STATUS value) { return active ? TRUE : SetServiceStatus(h, value); }
BOOL WINAPI free_console() { return active ? TRUE : FreeConsole(); }
int parameters(nssm_service_t *service, STARTUPINFO *si) { return active ? 0 : get_parameters(service, si); }
int output(nssm_service_t *service, STARTUPINFO *si) { return active ? 0 : get_output_handles(service, si); }
void close_output(STARTUPINFO *si) { if (active) output_closes++; else close_output_handles(si); }
void cleanup(nssm_service_t *service) { if (! active) cleanup_loggers(service); }
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
  return abort_hook && ! _tcscmp(event, NSSM_HOOK_EVENT_START) && ! _tcscmp(action, NSSM_HOOK_ACTION_PRE) ? NSSM_HOOK_STATUS_ABORT : NSSM_HOOK_STATUS_NOTFOUND;
}
int hook(hook_thread_t *l, nssm_service_t *s, TCHAR *e, TCHAR *a, unsigned long *c, unsigned long d) { return hook(l,s,e,a,c,d,true); }
int hook(hook_thread_t *l, nssm_service_t *s, TCHAR *e, TCHAR *a, unsigned long *c) { return hook(l,s,e,a,c,NSSM_HOOK_DEADLINE,true); }
void log(unsigned short, unsigned long, ...) { /* No event-log writes in the launch/SCM harness. */ }

static void environment_case(bool created, bool terminated, DWORD waited, int expected) {
  reset(); create_ok = created; terminate_ok = terminated; wait_result = waited;
  TCHAR block[] = _T("A=1\0");
  CHECK(test_environment(block) == expected);
  CHECK(closes == (created ? 2U : 0U));
  CHECK(waits == (created ? 1U : 0U));
  if (created) CHECK(launch_flags & CREATE_SUSPENDED);
  active = false;
}
static void service_case(bool suspended, bool created, bool resume_success, bool abort) {
  reset(); create_ok = created; resume_ok = resume_success; abort_hook = abort; wait_result = WAIT_TIMEOUT;
  nssm_service_t *service = new nssm_service_t();
  _tcscpy_s(service->name, _T("Launch test")); _tcscpy_s(service->exe, _T("fake.exe"));
  service->allow_restart = true; service->no_console = true; service->throttle_delay = 1;
  service->affinity = suspended ? 1 : 0;
  service->kill_process_tree = true;
  InitializeCriticalSection(&service->hook_section);
  int result = start_service(service);
  if (abort || ! created || (suspended && ! resume_success)) {
    CHECK(result != 0);
    CHECK(service->status.dwCurrentState == SERVICE_STOPPED);
    CHECK(! service->process_handle);
  }
  else {
    CHECK(result == 0 && service->status.dwCurrentState == SERVICE_RUNNING);
    CHECK(thread_closed && ! process_closed); // Process ownership stays with service.
    CHECK(resumes == (suspended ? 1U : 0U));
    CHECK(stop_service(service, 0, true, true) == 0);
    CHECK(tree_calls == 1 && process_closed);
  }
  CHECK(output_closes == 1);
  CHECK(closes == (created && ! abort ? 2U : 0U));
  DeleteCriticalSection(&service->hook_section);
  delete service; active = false;
}
}

void test_launch_handles() {
  launch_test::environment_case(true, true, WAIT_OBJECT_0, 0);
  launch_test::environment_case(true, false, WAIT_FAILED, -1);
  launch_test::environment_case(true, true, WAIT_TIMEOUT, -1);
  launch_test::environment_case(true, false, WAIT_OBJECT_0, 0); // Already exited.
  launch_test::environment_case(false, false, WAIT_FAILED, -1);
  launch_test::service_case(false, true, true, false);
  launch_test::service_case(true, true, true, false);
  launch_test::service_case(false, false, true, false);
  launch_test::service_case(false, true, true, true);
  launch_test::service_case(true, true, false, false);
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
