// Opt-in integration test. Only uniquely created, disabled fixture services are
// touched; neither service is started and no emitted install command is run.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>

static void require(bool ok, const char *what) {
  if (! ok) { fprintf(stderr, "%s (Win32 %lu)\n", what, GetLastError()); throw std::runtime_error(what); }
}
static std::wstring quoted(const std::wstring &s) { return L"\"" + s + L"\""; }
struct service_fixture {
  SC_HANDLE service;
  std::wstring name, path;
  HKEY key;
  service_fixture(SC_HANDLE manager, const std::wstring &exe, const wchar_t *suffix) : service(NULL), key(NULL) {
    wchar_t buffer[128];
    swprintf_s(buffer, L"NSSM-Export-Test-%lu-%llu-%s", GetCurrentProcessId(), GetTickCount64(), suffix);
    name = buffer;
    path = L"SYSTEM\\CurrentControlSet\\Services\\" + name;
    service = CreateServiceW(manager, name.c_str(), name.c_str(), DELETE | WRITE_DAC | SERVICE_QUERY_CONFIG,
      SERVICE_WIN32_OWN_PROCESS, SERVICE_DISABLED, SERVICE_ERROR_NORMAL, quoted(exe).c_str(), NULL, NULL, NULL, NULL, NULL);
    require(service != NULL, "Create isolated disabled service");
  }
  ~service_fixture() {
    if (key) RegCloseKey(key);
    if (service) {
      if (! DeleteService(service)) { fprintf(stderr, "Could not delete fixture %ls: %lu\n", name.c_str(), GetLastError()); cleanup_failed = true; }
      CloseServiceHandle(service);
    }
  }
  static bool cleanup_failed;
  void configure(const std::wstring &exe) {
    require(RegCreateKeyExW(HKEY_LOCAL_MACHINE, (path + L"\\Parameters").c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS, "Open fixture Parameters");
    set(key, L"Application", exe, REG_EXPAND_SZ);
    set(key, L"AppParameters", L"round trip", REG_EXPAND_SZ);
    HKEY actions;
    require(RegCreateKeyExW(key, L"AppExit", 0, NULL, 0, KEY_SET_VALUE, NULL, &actions, NULL) == ERROR_SUCCESS, "Create fixture AppExit");
    set(actions, L"", L"Ignore"); set(actions, L"0", L"Restart");
    set(actions, L"42", L"Exit"); set(actions, L"4294967295", L"Suicide");
    RegCloseKey(actions);
  }
  static void set(HKEY key, const wchar_t *name, const std::wstring &value, DWORD type = REG_SZ) {
    require(RegSetValueExW(key, name, 0, type, reinterpret_cast<const BYTE *>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t))) == ERROR_SUCCESS, "Write fixture value");
  }
  void query_only() {
    PSECURITY_DESCRIPTOR descriptor = NULL;
    // Deny changes even to the administrator token; allow only configuration
    // queries to new callers. This fixture retains its already-granted DELETE handle.
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;DC;;;WD)(A;;CC;;;WD)", SDDL_REVISION_1, &descriptor, NULL) != FALSE, "Build fixture DACL");
    BOOL result = SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor); require(result != FALSE, "Restrict fixture service to query access");
  }
};
bool service_fixture::cleanup_failed = false;

struct output_file {
  wchar_t path[MAX_PATH];
  output_file() {
    wchar_t directory[MAX_PATH];
    require(GetTempPathW(MAX_PATH, directory) != 0 && GetTempFileNameW(directory, L"nse", 0, path) != 0, "Create output fixture");
  }
  ~output_file() { if (! DeleteFileW(path)) service_fixture::cleanup_failed = true; }
};
struct result { DWORD code; std::wstring output; };
static result run(const std::wstring &exe, const std::wstring &arguments) {
  output_file file;
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  HANDLE output = CreateFileW(file.path, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, 0, NULL);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
  if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    require(false, "Open child standard handles");
  }
  STARTUPINFOW si = {}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = si.hStdError = output; si.hStdInput = input;
  PROCESS_INFORMATION process = {};
  std::wstring command = quoted(exe) + L" " + arguments;
  BOOL created = CreateProcessW(exe.c_str(), &command[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &process);
  CloseHandle(output); CloseHandle(input); require(created != FALSE, "Launch isolated NSSM command");
  CloseHandle(process.hThread);
  DWORD waited = WaitForSingleObject(process.hProcess, 10000);
  if (waited != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 9); WaitForSingleObject(process.hProcess, 5000); }
  result value = {};
  BOOL got_code = GetExitCodeProcess(process.hProcess, &value.code);
  CloseHandle(process.hProcess);
  require(waited == WAIT_OBJECT_0 && got_code, "NSSM command completed within deadline");
  HANDLE read = CreateFileW(file.path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  require(read != INVALID_HANDLE_VALUE, "Read command output");
  char bytes[65536]; DWORD count = 0;
  BOOL ok = ReadFile(read, bytes, sizeof(bytes), &count, NULL); CloseHandle(read);
  require(ok && count < sizeof(bytes), "Bounded command output");
  if (count) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, count, NULL, 0);
    require(length > 0, "UTF-8 command output"); value.output.resize(length);
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, count, &value.output[0], length) == length, "Decode output");
  }
  return value;
}

int wmain(int argc, wchar_t **argv) {
  SC_HANDLE manager = NULL;
  try {
    require(argc == 2, "Supply the NSSM executable path");
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    require(manager != NULL, "Opt-in SCM test requires an elevated disposable Windows runner");
    {
      service_fixture source(manager, argv[1], L"source"), imported(manager, argv[1], L"import");
      source.configure(argv[1]); imported.configure(argv[1]); source.query_only();
      // Remove destination rules so replay must reconstruct all of them.
      require(RegDeleteKeyW(imported.key, L"AppExit") == ERROR_SUCCESS, "Clear destination rules");
      result exported = run(argv[1], L"dump " + quoted(source.name) + L" " + quoted(imported.name));
      if (exported.code) fwprintf(stderr, L"%ls\n", exported.output.c_str());
      require(exported.code == 0, "Query-only service export succeeds");
      const wchar_t *rules[] = {L"Default Ignore", L"0 Restart", L"42 Exit", L"4294967295 Suicide"};
      for (const wchar_t *rule : rules) require(exported.output.find(L" AppExit " + std::wstring(rule)) != std::wstring::npos, "Export contains every exit rule");
      require(run(argv[1], L"set " + quoted(source.name) + L" AppParameters denied").code != 0, "Setter still requires change access");
      result original = run(argv[1], L"get " + quoted(source.name) + L" AppParameters");
      require(original.code == 0 && original.output.find(L"round trip") != std::wstring::npos, "Denied setter did not modify the source");
      size_t offset = 0; unsigned replayed = 0;
      while (offset < exported.output.size()) {
        size_t end = exported.output.find(L'\n', offset);
        if (end == std::wstring::npos) end = exported.output.size();
        std::wstring line = exported.output.substr(offset, end - offset);
        if (! line.empty() && line.back() == L'\r') line.pop_back();
        if (line.find(L" AppExit ") != std::wstring::npos) {
          // Use only the arguments after the emitted executable; never run install.
          size_t command = line.find(L" set "); require(command != std::wstring::npos, "Expected set command");
          require(line.find(imported.name) != std::wstring::npos, "Replay targets only the destination fixture");
          require(run(argv[1], line.substr(command + 1)).code == 0, "Replay exported AppExit setting"); replayed++;
        }
        offset = end + 1;
      }
      require(replayed == 4, "Replay all four rules");
      result again = run(argv[1], L"dump " + quoted(imported.name));
      require(again.code == 0, "Re-export imported settings");
      for (const wchar_t *rule : rules) require(again.output.find(L" AppExit " + std::wstring(rule)) != std::wstring::npos, "Re-export preserves the rule");
    }
    CloseServiceHandle(manager); manager = NULL;
    require(! service_fixture::cleanup_failed, "Fixture cleanup succeeded");
    puts("Query-only SCM export/import passed; both disabled fixtures were deleted without starting.");
    return 0;
  }
  catch (const std::exception &error) {
    if (manager) CloseServiceHandle(manager);
    fprintf(stderr, "SCM export test failed: %s\n", error.what()); return 1;
  }
}
