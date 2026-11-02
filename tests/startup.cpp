#include "test.h"
#include "launch_fixture.h"

namespace scm {
struct sample {
  DWORD at, state, checkpoint, hint, error, specific;
};
static std::vector<sample> samples;
static DWORD elapsed, clock_origin, query_failure_at, start_error;
static unsigned queries, closes;
static bool start_ok, progressing, changing_hint;
static int manager_token, service_token;
static SC_HANDLE manager_handle = reinterpret_cast<SC_HANDLE>(&manager_token);
static SC_HANDLE service_handle = reinterpret_cast<SC_HANDLE>(&service_token);

static void reset(std::initializer_list<sample> states) {
  samples = states;
  elapsed = clock_origin = queries = closes = start_error = 0;
  query_failure_at = MAXDWORD;
  start_ok = true;
  progressing = changing_hint = false;
}
static SC_HANDLE WINAPI open_manager(LPCTSTR, LPCTSTR, DWORD) { return manager_handle; }
static SC_HANDLE WINAPI open_service(SC_HANDLE, LPCTSTR, DWORD) { return service_handle; }
static BOOL WINAPI name(SC_HANDLE, LPCTSTR, LPTSTR output, LPDWORD length) {
  return _tcscpy_s(output, *length, _T("NSSM-Test-Startup")) == 0;
}
static BOOL WINAPI start(SC_HANDLE handle, DWORD, const TCHAR **) {
  CHECK(handle == service_handle);
  // A successful Win32 call does not promise to clear a stale last error.
  SetLastError(start_error);
  return start_ok;
}
static BOOL WINAPI query(SC_HANDLE handle, LPSERVICE_STATUS status) {
  CHECK(handle == service_handle);
  if (++queries > 2000) {
    CHECK(false); // Detect an unbounded poll without hanging the test process.
    SetLastError(ERROR_TIMEOUT);
    return FALSE;
  }
  if (elapsed >= query_failure_at) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  REQUIRE(! samples.empty());
  sample current = samples[0];
  for (size_t i = 1; i < samples.size(); i++) {
    if (samples[i].at > elapsed) break;
    current = samples[i];
  }
  ZeroMemory(status, sizeof(*status));
  status->dwCurrentState = current.state;
  status->dwCheckPoint = progressing ? elapsed / 500 + 1 : current.checkpoint;
  status->dwWaitHint = changing_hint ? current.hint + elapsed : current.hint;
  status->dwWin32ExitCode = current.error;
  status->dwServiceSpecificExitCode = current.specific;
  return TRUE;
}
static BOOL WINAPI close(SC_HANDLE handle) {
  CHECK(handle == manager_handle || handle == service_handle);
  closes++;
  return TRUE;
}
static DWORD WINAPI ticks() { return clock_origin + elapsed; }
static void WINAPI sleep(DWORD milliseconds) {
  CHECK(milliseconds > 0 && milliseconds <= 1000);
  elapsed += milliseconds;
}
}

// Replace only operating-system boundaries in this test translation unit.
// The actual control_service() and polling implementation are compiled below.
static bool fail_affinity_allocation = false;
static LPVOID WINAPI service_allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (fail_affinity_allocation) return NULL;
  return HeapAlloc(heap, flags, bytes);
}
#undef OpenSCManager
#undef OpenService
#undef GetServiceDisplayName
#undef GetServiceKeyName
#undef StartService
#define OpenSCManager scm::open_manager
#define OpenService scm::open_service
#define GetServiceDisplayName scm::name
#define GetServiceKeyName scm::name
#define StartService scm::start
#define QueryServiceStatus scm::query
#define CloseServiceHandle scm::close
#define GetTickCount scm::ticks
#define Sleep scm::sleep
#define HeapAlloc service_allocate
#undef CreateProcess
#define CreateProcess launch_test::create
#define CloseHandle launch_test::close
#define WaitForSingleObject launch_test::wait
#define ResumeThread launch_test::resume
#define GetExitCodeProcess launch_test::code
#define GetProcessAffinityMask launch_test::affinity
#define SetProcessAffinityMask launch_test::set_affinity
#define SetServiceStatus launch_test::status
#define FreeConsole launch_test::free_console
#define get_parameters launch_test::parameters
#define get_output_handles launch_test::output
#define close_output_handles launch_test::close_output
#define cleanup_loggers launch_test::cleanup
#define duplicate_environment_strings launch_test::environment
#define get_process_creation_time launch_test::time
#define get_process_exit_time launch_test::exit_time
#define kill_process launch_test::kill
#define nssm_hook launch_test::hook
#define log_event launch_test::log
#include "../src/service.cpp"
#undef CreateProcess
#undef CloseHandle
#undef WaitForSingleObject
#undef ResumeThread
#undef GetExitCodeProcess
#undef GetProcessAffinityMask
#undef SetProcessAffinityMask
#undef SetServiceStatus
#undef FreeConsole
#undef get_parameters
#undef get_output_handles
#undef close_output_handles
#undef cleanup_loggers
#undef duplicate_environment_strings
#undef get_process_creation_time
#undef get_process_exit_time
#undef kill_process
#undef nssm_hook
#undef log_event
#undef HeapAlloc
#undef OpenSCManager
#undef OpenService
#undef GetServiceDisplayName
#undef GetServiceKeyName
#undef StartService
#undef QueryServiceStatus
#undef CloseServiceHandle
#undef GetTickCount
#undef Sleep

void test_affinity_allocation() {
  TCHAR *output = reinterpret_cast<TCHAR *>(1);
  fail_affinity_allocation = true;
  CHECK(affinity_mask_to_string(1, &output) != 0);
  CHECK(output == NULL);
  fail_affinity_allocation = false;
}

static int run_start() {
  TCHAR name[] = _T("NSSM-Test-Startup");
  TCHAR *args[] = { name };
  int result = control_service(NSSM_SERVICE_CONTROL_START, 1, args);
  CHECK(scm::closes == 2);
  return result;
}

void test_startup() {
  registry_fixture registry;
  // Missing parameters (a native service), then all relevant AppThrottle values.
  const DWORD throttles[] = { MAXDWORD, 0, 1, 1500, 120000 };
  for (unsigned i = 0; i < _countof(throttles); i++) {
    if (throttles[i] != MAXDWORD) {
      HKEY key = open_registry(_T("NSSM-Test-Startup"), KEY_WRITE);
      REQUIRE(key);
      CHECK(RegSetValueEx(key, NSSM_REG_THROTTLE, 0, REG_DWORD,
        reinterpret_cast<const BYTE *>(&throttles[i]), sizeof(DWORD)) == ERROR_SUCCESS);
      RegCloseKey(key);
    }
    scm::reset({ { 0, SERVICE_START_PENDING, 1, 10000 }, { 5000, SERVICE_RUNNING } });
    CHECK(run_start() == 0);
    CHECK(scm::elapsed >= 5000 && scm::elapsed <= 6000);
    CHECK(scm::queries > 1);
  }

  scm::reset({ { 0, SERVICE_RUNNING } });
  scm::start_error = ERROR_ACCESS_DENIED; // Stale last-error on success.
  CHECK(run_start() == 0);
  CHECK(! scm::elapsed);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 5000 }, { 2000, SERVICE_RUNNING } });
  scm::start_ok = false;
  scm::start_error = ERROR_IO_PENDING;
  CHECK(run_start() == 0);
  CHECK(scm::elapsed >= 2000);

  const DWORD start_errors[] = { ERROR_ACCESS_DENIED, ERROR_SERVICE_DISABLED, ERROR_SERVICE_ALREADY_RUNNING };
  for (unsigned i = 0; i < _countof(start_errors); i++) {
    scm::reset({});
    scm::start_ok = false;
    scm::start_error = start_errors[i];
    CHECK(run_start() != 0);
    CHECK(! scm::queries && ! scm::elapsed);
  }

  scm::reset({ { 0, SERVICE_STOPPED, 0, 0, ERROR_FILE_NOT_FOUND } });
  CHECK(run_start() != 0);
  CHECK(scm::queries == 1 && ! scm::elapsed);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 10000 },
    { 2000, SERVICE_STOPPED, 0, 0, ERROR_SERVICE_SPECIFIC_ERROR, 5 } });
  CHECK(run_start() != 0);
  CHECK(scm::elapsed >= 2000 && scm::elapsed <= 3000);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 10000 }, { 1000, SERVICE_PAUSED } });
  CHECK(run_start() != 0);
  CHECK(scm::elapsed <= 2000);

  scm::reset({});
  scm::query_failure_at = 0;
  CHECK(run_start() != 0);
  CHECK(scm::queries == 1 && ! scm::elapsed);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 10000 } });
  scm::query_failure_at = 2000;
  CHECK(run_start() != 0);
  CHECK(scm::elapsed <= 3000);

  // No checkpoint progress: the wait hint expires, even if the hint changes.
  for (int hint_changes = 0; hint_changes < 2; hint_changes++) {
    scm::reset({ { 0, SERVICE_START_PENDING, 1, 3000 } });
    scm::changing_hint = !!hint_changes;
    CHECK(run_start() != 0);
    CHECK(scm::elapsed == 3000);
  }
  scm::reset({ { 0, SERVICE_START_PENDING, 1, 0 } });
  CHECK(run_start() != 0);
  CHECK(scm::elapsed > 0 && scm::elapsed <= 60000);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, MAXDWORD } });
  CHECK(run_start() != 0);
  CHECK(scm::elapsed == 60000);

  // Checkpoint progress extends the progress deadline, but never the total cap.
  scm::reset({ { 0, SERVICE_START_PENDING, 1, 2000 }, { 7000, SERVICE_RUNNING } });
  scm::progressing = true;
  CHECK(run_start() == 0);
  CHECK(scm::elapsed >= 7000 && scm::elapsed <= 8000);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 2000 } });
  scm::progressing = true;
  CHECK(run_start() != 0);
  CHECK(scm::elapsed == 60000);

  scm::reset({ { 0, SERVICE_START_PENDING, 1, 5000 }, { 2000, SERVICE_RUNNING } });
  scm::clock_origin = MAXDWORD - 500;
  CHECK(run_start() == 0);
  CHECK(scm::elapsed >= 2000 && scm::elapsed <= 3000);

  // Check the error codes supplied to the CLI diagnostics as well as exit status.
  SERVICE_STATUS status = {};
  scm::reset({ { 0, SERVICE_STOPPED, 0, 0, ERROR_FILE_NOT_FOUND } });
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_FILE_NOT_FOUND);
  scm::reset({ { 0, SERVICE_STOPPED, 0, 0, ERROR_SERVICE_SPECIFIC_ERROR, 5 } });
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_SERVICE_SPECIFIC_ERROR);
  CHECK(status.dwServiceSpecificExitCode == 5);
  scm::reset({ { 0, SERVICE_STOPPED } });
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_SERVICE_NOT_ACTIVE);
  scm::reset({ { 0, SERVICE_START_PENDING, 1, 10000 } });
  scm::query_failure_at = 1000;
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_ACCESS_DENIED);
  scm::reset({ { 0, SERVICE_START_PENDING, 1, 1 } });
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_TIMEOUT);
  CHECK(scm::elapsed == 1000);
  scm::reset({ { 0, SERVICE_START_PENDING, 5, 3000 }, { 1000, SERVICE_START_PENDING, 4, 3000 } });
  CHECK(await_service_start(scm::service_handle, &status) == ERROR_TIMEOUT);
  CHECK(scm::elapsed == 3000);
}
