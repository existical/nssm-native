// Real NSSM wrapper acceptance, only on an explicitly opted-in disposable host.
namespace lifecycle_live {
static std::wstring file(const std::wstring &dir, const wchar_t *name) { return dir + L"\\" + name; }
static void write(const std::wstring &path, const std::string &text) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
  require(h != INVALID_HANDLE_VALUE, "Create lifecycle fixture file");
  DWORD written = 0; BOOL ok = WriteFile(h, text.data(), (DWORD) text.size(), &written, NULL); CloseHandle(h);
  require(ok && written == text.size(), "Write lifecycle fixture file");
}
static std::string read(const std::wstring &path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return "";
  char bytes[65536]; DWORD count = 0; BOOL ok = ReadFile(h, bytes, sizeof(bytes), &count, NULL); CloseHandle(h);
  require(ok && count < sizeof(bytes), "Read bounded lifecycle output");
  return std::string(bytes, count);
}
static void output(DWORD stream, const char *text) {
  DWORD written; WriteFile(GetStdHandle(stream), text, (DWORD) strlen(text), &written, NULL);
}
static BOOL WINAPI ignore_console(DWORD) { return TRUE; }
static int child(const std::wstring &dir) {
  SetConsoleCtrlHandler(ignore_console, TRUE);
  write(file(dir, L"child.pid"), std::to_string(GetCurrentProcessId()));
  output(STD_ERROR_HANDLE, "descendant-ready\n");
  ULONGLONG started = GetTickCount64();
  while (GetTickCount64() - started < 45000 && GetFileAttributesW(file(dir, L"release").c_str()) == INVALID_FILE_ATTRIBUTES) Sleep(50);
  return 0;
}
static int app(const std::wstring &dir) {
  SetConsoleCtrlHandler(ignore_console, TRUE);
  DeleteFileW(file(dir, L"stop").c_str());
  char input[32] = {}; DWORD count;
  if (! ReadFile(GetStdHandle(STD_INPUT_HANDLE), input, sizeof(input)-1, &count, NULL) || std::string(input, count) != "stdin fixture\n") return 11;
  wchar_t value[32];
  if (! GetEnvironmentVariableW(L"NSSM_TEST_AFTER", value, _countof(value)) || wcscmp(value, L"present")) return 12;
  output(STD_OUTPUT_HANDLE, "stdin-and-environment-ok\n");
  wchar_t self[32768]; if (! GetModuleFileNameW(NULL, self, _countof(self))) return 13;
  std::wstring command = quoted(self) + L" --wrapped-child " + quoted(dir);
  STARTUPINFOW si = {}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION process = {};
  if (! CreateProcessW(self, &command[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &process)) return 14;
  CloseHandle(process.hProcess); CloseHandle(process.hThread);
  write(file(dir, L"app.pid"), std::to_string(GetCurrentProcessId()));
  ULONGLONG started = GetTickCount64();
  while (GetTickCount64() - started < 45000) {
    if (GetFileAttributesW(file(dir, L"crash").c_str()) != INVALID_FILE_ATTRIBUTES) { DeleteFileW(file(dir, L"crash").c_str()); return 17; }
    if (GetFileAttributesW(file(dir, L"stop").c_str()) != INVALID_FILE_ATTRIBUTES || GetFileAttributesW(file(dir, L"release").c_str()) != INVALID_FILE_ATTRIBUTES) break;
    Sleep(50);
  }
  output(STD_OUTPUT_HANDLE, "stdout-final-tail\n");
  output(STD_ERROR_HANDLE, "stderr-final-tail\n");
  return 0;
}
static int hook(const std::wstring &dir, const std::wstring &event, const std::wstring &argument) {
  if (argument != L"final argument") return 21;
  if (event == L"Stop") { output(STD_OUTPUT_HANDLE, "hook-stop-quoted-ok\n"); write(file(dir, L"stop"), "stop"); }
  else if (event == L"Start") output(STD_OUTPUT_HANDLE, "hook-start-quoted-ok\n");
  else output(STD_OUTPUT_HANDLE, "hook-exit-quoted-ok\n");
  return 0;
}
struct directory {
  std::wstring path;
  directory() {
    wchar_t temp[MAX_PATH], unique[MAX_PATH];
    require(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"nsl", 0, unique), "Reserve lifecycle directory");
    require(DeleteFileW(unique) && CreateDirectoryW(unique, NULL), "Create lifecycle directory"); path = unique;
  }
  ~directory() {
    WIN32_FIND_DATAW data; HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
      do { if (! (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ! DeleteFileW(file(path, data.cFileName).c_str())) service_fixture::cleanup_failed = true; } while (FindNextFileW(find, &data));
      FindClose(find);
    }
    if (! RemoveDirectoryW(path.c_str())) service_fixture::cleanup_failed = true;
  }
};
struct observer {
  HANDLE handle;
  DWORD pid;
  observer() : handle(NULL), pid(0) {}
  ~observer() { if (handle) CloseHandle(handle); }
  void attach(const std::wstring &path, DWORD previous = 0) {
    ULONGLONG started = GetTickCount64();
    do {
      std::string value = read(path); DWORD candidate = strtoul(value.c_str(), NULL, 10);
      if (candidate && candidate != previous) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, candidate);
        if (h) { handle = h; pid = candidate; return; }
      }
      Sleep(50);
    } while (GetTickCount64() - started < 15000);
    require(false, "Observe owned application/descendant readiness");
  }
  void exited(const char *phase) {
    DWORD result = WaitForSingleObject(handle, 5000);
    if (result != WAIT_OBJECT_0) fprintf(stderr, "Still live: %s, PID %lu, wait %lu\n", phase, pid, result);
    require(result == WAIT_OBJECT_0, "Owned process exited during NSSM cleanup");
  }
};
struct fixture : service_fixture {
  std::wstring dir;
  fixture(SC_HANDLE manager, const std::wstring &nssm, const std::wstring &directory, const wchar_t *prefix = L"NSSM-Lifecycle-Test") :
    service_fixture(manager, nssm, L"wrapped", prefix, SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_QUERY_STATUS), dir(directory) {}
  ~fixture() {
    // Every cleanup operation is restricted to this created service and directory.
    try { write(file(dir, L"release"), "release"); } catch (...) { cleanup_failed = true; }
    SERVICE_STATUS status = {}; ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (! control_live::wait_state(service, SERVICE_STOPPED, 35000)) cleanup_failed = true;
    if (std::uncaught_exception()) {
      try {
        fprintf(stderr, "Fixture stdout:\n%s\nFixture stderr:\n%s\n", read(file(dir, L"stdout.log")).c_str(), read(file(dir, L"stderr.log")).c_str());
      } catch (...) { cleanup_failed = true; }
    }
  }
};
static void command(const std::wstring &nssm, const std::wstring &args) {
  result r = run(nssm, args);
  if (r.code) fwprintf(stderr, L"%ls\n", r.output.c_str());
  require(! r.code, "Lifecycle CLI command succeeds");
}
static int test(const std::wstring &nssm) {
  SC_HANDLE manager = NULL;
  try {
    {
      directory dir;
      wchar_t self[32768]; require(GetModuleFileNameW(NULL, self, _countof(self)) != 0, "Resolve lifecycle helper");
      std::wstring helper = file(dir.path, L"helper with spaces.exe");
      require(CopyFileW(self, helper.c_str(), TRUE) != FALSE, "Copy helper to a path with spaces");
      write(file(dir.path, L"stdin.txt"), "stdin fixture\n");
      manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
      require(manager != NULL, "Lifecycle test requires disposable elevated host");
      {
        fixture owned(manager, nssm, dir.path);
        // Bootstrap Parameters/Application so NSSM recognises its own service
        // before using the public setting commands. The service stays disabled.
        owned.configure(helper);
        const std::wstring set = L"set " + quoted(owned.name) + L" ";
        command(nssm, set + L"Application " + quoted(helper));
        command(nssm, set + L"AppDirectory " + quoted(dir.path));
        command(nssm, set + L"AppParameters --wrapped-app " + quoted(dir.path));
        command(nssm, set + L"AppStdin " + quoted(file(dir.path, L"stdin.txt")));
        command(nssm, set + L"AppStdout " + quoted(file(dir.path, L"stdout.log")));
        command(nssm, set + L"AppStderr " + quoted(file(dir.path, L"stderr.log")));
        command(nssm, set + L"AppTimestampLog 1");
        command(nssm, set + L"AppRedirectHook 1");
        command(nssm, set + L"AppExit Default Restart");
        command(nssm, set + L"AppRestartDelay 100");
        command(nssm, set + L"AppEnvironmentExtra NSSM_TEST_EMPTY= NSSM_TEST_AFTER=present");
        const wchar_t *events[] = {L"Start", L"Stop", L"Exit"};
        for (const wchar_t *event : events) {
          // Exercise the complete stored hook command and its final quoted argument.
          std::wstring hook_command = quoted(helper) + L" --wrapped-hook " + quoted(dir.path) + L" " + event + L" \"final argument\"";
          // Set the raw command through registry, avoiding an extra CLI quoting layer.
          HKEY key; std::wstring path = owned.path + L"\\Parameters\\AppEvents\\" + event;
          require(RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS, "Create owned hook key");
          service_fixture::set(key, std::wstring(event) == L"Stop" ? L"Pre" : L"Post", hook_command, REG_EXPAND_SZ); RegCloseKey(key);
        }
        require(ChangeServiceConfigW(owned.service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE, "Enable owned wrapper");
        command(nssm, L"start " + quoted(owned.name));
        observer first, first_child; first.attach(file(dir.path, L"app.pid")); first_child.attach(file(dir.path, L"child.pid"));
        write(file(dir.path, L"crash"), "crash");
        observer restarted, restarted_child; restarted.attach(file(dir.path, L"app.pid"), first.pid); restarted_child.attach(file(dir.path, L"child.pid"), first_child.pid);
        first.exited("crashed root"); first_child.exited("crashed root descendant");
        command(nssm, L"stop " + quoted(owned.name)); restarted.exited("restart root"); restarted_child.exited("restart descendant");
        command(nssm, L"start " + quoted(owned.name));
        observer second, second_child; second.attach(file(dir.path, L"app.pid"), restarted.pid); second_child.attach(file(dir.path, L"child.pid"), restarted_child.pid);
        command(nssm, L"stop " + quoted(owned.name)); second.exited("second root"); second_child.exited("second descendant");
        std::string out = read(file(dir.path, L"stdout.log")), err = read(file(dir.path, L"stderr.log"));
        const char *tokens[] = {"stdin-and-environment-ok", "stdout-final-tail", "hook-start-quoted-ok", "hook-stop-quoted-ok", "hook-exit-quoted-ok"};
        for (const char *token : tokens) require(out.find(token) != std::string::npos, "Captured application and hook output");
        require(err.find("stderr-final-tail") != std::string::npos && err.find("descendant-ready") != std::string::npos, "Captured stderr and descendant output");
        require(control_live::wait_state(owned.service, SERVICE_STOPPED, 3000), "Wrapper stopped");
      }
      CloseServiceHandle(manager); manager = NULL;
    }
    require(! service_fixture::cleanup_failed, "Lifecycle fixtures cleaned up");
    puts("Wrapped application start/crash/restart/stop/start/stop, stdin/environment, quoted hooks, log tails and descendant cleanup passed."); return 0;
  }
  catch (const std::exception &error) { if (manager) CloseServiceHandle(manager); fprintf(stderr, "Lifecycle test failed: %s\n", error.what()); return 1; }
}
}
