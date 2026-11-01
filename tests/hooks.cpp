#include "test.h"

class hook_executable {
  std::basic_string<TCHAR> directory;
public:
  std::basic_string<TCHAR> path;
  hook_executable() {
    TCHAR current[PATH_LENGTH];
    DWORD length = GetModuleFileName(NULL, current, _countof(current));
    REQUIRE(length && length < _countof(current));
    directory = current;
    directory.erase(directory.find_last_of(_T("\\/")) + 1);
    TCHAR suffix[64];
    _stprintf_s(suffix, _T("Hook test %lu %lu"), GetCurrentProcessId(), GetTickCount());
    directory += suffix;
    REQUIRE(CreateDirectory(directory.c_str(), NULL));
    path = directory + _T("\\hook probe.exe");
    if (! CopyFile(current, path.c_str(), TRUE)) {
      RemoveDirectory(directory.c_str());
      throw std::runtime_error("Could not copy hook executable");
    }
  }
  ~hook_executable() {
    CHECK(DeleteFile(path.c_str()));
    CHECK(RemoveDirectory(directory.c_str()));
  }
};

void test_hook_commands() {
  registry_fixture registry;
  const TCHAR *service = _T("NSSM-Test-Hooks");
  TCHAR output[1024];
  CHECK(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
  CHECK(! output[0]);
  const TCHAR *commands[] = {
    _T("\"C:\\Program Files\\QR Scanner\\hook.exe\""),
    _T("\"C:\\Program Files\\QR Scanner\\hook.exe\" --config \"C:\\Data Files\\settings.json\""),
    _T("\"C:\\Program Files\\QR Scanner\\hook.exe\" \"\""),
    _T("C:\\Tools\\hook.exe --config \"final argument\""),
    _T("\"C:\\Program Files\\QR Scanner\\hook.exe\" unquoted")
  };
  for (unsigned i = 0; i < _countof(commands); i++) {
    std::basic_string<TCHAR> command(commands[i]);
    REQUIRE(set_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, &command[0]) == 0);
    CHECK(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
    CHECK(! _tcscmp(output, commands[i]));
    int argc;
    TCHAR **argv = CommandLineToArgvW(output, &argc);
    int expected_argc;
    TCHAR **expected = CommandLineToArgvW(commands[i], &expected_argc);
    REQUIRE(argv && expected);
    CHECK(argc == expected_argc);
    for (int j = 0; j < argc && j < expected_argc; j++) CHECK(! _tcscmp(argv[j], expected[j]));
    LocalFree(argv);
    LocalFree(expected);
  }
  REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_HOOK_DIR"), _T("C:\\Program Files\\QR Scanner")));
  TCHAR expanded[] = _T("\"%NSSM_TEST_HOOK_DIR%\\hook.exe\" \"final argument\"");
  REQUIRE(set_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, expanded) == 0);
  CHECK(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
  CHECK(! _tcscmp(output, _T("\"C:\\Program Files\\QR Scanner\\hook.exe\" \"final argument\"")));
  // REG_SZ must remain literal, while REG_EXPAND_SZ above still expands.
  HKEY key = open_registry(service, _T("AppEvents\\Start"), KEY_WRITE);
  REQUIRE(key);
  CHECK(set_string(key, _T("Pre"), expanded, false) == 0);
  RegCloseKey(key);
  CHECK(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
  CHECK(! _tcscmp(output, expanded));

  // Exercise Windows executable selection and real argv parsing too.
  hook_executable executable;
  std::basic_string<TCHAR> command = _T("\"") + executable.path + _T("\" --hook-child \"final quoted argument\"");
  REQUIRE(set_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, &command[0]) == 0);
  REQUIRE(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
  STARTUPINFO startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  REQUIRE(CreateProcess(NULL, output, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process));
  DWORD waited = WaitForSingleObject(process.hProcess, 5000);
  CHECK(waited == WAIT_OBJECT_0);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
  }
  DWORD exit_code;
  CHECK(GetExitCodeProcess(process.hProcess, &exit_code));
  CHECK(exit_code == 0);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  TCHAR removed[] = _T("");
  CHECK(set_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, removed) == 0);
  CHECK(get_hook(service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, output, sizeof(output)) == 0);
  CHECK(! output[0]);
}
