// Opt-in integration tests. Configuration modes keep fixtures disabled.
// Only the controls mode starts its uniquely created native test services.
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
  service_fixture(SC_HANDLE manager, const std::wstring &exe, const wchar_t *suffix,
    const wchar_t *prefix = L"NSSM-Export-Test", DWORD extra_access = 0) : service(NULL), key(NULL) {
    wchar_t buffer[128];
    swprintf_s(buffer, L"%s-%lu-%llu-%s", prefix, GetCurrentProcessId(), GetTickCount64(), suffix);
    name = buffer;
    path = L"SYSTEM\\CurrentControlSet\\Services\\" + name;
    service = CreateServiceW(manager, name.c_str(), name.c_str(), DELETE | WRITE_DAC | SERVICE_QUERY_CONFIG | extra_access,
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
static result run(const std::wstring &exe, const std::wstring &arguments, bool read_only_output = false, DWORD timeout = 10000) {
  output_file file;
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  HANDLE output = CreateFileW(file.path, read_only_output ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ, &security, OPEN_EXISTING, 0, NULL);
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
  DWORD waited = WaitForSingleObject(process.hProcess, timeout);
  if (waited != WAIT_OBJECT_0) {
    fwprintf(stderr, L"Fixture command '%ls' failed to finish (wait result %lu)\n", arguments.c_str(), waited);
    TerminateProcess(process.hProcess, 9);
    WaitForSingleObject(process.hProcess, 5000);
  }
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

#include "dependencies_live_cases.h"
#include "controls_live_cases.h"
#include "lifecycle_live_cases.h"
#include "upgrade_live_cases.h"

int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && ! wcscmp(argv[1], L"--upgrade-stop")) return upgrade_live::stop(argv[2]);
  if (argc == 5 && ! wcscmp(argv[2], L"upgrade")) return upgrade_live::test(argv[1], argv[3], argv[4]);
  if (argc == 3 && ! wcscmp(argv[1], L"--wrapped-app")) return lifecycle_live::app(argv[2]);
  if (argc == 3 && ! wcscmp(argv[1], L"--wrapped-child")) return lifecycle_live::child(argv[2]);
  if (argc == 5 && ! wcscmp(argv[1], L"--wrapped-hook")) return lifecycle_live::hook(argv[2], argv[3], argv[4]);
  if (argc == 3 && ! wcscmp(argv[2], L"lifecycle")) return lifecycle_live::test(argv[1]);
  if (argc == 4 && ! wcscmp(argv[1], L"--control-service")) return control_live::host(argv[2], argv[3]);
  if (argc == 3 && ! wcscmp(argv[2], L"controls")) return control_live::test(argv[1]);
  if (argc == 3 && ! wcscmp(argv[2], L"dependencies")) return dependencies_live(argv[1]);
  SC_HANDLE manager = NULL;
  try {
    require(argc == 2, "Supply the NSSM executable path");
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    require(manager != NULL, "Opt-in SCM test requires an elevated disposable Windows runner");
    {
      service_fixture source(manager, argv[1], L"source"), imported(manager, argv[1], L"import");
      result missing = run(argv[1], L"dump " + quoted(imported.name));
      require(missing.code && missing.output.find(L" install ") == std::wstring::npos, "Reject incomplete wrapper backup before emitting install");
      require(run(argv[1], L"set " + quoted(imported.name) + L" Application " + quoted(argv[1])).code == 0, "Repair missing Parameters on an owned wrapper");
      require(run(argv[1], L"reset " + quoted(imported.name) + L" Application").code != 0, "Reject removal of required Application");
      require(run(argv[1], L"get " + quoted(imported.name) + L" Application").output.find(argv[1]) != std::wstring::npos, "Preserve repaired application");
      source.configure(argv[1]); imported.configure(argv[1]);
      require(run(argv[1], L"version", true).code != 0, "Version detects an output error");
      require(run(argv[1], L"get " + quoted(imported.name) + L" Application", true).code != 0, "Get detects an output error");
      require(run(argv[1], L"status " + quoted(source.name), true).code != 0, "Status detects an output error");
      require(run(argv[1], L"statuscode " + quoted(source.name), true).code == SERVICE_STOPPED, "Statuscode preserves the service state");
      require(run(argv[1], L"set " + quoted(source.name) + L" AppEnvironment KEEP=one").code == 0, "Set baseline environment");
      require(run(argv[1], L"set " + quoted(source.name) + L" AppEnvironment KEY=two MISSING_EQUALS").code != 0, "Reject a malformed replacement");
      require(run(argv[1], L"get " + quoted(source.name) + L" AppEnvironment KEEP").output.find(L"one") != std::wstring::npos, "Invalid replacement preserves the original environment");
      require(run(argv[1], L"set " + quoted(source.name) + L" DisplayName " + quoted(source.name + L" custom")).code == 0, "Give source a custom display name");
      wchar_t native_image[MAX_PATH];
      require(GetModuleFileNameW(NULL, native_image, _countof(native_image)) > 0, "Locate native fixture image");
      service_fixture native(manager, native_image, L"native");
      native.configure(argv[1]);
      require(run(argv[1], L"dump " + quoted(native.name)).code != 0, "Native Application value does not identify a wrapper");
      require(run(argv[1], L"get " + quoted(native.name) + L" ImagePath").code == 0, "Native SCM settings remain readable");
      require(run(argv[1], L"status " + quoted(source.name + L" custom")).code == 0, "Named status resolves the display name");
      require(run(argv[1], L"dump " + quoted(source.name), true).code != 0, "Unwritable output fails the whole dump");
      const std::wstring raw_log = L"%SystemRoot%\\literal.log";
      service_fixture::set(source.key, L"AppStdout", raw_log, REG_SZ);
      service_fixture::set(source.key, L"AppStderr", raw_log, REG_EXPAND_SZ);
      service_fixture::set(source.key, L"Application", argv[1], REG_SZ);
      const std::wstring unicode = L"\u96ea\u03a9";
      service_fixture::set(source.key, L"AppParameters", unicode, REG_SZ);
      const wchar_t empty_environment[2] = {};
      require(RegSetValueExW(source.key, L"AppEnvironment", 0, REG_MULTI_SZ, (const BYTE *)empty_environment, sizeof(empty_environment)) == ERROR_SUCCESS, "Write empty replacement environment");
      source.query_only();
      require(run(argv[1], L"status " + quoted(source.name)).code != 0, "Named status requires service query-status access");
      // Remove destination rules so replay must reconstruct all of them.
      require(RegDeleteKeyW(imported.key, L"AppExit") == ERROR_SUCCESS, "Clear destination rules");
      result exported = run(argv[1], L"dump " + quoted(source.name) + L" " + quoted(imported.name));
      if (exported.code) fwprintf(stderr, L"%ls\n", exported.output.c_str());
      require(exported.code == 0, "Query-only service export succeeds");
      require(exported.output.find(unicode) != std::wstring::npos, "Headless UTF-8 dump retains Unicode");
      require(exported.output.find(L" AppEnvironment :") != std::wstring::npos, "Export retains an empty replacement environment");
      require(exported.output.find(L" DisplayName ") == std::wstring::npos, "Cloned backup leaves the target display name available");
      {
        output_file base;
        std::wstring spaced = std::wstring(base.path) + L" nssm.exe";
        require(CopyFileW(argv[1], spaced.c_str(), TRUE) != FALSE, "Copy reviewed executable to a spaced path");
        struct remove_copy {
          const std::wstring &path;
          ~remove_copy() { if (! DeleteFileW(path.c_str())) service_fixture::cleanup_failed = true; }
        } cleanup = {spaced};
        service_fixture relocated(manager, spaced, L"renamed");
        relocated.configure(argv[1]);
        require(run(argv[1], L"dump " + quoted(relocated.name)).code == 0, "Recognize a separately installed wrapper by version resource");
        require(run(argv[1], L"list all", true).code != 0, "Report a failed list output write");
        result listed = run(argv[1], L"list");
        require(listed.code == 0 && listed.output.find(relocated.name) != std::wstring::npos &&
                listed.output.find(native.name) == std::wstring::npos, "List identifies the relocated wrapper and excludes the native service");
        result copy_dump = run(spaced, L"dump " + quoted(source.name));
        size_t end = copy_dump.output.find(L" install ");
        require(copy_dump.code == 0 && end != std::wstring::npos, "Read executable prefix from spaced-path dump");
        std::wstring prefix = copy_dump.output.substr(0, end);
        require(prefix == quoted(spaced), "Quote command executable exactly once");
        wchar_t shell[MAX_PATH];
        require(GetSystemDirectoryW(shell, _countof(shell)) > 0, "Locate command processor");
        require(run(std::wstring(shell) + L"\\cmd.exe", L"/d /s /c \"" + prefix + L" version\"").code == 0, "Run emitted command prefix");
      }
      result original_dump = run(argv[1], L"dump " + quoted(source.name));
      require(original_dump.code == 0 && original_dump.output.find(L" DisplayName ") != std::wstring::npos, "Same-name backup preserves the custom display name");
      require(run(argv[1], L"set " + quoted(imported.name) + L" Environment MODE=production").code == 0, "Set native environment");
      require(run(argv[1], L"set " + quoted(imported.name) + L" Environment +ADDED=value").code == 0, "Append native environment");
      require(run(argv[1], L"set " + quoted(imported.name) + L" Environment -MODE=prod").code == 0, "Remove conditional nonmatch");
      require(run(argv[1], L"get " + quoted(imported.name) + L" Environment MODE").output.find(L"production") != std::wstring::npos, "Keep full environment value on prefix mismatch");
      const wchar_t *rules[] = {L"Default Ignore", L"0 Restart", L"42 Exit", L"4294967295 Suicide"};
      for (const wchar_t *rule : rules) require(exported.output.find(L" AppExit " + std::wstring(rule)) != std::wstring::npos, "Export contains every exit rule");
      require(run(argv[1], L"set " + quoted(source.name) + L" AppParameters denied").code != 0, "Setter still requires change access");
      result original = run(argv[1], L"get " + quoted(source.name) + L" AppParameters");
      require(original.code == 0 && original.output.find(unicode) != std::wstring::npos, "Denied setter preserves the original Unicode arguments");
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
        if (line.find(L" REG_SZ:") != std::wstring::npos || line.find(L" AppStderr ") != std::wstring::npos || line.find(L" AppEnvironment :") != std::wstring::npos) {
          size_t command = line.find(L" set ");
          require(command != std::wstring::npos && line.find(imported.name) != std::wstring::npos, "Typed setter targets the destination");
          wchar_t system[MAX_PATH];
          require(GetSystemDirectoryW(system, _countof(system)) > 0, "Locate cmd for typed replay");
          require(!run(std::wstring(system) + L"\\cmd.exe", L"/d /v:off /s /c \"" + line + L"\"").code, "Replay exported registry string through cmd");
        }
        offset = end + 1;
      }
      require(replayed == 4, "Replay all four rules");
      result arguments = run(argv[1], L"get " + quoted(imported.name) + L" AppParameters");
      require(arguments.code == 0 && arguments.output.find(unicode) != std::wstring::npos, "Replay preserves Unicode arguments");
      wchar_t restored[2] = {1,1}; DWORD size = sizeof(restored), kind = REG_NONE;
      require(RegQueryValueExW(imported.key, L"AppEnvironment", NULL, &kind, (BYTE *)restored, &size) == ERROR_SUCCESS &&
              kind == REG_MULTI_SZ && size == sizeof(restored) && !restored[0] && !restored[1], "Restore a present empty environment");
      for (auto value : {L"Application", L"AppStdout"}) {
        DWORD type = 0;
        require(RegQueryValueExW(imported.key, value, NULL, &type, NULL, NULL) == ERROR_SUCCESS && type == REG_SZ, "Restored literal registry type");
      }
      for (auto value : {L"AppStdout", L"AppStderr"}) {
        wchar_t data[MAX_PATH]; DWORD length = sizeof(data), type = 0;
        require(RegQueryValueExW(imported.key, value, NULL, &type, (BYTE *)data, &length) == ERROR_SUCCESS &&
          raw_log == data && type == (wcscmp(value, L"AppStdout") ? REG_EXPAND_SZ : REG_SZ), "Preserve raw data and registry type through the command interpreter");
      }
      result again = run(argv[1], L"dump " + quoted(imported.name));
      require(again.code == 0, "Re-export imported settings");
      for (const wchar_t *rule : rules) require(again.output.find(L" AppExit " + std::wstring(rule)) != std::wstring::npos, "Re-export preserves the rule");
    }
    CloseServiceHandle(manager); manager = NULL;
    require(! service_fixture::cleanup_failed, "Fixture cleanup succeeded");
    puts("Query-only SCM export/import, typed replay and wrapper classification passed.");
    return 0;
  }
  catch (const std::exception &error) {
    if (manager) CloseServiceHandle(manager);
    fprintf(stderr, "SCM export test failed: %s\n", error.what()); return 1;
  }
}
