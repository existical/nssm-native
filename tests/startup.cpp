#include "test.h"
#include "launch_fixture.h"
#include "install_fixture.h"
#include "gui_fixture.h"
#include "dependency_fixture.h"
namespace wait_test {
static bool fail_hook_lock;
static nssm_service_t *registered;
static SERVICE_STATUS initial_failure;
static BOOL WINAPI initialize_lock(LPCRITICAL_SECTION lock, DWORD spin) {
  return !fail_hook_lock && InitializeCriticalSectionAndSpinCount(lock, spin);
}
static SERVICE_STATUS_HANDLE WINAPI register_handler(LPCTSTR name, LPHANDLER_FUNCTION_EX handler, void *context) {
  if (!fail_hook_lock) return RegisterServiceCtrlHandlerEx(name, handler, context);
  registered = static_cast<nssm_service_t *>(context);
  return reinterpret_cast<SERVICE_STATUS_HANDLE>(context);
}
static bool active;
static std::vector<DWORD> requests, results;
static unsigned statuses;
static DWORD WINAPI wait(HANDLE h, DWORD timeout) {
  if (! active) return launch_test::wait(h, timeout);
  requests.push_back(timeout);
  REQUIRE(requests.size() <= results.size());
  return results[requests.size() - 1];
}
static BOOL WINAPI status(SERVICE_STATUS_HANDLE h, LPSERVICE_STATUS value) {
  if (fail_hook_lock) { initial_failure = *value; return TRUE; }
  if (! active) return launch_test::status(h, value);
  statuses++; return TRUE;
}
}

namespace scm {
struct sample {
  DWORD at, state, checkpoint, hint, error, specific;
};
static std::vector<sample> samples;
static DWORD elapsed, clock_origin, query_failure_at, start_error;
static unsigned queries, closes;
static bool start_ok, progressing, changing_hint;
static bool export_access;
static bool fail_name, test_config;
static std::wstring description;
static DWORD config_error;
static BOOL WINAPI config2(SC_HANDLE h, DWORD level, LPBYTE buffer, DWORD capacity, LPDWORD needed) {
  if (install_test::active && level == SERVICE_CONFIG_DELAYED_AUTO_START_INFO) return install_test::query_delayed(buffer);
  if (install_test::active && level == SERVICE_CONFIG_DESCRIPTION) return install_test::query_description(buffer);
  if (!test_config) return QueryServiceConfig2(h, level, buffer, capacity, needed);
  if (config_error) { SetLastError(config_error); return FALSE; }
  if (level == SERVICE_CONFIG_DELAYED_AUTO_START_INFO) {
    REQUIRE(capacity == sizeof(SERVICE_DELAYED_AUTO_START_INFO));
    reinterpret_cast<SERVICE_DELAYED_AUTO_START_INFO *>(buffer)->fDelayedAutostart = TRUE;
    return TRUE;
  }
  REQUIRE(level == SERVICE_CONFIG_DESCRIPTION && capacity >= sizeof(SERVICE_DESCRIPTION));
  SERVICE_DESCRIPTION *record = reinterpret_cast<SERVICE_DESCRIPTION *>(buffer);
  *needed = sizeof(*record) + static_cast<DWORD>((description.size() + 1) * sizeof(TCHAR));
  if (capacity < *needed) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  record->lpDescription = description.empty() ? NULL : reinterpret_cast<TCHAR *>(record + 1);
  if (record->lpDescription) memcpy(record->lpDescription, description.c_str(), *needed - sizeof(*record));
  return TRUE;
}
static DWORD requested_access;
static DWORD requested_manager_access;
static std::vector<void *> export_objects;
static int manager_token, service_token;
static SC_HANDLE manager_handle = reinterpret_cast<SC_HANDLE>(&manager_token);
static SC_HANDLE service_handle = reinterpret_cast<SC_HANDLE>(&service_token);

struct process_sample { DWORD state, pid, error; };
static std::vector<process_sample> process_samples;
static unsigned process_queries;
static BOOL WINAPI query_process(SC_HANDLE handle, SC_STATUS_TYPE type, LPBYTE buffer, DWORD capacity, LPDWORD needed) {
  REQUIRE(handle == service_handle && type == SC_STATUS_PROCESS_INFO && capacity >= sizeof(SERVICE_STATUS_PROCESS));
  REQUIRE(process_queries < process_samples.size());
  process_sample sample = process_samples[process_queries++];
  if (sample.error) { SetLastError(sample.error); return FALSE; }
  SERVICE_STATUS_PROCESS *status = reinterpret_cast<SERVICE_STATUS_PROCESS *>(buffer);
  *status = {};
  status->dwCurrentState = sample.state;
  status->dwProcessId = sample.pid;
  *needed = sizeof(*status);
  return TRUE;
}

enum class inventory { pages, empty, stalled, denied };
static inventory listing;
static unsigned pages_read;
static BOOL WINAPI enumerate(SC_HANDLE manager, SC_ENUM_TYPE, DWORD, DWORD, LPBYTE data, DWORD capacity,
                             LPDWORD needed, LPDWORD count, LPDWORD resume, LPCTSTR) {
  REQUIRE(manager == manager_handle && data && capacity <= 64 * 1024);
  REQUIRE(++pages_read <= 2);
  *needed = *count = 0;
  if (listing == inventory::empty) return TRUE;
  if (listing == inventory::denied) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  if (listing == inventory::pages) {
    CHECK(*resume == (pages_read == 1 ? 0U : 71U));
    *count = 1;
    reinterpret_cast<ENUM_SERVICE_STATUS_PROCESS *>(data)->lpServiceName =
        pages_read == 1 ? _T("First service") : _T("Second service");
    if (pages_read == 2) return TRUE;
    *resume = 71;
  }
  *needed = 300 * 1024;
  SetLastError(ERROR_MORE_DATA);
  return FALSE;
}

static void reset(std::initializer_list<sample> states) {
  samples = states;
  elapsed = clock_origin = queries = closes = start_error = 0;
  query_failure_at = MAXDWORD;
  start_ok = true;
  progressing = changing_hint = false;
}
static SC_HANDLE WINAPI open_manager(LPCTSTR, LPCTSTR, DWORD access) {
  if (dependency_test::active) return dependency_test::manager(access);
  if (export_access) requested_manager_access = access;
  return install_test::active ? install_test::manager() : manager_handle;
}
static SC_HANDLE WINAPI open_service(SC_HANDLE, LPCTSTR value, DWORD access) {
  if (dependency_test::active) return dependency_test::open(value, access);
  if (export_access) { requested_access = access; SetLastError(ERROR_ACCESS_DENIED); return NULL; }
  return service_handle;
}
static BOOL WINAPI name(SC_HANDLE, LPCTSTR value, LPTSTR output, LPDWORD length) {
  if (dependency_test::active) return dependency_test::name(value, output, length);
  if (fail_name) { output[0] = 0; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
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
static BOOL WINAPI control(SC_HANDLE handle, DWORD, LPSERVICE_STATUS status) {
  if (start_ok || start_error == ERROR_IO_PENDING) query(handle, status);
  SetLastError(start_error);
  return start_ok;
}
static BOOL WINAPI close(SC_HANDLE handle) {
  if (dependency_test::active) return dependency_test::close(handle);
  if (gui_test::active) return gui_test::close(handle);
  if (install_test::active) return install_test::close(handle);
  CHECK(handle == manager_handle || handle == service_handle);
  closes++;
  return TRUE;
}
static ULONGLONG ticks() { return (ULONGLONG)clock_origin + elapsed; }
static void WINAPI sleep(DWORD milliseconds) {
  CHECK(milliseconds > 0 && milliseconds <= 1000);
  elapsed += milliseconds;
}
}

// Replace only operating-system boundaries in this test translation unit.
// The actual control_service() and polling implementation are compiled below.
static bool fail_affinity_allocation = false;
static LPVOID WINAPI service_allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (dependency_test::active) return dependency_test::allocate(heap, flags, bytes);
  if (gui_test::active) return gui_test::allocate(heap, flags, bytes);
  if (fail_affinity_allocation) return NULL;
  void *p = install_test::allocate(heap, flags, bytes);
  if (scm::export_access && p && bytes == sizeof(nssm_service_t)) scm::export_objects.push_back(p);
  return p;
}
static BOOL WINAPI service_release(HANDLE heap, DWORD flags, LPVOID p) {
  if (dependency_test::active) return dependency_test::release(heap, flags, p);
  if (gui_test::active) return gui_test::release(heap, flags, p);
  for (size_t i = 0; i < scm::export_objects.size(); i++) if (scm::export_objects[i] == p) { scm::export_objects.erase(scm::export_objects.begin() + i); break; }
  return install_test::release(heap, flags, p);
}
#undef OpenSCManager
#undef OpenService
#undef GetServiceDisplayName
#undef GetServiceKeyName
#undef StartService
#undef EnumServicesStatusEx
#define EnumServicesStatusEx scm::enumerate
#define OpenSCManager scm::open_manager
#define OpenService scm::open_service
#define GetServiceDisplayName scm::name
#define GetServiceKeyName scm::name
#define StartService scm::start
#define ControlService scm::control
#define QueryServiceStatusEx scm::query_process
#define QueryServiceStatus scm::query
#undef QueryServiceConfig
#define QueryServiceConfig dependency_test::query
#undef QueryServiceConfig2
#define QueryServiceConfig2 scm::config2
#define CloseServiceHandle scm::close
#define monotonic_milliseconds scm::ticks
#define Sleep scm::sleep
#define HeapAlloc service_allocate
#define HeapFree service_release
#undef CreateService
#undef ChangeServiceConfig
#undef ChangeServiceConfig2
#define CreateService install_test::create
#define ChangeServiceConfig install_test::change
#define ChangeServiceConfig2 install_test::change2
#define DeleteService install_test::remove
#define create_parameters install_test::parameters
#undef CreateProcess
#define CreateProcess launch_test::create
#define CloseHandle launch_test::close
#define DuplicateHandle launch_test::duplicate
#define RegisterWaitForSingleObject launch_test::register_wait
#define UnregisterWaitEx launch_test::unregister_wait
#define WaitForMultipleObjects launch_test::wait_multiple
#define WaitForSingleObject wait_test::wait
#define ResumeThread launch_test::resume
#define GetExitCodeProcess launch_test::code
#define GetProcessAffinityMask launch_test::affinity
#define SetProcessAffinityMask launch_test::set_affinity
#define SetServiceStatus wait_test::status
#undef RegisterServiceCtrlHandlerEx
#define RegisterServiceCtrlHandlerEx wait_test::register_handler
#define InitializeCriticalSectionAndSpinCount wait_test::initialize_lock
#define FreeConsole launch_test::free_console
#define get_exit_action launch_test::exit_action
#define get_parameters launch_test::parameters
#define get_output_handles launch_test::output
#define close_output_handles launch_test::close_output
#define cleanup_loggers launch_test::cleanup
#define duplicate_environment_strings launch_test::environment
#define get_process_creation_time launch_test::time
#define get_process_exit_time launch_test::exit_time
#define kill_process launch_test::kill
#define kill_process_tree(...) launch_test::kill_tree(__VA_ARGS__)
#define nssm_hook launch_test::hook
#define log_event launch_test::log
#include "../src/service.cpp"
#undef QueryServiceStatusEx
#undef QueryServiceConfig
#define QueryServiceConfig QueryServiceConfigW
#undef QueryServiceConfig2
#define QueryServiceConfig2 QueryServiceConfig2W
#undef CreateProcess
#undef CloseHandle
#undef DuplicateHandle
#undef RegisterWaitForSingleObject
#undef UnregisterWaitEx
#undef WaitForSingleObject
#undef WaitForMultipleObjects
#undef ResumeThread
#undef GetExitCodeProcess
#undef GetProcessAffinityMask
#undef SetProcessAffinityMask
#undef SetServiceStatus
#undef RegisterServiceCtrlHandlerEx
#undef InitializeCriticalSectionAndSpinCount
#undef FreeConsole
#undef get_parameters
#undef get_exit_action
#undef get_output_handles
#undef close_output_handles
#undef cleanup_loggers
#undef duplicate_environment_strings
#undef get_process_creation_time
#undef get_process_exit_time
#undef kill_process
#undef kill_process_tree
#undef nssm_hook
#undef log_event
#undef HeapAlloc
#undef HeapFree
#undef CreateService
#undef ChangeServiceConfig
#undef ChangeServiceConfig2
#undef DeleteService
#undef create_parameters
#undef OpenSCManager
#undef OpenService
#undef GetServiceDisplayName
#undef GetServiceKeyName
#undef StartService
#undef ControlService
#undef QueryServiceStatus
#undef CloseServiceHandle
#undef monotonic_milliseconds
#undef Sleep

#undef EnumServicesStatusEx

#include "control_waits_cases.h"

void test_export_access() {
  TCHAR key[] = _T("Original key"), output[20] = _T("untouched");
  scm::fail_name = true;
  CHECK(open_service(scm::manager_handle, key, DELETE, key, _countof(key)) == scm::service_handle);
  CHECK(!_tcscmp(key, _T("Original key")));
  CHECK(!open_service(scm::manager_handle, key, DELETE, output, _countof(output)));
  CHECK(GetLastError() == ERROR_ACCESS_DENIED && !_tcscmp(output, _T("untouched")));
  scm::fail_name = false;
  scm::test_config = true;
  scm::description.clear();
  CHECK(!get_service_description(key, scm::service_handle, _countof(output), output) && !output[0]);
  scm::description.assign(4000, L'x');
  std::vector<TCHAR> description(scm::description.size() + 1);
  CHECK(!get_service_description(key, scm::service_handle, (DWORD)description.size(), description.data()));
  CHECK(scm::description == description.data());
  CHECK(get_service_description(key, scm::service_handle, _countof(output), output) && !output[0]);
  QUERY_SERVICE_CONFIG config = {};
  config.dwStartType = SERVICE_AUTO_START;
  DWORD startup;
  CHECK(!get_service_startup(key, scm::service_handle, &config, &startup) && startup == NSSM_STARTUP_DELAYED);
  scm::config_error = ERROR_INVALID_LEVEL;
  CHECK(!get_service_startup(key, scm::service_handle, &config, &startup) && startup == NSSM_STARTUP_AUTOMATIC);
  scm::config_error = ERROR_ACCESS_DENIED;
  CHECK(get_service_startup(key, scm::service_handle, &config, &startup));
  scm::config_error = 0;
  scm::test_config = false;
  TCHAR *all[] = {_T("all")};
  for (scm::inventory listing : {scm::inventory::empty, scm::inventory::pages, scm::inventory::stalled, scm::inventory::denied}) {
    scm::listing = listing;
    scm::pages_read = scm::closes = 0;
    int result = list_nssm_services(1, all);
    CHECK((result == 0) == (listing == scm::inventory::empty || listing == scm::inventory::pages));
    CHECK(scm::pages_read == (listing == scm::inventory::pages ? 2U : 1U));
    CHECK(scm::closes == 1);
  }
  scm::export_access = true;
  TCHAR names[][32] = {NSSM_REG_ENV, NSSM_REG_ENV_EXTRA, NSSM_NATIVE_ENVIRONMENT};
  for (auto &name : names) {
    TCHAR *arguments[] = {_T("set"), _T("NSSM-Test-Access"), name, _T("+A=1"), _T("B=2"), NULL};
    CHECK(pre_edit_service(5, arguments) == 1);
    arguments[3] = _T("-A");
    CHECK(pre_edit_service(5, arguments) == 1);
    arguments[3] = _T(":A=1");
    CHECK(pre_edit_service(5, arguments) == 3); // Whole-block replacement reaches the SCM stub.
  }
  TCHAR *args[] = {_T("dump"), _T("NSSM-Test-Access"), _T("AppParameters"), _T("value"), NULL};
  const TCHAR *verbs[] = {_T("dump"), _T("get"), _T("set"), _T("reset"), _T("edit")};
  for (unsigned i = 0; i < _countof(verbs); i++) {
    args[0] = const_cast<TCHAR *>(verbs[i]);
    CHECK(pre_edit_service(i == 2 ? 4 : (i == 1 || i == 3 ? 3 : 2), args) == 3);
    CHECK(scm::requested_access == (SERVICE_QUERY_CONFIG | (i < 2 ? 0 : SERVICE_CHANGE_CONFIG)));
    CHECK(scm::requested_manager_access == SC_MANAGER_CONNECT);
    CHECK(scm::export_objects.empty());
  }
  fail_affinity_allocation = true;
  CHECK(pre_edit_service(2, args) == 2);
  fail_affinity_allocation = false;
  CHECK(scm::export_objects.empty());
  scm::export_access = false;
}

DWORD WINAPI test_power_worker(void *context) { return dispatch_power_hooks(context); }

void test_handle_waits() {
  using namespace wait_test;
  active = true;
  const DWORD outcomes[] = {WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_FAILED};
  const int expected[] = {0, 1, -1};
  for (unsigned timeout = 0; timeout <= 1; timeout++) {
    for (unsigned i = 0; i < _countof(outcomes); i++) {
      requests.clear(); results.assign(1, outcomes[i]); statuses = 0;
      SERVICE_STATUS state = {}; state.dwCurrentState = SERVICE_START_PENDING; state.dwCheckPoint = 42; state.dwWaitHint = 100;
      CHECK(await_single_handle(NULL, &state, NULL, _T("Wait test"), _T("test"), timeout) == expected[i]);
      CHECK(requests.size() == 1);
      if (requests.size()) CHECK(requests[0] == timeout);
      CHECK(statuses == timeout && state.dwCheckPoint == 42 + timeout && state.dwWaitHint == (timeout ? NSSM_WAITHINT_MARGIN + timeout : 100));
    }
  }
  requests.clear(); results.assign(3, WAIT_TIMEOUT); statuses = 0;
  SERVICE_STATUS state = {}; state.dwCurrentState = SERVICE_STOP_PENDING; state.dwWaitHint = MAXDWORD;
  CHECK(await_single_handle(NULL, &state, NULL, _T("Wait test"), _T("test"), NSSM_SERVICE_STATUS_DEADLINE * 2 + 7) == 1);
  CHECK(requests.size() == 3 && requests[0] == NSSM_SERVICE_STATUS_DEADLINE && requests[1] == NSSM_SERVICE_STATUS_DEADLINE && requests[2] == 7);
  CHECK(statuses == 3 && state.dwWaitHint == 7 + NSSM_WAITHINT_MARGIN);
  for (DWORD stable : {SERVICE_RUNNING, SERVICE_PAUSED, SERVICE_STOPPED}) {
    state = {}; state.dwCurrentState = stable;
    requests.clear(); results.assign(1, WAIT_TIMEOUT); statuses = 0;
    CHECK(await_single_handle(NULL, &state, NULL, _T("Wait test"), _T("test"), 1) == 1);
    CHECK(!statuses && !state.dwCheckPoint && !state.dwWaitHint);
  }
  active = false;
  HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL); REQUIRE(event);
  CHECK(await_single_handle(NULL, NULL, event, _T("Wait test"), _T("test"), 0) == 1);
  CHECK(await_single_handle(NULL, NULL, event, _T("Wait test"), _T("test"), 1) == 1);
  REQUIRE(SetEvent(event));
  CHECK(await_single_handle(NULL, NULL, event, _T("Wait test"), _T("test"), 0) == 0);
  CHECK(await_single_handle(NULL, NULL, event, _T("Wait test"), _T("test"), 1) == 0);
  CloseHandle(event);
  CHECK(await_single_handle(NULL, NULL, NULL, _T("Wait test"), _T("test"), 0) == -1);
  CHECK(await_single_handle(NULL, NULL, NULL, _T("Wait test"), _T("test"), 1) == -1);
}

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

static void check_service_process(std::initializer_list<scm::process_sample> samples, DWORD expected, bool opened = false) {
  scm::process_samples = samples;
  scm::process_queries = 0;
  DWORD before = test_handle_count();
  kill_t root = {};
  CHECK(open_service_process(scm::service_handle, &root) == expected);
  CHECK(scm::process_queries == samples.size());
  CHECK(!!root.process_handle == opened);
  if (root.process_handle) {
    CHECK(root.pid == GetCurrentProcessId());
    CloseHandle(root.process_handle);
  }
  CHECK(test_handle_count() == before);
}

void test_startup() {
  DWORD pid = GetCurrentProcessId();
  check_service_process({{SERVICE_STOPPED, pid}}, ERROR_SUCCESS);
  check_service_process({{SERVICE_START_PENDING, pid}}, ERROR_RETRY);
  check_service_process({{SERVICE_STOP_PENDING, pid}}, ERROR_RETRY);
  check_service_process({{SERVICE_RUNNING, 0}}, ERROR_RETRY);
  check_service_process({{SERVICE_RUNNING, pid}, {SERVICE_RUNNING, pid + 1}}, ERROR_RETRY);
  check_service_process({{SERVICE_RUNNING, pid}, {SERVICE_STOPPED, pid}}, ERROR_RETRY);
  check_service_process({{SERVICE_RUNNING, pid}, {SERVICE_STOP_PENDING, pid}}, ERROR_RETRY);
  check_service_process({{SERVICE_RUNNING, pid}, {0, 0, ERROR_ACCESS_DENIED}}, ERROR_ACCESS_DENIED);
  for (DWORD state : {SERVICE_RUNNING, SERVICE_PAUSED, SERVICE_PAUSE_PENDING, SERVICE_CONTINUE_PENDING})
    check_service_process({{state, pid}, {SERVICE_RUNNING, pid}}, ERROR_SUCCESS, true);

  TCHAR image[16];
  CHECK(service_image_path(_T("nssm.exe"), image, _countof(image)) && !_tcscmp(image, _T("nssm.exe")));
  CHECK(service_image_path(_T("C:\\a b\\x.exe"), image, 15) && !_tcscmp(image, _T("\"C:\\a b\\x.exe\"")));
  CHECK(!service_image_path(_T("C:\\a b\\x.exe"), image, 14));
  std::wstring long_path = L"C:\\a space\\" + std::wstring(300, L'x') + L"\\nssm.exe";
  std::vector<TCHAR> full(long_path.size() + 3);
  CHECK(service_image_path(long_path.c_str(), full.data(), full.size()));
  CHECK(L"\"" + long_path + L"\"" == full.data());
  CHECK(!service_image_path(long_path.c_str(), full.data(), full.size() - 1));
  TCHAR *name[] = {_T("NSSM-Lock-Test")};
  wait_test::fail_hook_lock = true;
  service_main(1, name);
  REQUIRE(wait_test::registered);
  CHECK(!wait_test::registered->hook_section_initialised && wait_test::initial_failure.dwCurrentState == SERVICE_STOPPED);
  CHECK(wait_test::initial_failure.dwWin32ExitCode == ERROR_NOT_ENOUGH_MEMORY);
  cleanup_nssm_service(wait_test::registered);
  wait_test::registered = NULL;
  wait_test::fail_hook_lock = false;
  registry_fixture registry;
  // An early CLI poll sees the SCM's initial two-second hint. The wrapper's
  // first report must advance that checkpoint even if no child exists yet.
  nssm_service_t *wrapper = new nssm_service_t();
  wait_test::active = true;
  report_start_pending(wrapper);
  wait_test::active = false;
  CHECK(wrapper->status.dwCurrentState == SERVICE_START_PENDING && wrapper->status.dwCheckPoint == 1);
  scm::reset({{0, SERVICE_START_PENDING, 0, 2000},
              {100, wrapper->status.dwCurrentState, wrapper->status.dwCheckPoint, wrapper->status.dwWaitHint},
              {6000, SERVICE_RUNNING}});
  CHECK(run_start() == 0);
  delete wrapper;
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
