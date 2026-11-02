// Included by export_live.cpp; this mode alone starts its own native fixtures.
namespace control_live {
static SERVICE_STATUS_HANDLE status_handle;
static HANDLE request_event;
static volatile LONG requested;
static std::wstring mode;
static void publish(DWORD state, DWORD checkpoint = 0, DWORD hint = 0, DWORD error = 0) {
  SERVICE_STATUS status = {};
  status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  status.dwCurrentState = state;
  status.dwControlsAccepted = state == SERVICE_RUNNING || state == SERVICE_PAUSED ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE : 0;
  status.dwCheckPoint = checkpoint; status.dwWaitHint = hint;
  status.dwWin32ExitCode = error;
  if (error == ERROR_SERVICE_SPECIFIC_ERROR) status.dwServiceSpecificExitCode = 42;
  SetServiceStatus(status_handle, &status);
}
static void WINAPI handler(DWORD control) {
  if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_PAUSE && control != SERVICE_CONTROL_CONTINUE) return;
  InterlockedExchange(&requested, control);
  SetEvent(request_event);
}
static void WINAPI service_main(DWORD argc, LPWSTR *argv) {
  if (! argc) return;
  request_event = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (! request_event) return;
  status_handle = RegisterServiceCtrlHandlerW(argv[0], handler);
  if (! status_handle) { CloseHandle(request_event); return; }
  publish(SERVICE_RUNNING);
  ULONGLONG started = GetTickCount64();
  while (GetTickCount64() - started < 30000) { // Independent fixture watchdog.
    if (WaitForSingleObject(request_event, 100) != WAIT_OBJECT_0) continue;
    DWORD control = InterlockedExchange(&requested, 0);
    DWORD pending = control == SERVICE_CONTROL_STOP ? SERVICE_STOP_PENDING :
      control == SERVICE_CONTROL_PAUSE ? SERVICE_PAUSE_PENDING : SERVICE_CONTINUE_PENDING;
    DWORD done = control == SERVICE_CONTROL_STOP ? SERVICE_STOPPED :
      control == SERVICE_CONTROL_PAUSE ? SERVICE_PAUSED : SERVICE_RUNNING;
    publish(pending, 1, mode == L"stall" ? 1000 : 3000);
    if (mode == L"stall" && control == SERVICE_CONTROL_PAUSE) Sleep(2500);
    else {
      Sleep(350); publish(pending, 2, 3000); Sleep(350);
    }
    if (done == SERVICE_STOPPED) {
      publish(done, 0, 0, mode == L"failure" ? ERROR_SERVICE_SPECIFIC_ERROR : 0);
      CloseHandle(request_event); return;
    }
    publish(done);
  }
  publish(SERVICE_STOPPED);
  CloseHandle(request_event);
}
static int host(const wchar_t *name, const wchar_t *test_mode) {
  mode = test_mode;
  SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(name), service_main}, {NULL, NULL}};
  return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}
static bool wait_state(SC_HANDLE service, DWORD wanted, DWORD timeout) {
  ULONGLONG started = GetTickCount64();
  do {
    SERVICE_STATUS status = {};
    if (! QueryServiceStatus(service, &status)) return false;
    if (status.dwCurrentState == wanted) return true;
    Sleep(50);
  } while (GetTickCount64() - started < timeout);
  return false;
}
struct fixture : service_fixture {
  fixture(SC_HANDLE manager, const std::wstring &exe, const wchar_t *test_mode) :
    service_fixture(manager, exe, test_mode, L"NSSM-Control-Test",
      SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS) {
    std::wstring command = quoted(exe) + L" --control-service " + quoted(name) + L" " + test_mode;
    require(ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE,
      command.c_str(), NULL, NULL, NULL, NULL, NULL, NULL) != FALSE, "Configure owned control fixture");
  }
  ~fixture() {
    SERVICE_STATUS status = {};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (! wait_state(service, SERVICE_STOPPED, 35000)) cleanup_failed = true;
    // Base destructor deletes only this fixture; no process is force-terminated.
  }
};
static int test(const std::wstring &nssm) {
  SC_HANDLE manager = NULL;
  try {
    wchar_t self[32768]; DWORD length = GetModuleFileNameW(NULL, self, _countof(self));
    require(length && length < _countof(self), "Resolve fixture executable");
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    require(manager != NULL, "Control test requires an elevated disposable runner");
    const wchar_t *modes[] = {L"normal", L"stall", L"failure"};
    for (const wchar_t *test_mode : modes) {
      fixture owned(manager, self, test_mode);
      require(run(nssm, L"start " + quoted(owned.name)).code == 0, "Start owned control fixture");
      require(wait_state(owned.service, SERVICE_RUNNING, 3000), "Fixture running");
      result paused = run(nssm, L"pause " + quoted(owned.name));
      if (std::wstring(test_mode) == L"stall") {
        require(paused.code != 0 && paused.output.find(L"no progress") != std::wstring::npos, "Stalled pause reports failure and reason");
      }
      else require(paused.code == 0, "Pause completes");
      require(wait_state(owned.service, SERVICE_PAUSED, 4000), "Fixture eventually paused");
      require(run(nssm, L"continue " + quoted(owned.name)).code == 0, "Continue completes");
      require(wait_state(owned.service, SERVICE_RUNNING, 3000), "Fixture running after continue");
      result stopped = run(nssm, L"stop " + quoted(owned.name));
      if (std::wstring(test_mode) == L"failure")
        require(stopped.code != 0 && stopped.output.find(L"service-specific exit code 42") != std::wstring::npos, "Preserve shutdown failure code");
      else require(stopped.code == 0, "Stop completes");
      require(wait_state(owned.service, SERVICE_STOPPED, 3000), "Fixture stopped");
      require(run(nssm, L"status " + quoted(owned.name)).output.find(L"SERVICE_STOPPED") != std::wstring::npos, "CLI observes stopped state");
    }
    CloseServiceHandle(manager); manager = NULL;
    require(! service_fixture::cleanup_failed, "Control fixtures cleaned up");
    puts("Live start/pause/continue/stop, stalled progress and shutdown failure passed; all owned fixtures deleted.");
    return 0;
  }
  catch (const std::exception &error) {
    if (manager) CloseServiceHandle(manager);
    fprintf(stderr, "SCM control test failed: %s\n", error.what()); return 1;
  }
}
}
