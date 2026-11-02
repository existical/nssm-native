// Included after the production service.cpp and its SCM boundary wrappers.
static int run_control(DWORD action, bool return_status = false) {
  TCHAR name[] = _T("NSSM-Test-Startup");
  TCHAR *args[] = { name };
  int result = control_service(action, 1, args, return_status);
  CHECK(scm::closes == 2);
  return result;
}

void test_control_waits() {
  registry_fixture registry;
  const DWORD actions[] = {SERVICE_CONTROL_STOP, SERVICE_CONTROL_PAUSE, SERVICE_CONTROL_CONTINUE};
  const DWORD pending[] = {SERVICE_STOP_PENDING, SERVICE_PAUSE_PENDING, SERVICE_CONTINUE_PENDING};
  const DWORD done[] = {SERVICE_STOPPED, SERVICE_PAUSED, SERVICE_RUNNING};
  for (unsigned i = 0; i < _countof(actions); i++) {
    scm::reset({{0, pending[i], 1, 3000}, {2000, done[i]}});
    scm::start_error = ERROR_ACCESS_DENIED; // Stale last-error on success.
    CHECK(run_control(actions[i]) == 0);
    CHECK(scm::elapsed >= 2000 && scm::elapsed <= 3000);

    scm::reset({{0, pending[i], 1, 3000}});
    CHECK(run_control(actions[i]) != 0);
    CHECK(scm::elapsed == 3000);

    scm::reset({{0, pending[i], 1, 3000}});
    scm::changing_hint = true;
    CHECK(run_control(actions[i]) != 0);
    CHECK(scm::elapsed == 3000);

    scm::reset({{0, pending[i], 1, 3000}});
    scm::progressing = true;
    CHECK(run_control(actions[i]) != 0);
    CHECK(scm::elapsed == (i ? 60000UL : 180000UL));

    const DWORD initial = i == 2 ? SERVICE_PAUSED : SERVICE_RUNNING;
    scm::reset({{0, initial}, {500, pending[i], 1, 2000}, {1500, done[i]}});
    CHECK(run_control(actions[i], true) == (int) done[i]);
    CHECK(scm::elapsed >= 1500 && scm::elapsed <= 2500);

    scm::reset({{0, pending[i], 1, 3000}, {2000, done[i]}});
    scm::clock_origin = MAXDWORD - 500;
    CHECK(run_control(actions[i]) == 0);
    CHECK(scm::elapsed >= 2000 && scm::elapsed <= 3000);

    scm::reset({{0, pending[i], 1, 3000}});
    scm::clock_origin = MAXDWORD - 500;
    CHECK(run_control(actions[i], true) == 0);
    CHECK(scm::elapsed == 3000);

    SERVICE_STATUS status = {};
    scm::reset({{0, pending[i], 1, 3000}});
    scm::query_failure_at = 1000;
    service_wait_result result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
    CHECK(result.outcome == SERVICE_WAIT_QUERY_ERROR && result.error == ERROR_ACCESS_DENIED);
    CHECK(scm::elapsed >= 1000 && scm::elapsed <= 1300);

    scm::reset({{0, pending[i], 2, 3000}, {500, pending[i], 1, 100000}});
    result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
    CHECK(result.outcome == SERVICE_WAIT_STALLED && result.error == ERROR_TIMEOUT);
    CHECK(scm::elapsed == 3000); // Decreasing checkpoint and hint change do not extend the wait.

    scm::reset({{0, pending[i], 1, 3000}});
    scm::progressing = true;
    result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
    CHECK(result.outcome == SERVICE_WAIT_DEADLINE && result.error == ERROR_TIMEOUT);
    CHECK(scm::elapsed == 60000);

    const DWORD hints[] = {0, 1, MAXDWORD};
    const DWORD waits[] = {10000, 1000, 60000};
    for (unsigned j = 0; j < _countof(hints); j++) {
      scm::reset({{0, pending[i], 1, hints[j]}});
      result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
      CHECK(result.error == ERROR_TIMEOUT && scm::elapsed == waits[j]);
    }

    scm::reset({{0, SERVICE_STOPPED, 0, 0, ERROR_SERVICE_SPECIFIC_ERROR, 42}});
    result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
    CHECK(result.outcome == SERVICE_WAIT_FAILED && result.error == ERROR_SERVICE_SPECIFIC_ERROR);
    CHECK(status.dwServiceSpecificExitCode == 42 && ! scm::elapsed);
    scm::reset({{0, SERVICE_START_PENDING, 0, 0, ERROR_FILE_NOT_FOUND}});
    result = await_service_control_response(actions[i], scm::service_handle, &status, initial, 60000);
    CHECK(result.outcome == SERVICE_WAIT_FAILED && result.error == ERROR_FILE_NOT_FOUND);

    scm::reset({{0, pending[i], 1, 3000}, {2000, done[i]}});
    scm::start_ok = false; scm::start_error = ERROR_IO_PENDING;
    CHECK(run_control(actions[i]) == 0);
    scm::reset({});
    scm::start_ok = false; scm::start_error = ERROR_ACCESS_DENIED;
    CHECK(run_control(actions[i]) != 0 && ! scm::queries);
  }

  scm::reset({{0, SERVICE_STOP_PENDING, 1, 100000}, {90000, SERVICE_STOPPED}});
  CHECK(run_control(SERVICE_CONTROL_STOP) == 0 && scm::elapsed == 90000);
  scm::reset({{0, SERVICE_PAUSED}, {500, SERVICE_STOP_PENDING, 0, 3000}, {1500, SERVICE_STOPPED}});
  CHECK(run_control(SERVICE_CONTROL_STOP) == 0);
  scm::reset({{0, SERVICE_STOP_PENDING, 1, 3000}, {2000, SERVICE_STOPPED}});
  CHECK(run_control(SERVICE_CONTROL_SHUTDOWN) == 0);
  scm::reset({}); scm::start_ok = false; scm::start_error = ERROR_SERVICE_NOT_ACTIVE;
  CHECK(run_control(SERVICE_CONTROL_STOP, true) == SERVICE_STOPPED && ! scm::queries);
  scm::reset({{0, SERVICE_RUNNING}});
  CHECK(run_control(NSSM_SERVICE_CONTROL_ROTATE) == 0 && ! scm::elapsed);
  scm::reset({{0, SERVICE_RUNNING}});
  CHECK(run_control(SERVICE_CONTROL_INTERROGATE, true) == SERVICE_RUNNING);
  scm::reset({}); scm::query_failure_at = 0;
  CHECK(run_control(SERVICE_CONTROL_INTERROGATE) != 0);

  const TCHAR *name = _T("NSSM-Test-Startup");
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 180000);
  HKEY key = open_registry(name, KEY_WRITE); REQUIRE(key);
  const TCHAR exe[] = _T("test.exe");
  REQUIRE(RegSetValueEx(key, NSSM_REG_EXE, 0, REG_SZ, (const BYTE *) exe, sizeof(exe)) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 180000);
  DWORD delay = 240000;
  REQUIRE(RegSetValueEx(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, 0, REG_DWORD, (const BYTE *) &delay, sizeof(delay)) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 376000);
  CHECK(service_control_timeout(SERVICE_CONTROL_PAUSE, name) == 60000);
  CHECK(service_control_timeout(SERVICE_CONTROL_CONTINUE, name) == 60000);
  // A configured stop may legitimately exceed both START and the native policy.
  scm::reset({{0, SERVICE_STOP_PENDING, 1, 3000}, {240000, SERVICE_STOPPED}});
  scm::progressing = true;
  CHECK(run_control(SERVICE_CONTROL_STOP) == 0 && scm::elapsed == 240000);
  scm::reset({{0, SERVICE_STOP_PENDING, 1, 3000}});
  scm::progressing = true;
  CHECK(run_control(SERVICE_CONTROL_STOP) != 0 && scm::elapsed == 376000);
  DWORD skip = NSSM_STOP_METHOD_CONSOLE;
  REQUIRE(RegSetValueEx(key, NSSM_REG_STOP_METHOD_SKIP, 0, REG_DWORD, (const BYTE *) &skip, sizeof(skip)) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 180000);
  REQUIRE(RegDeleteValue(key, NSSM_REG_STOP_METHOD_SKIP) == ERROR_SUCCESS);
  delay = MAXDWORD;
  const TCHAR *delays[] = {NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, NSSM_REG_KILL_THREADS_GRACE_PERIOD};
  for (unsigned i = 0; i < _countof(delays); i++)
    REQUIRE(RegSetValueEx(key, delays[i], 0, REG_DWORD, (const BYTE *) &delay, sizeof(delay)) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 86400000);
  for (unsigned i = 0; i < _countof(delays); i++)
    REQUIRE(RegSetValueEx(key, delays[i], 0, REG_BINARY, (const BYTE *) &delay, sizeof(delay)) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 180000);
  REQUIRE(RegSetValueEx(key, delays[0], 0, REG_DWORD, (const BYTE *) &delay, 1) == ERROR_SUCCESS);
  CHECK(service_control_timeout(SERVICE_CONTROL_STOP, name) == 180000);
  CHECK(RegCloseKey(key) == ERROR_SUCCESS);
}
