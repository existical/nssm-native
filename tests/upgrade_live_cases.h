// A stopped-service executable replacement rehearsal, only on disposable CI.
#include <algorithm>
namespace upgrade_live {
using lifecycle_live::file;
using lifecycle_live::read;
using lifecycle_live::write;
using lifecycle_live::command;
// One CreateProcess/CRT argument containing a complete stored command line.
// The outer CLI quoting must preserve the inner application's literal quotes.
static std::wstring argument(const std::wstring &value) {
  std::wstring result = L"\""; size_t slashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') { slashes++; continue; }
    result.append(c == L'"' ? slashes*2+1 : slashes, L'\\');
    result += c; slashes = 0;
  }
  result.append(slashes*2, L'\\'); result += L'"'; return result;
}
struct registry_key {
  HKEY key;
  registry_key(HKEY parent, const std::wstring &path) : key(NULL) {
    require(RegOpenKeyExW(parent, path.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS, "Open owned configuration snapshot");
  }
  ~registry_key() { if (key) RegCloseKey(key); }
};
static void append(std::string &to, const std::wstring &value) {
  to.append(reinterpret_cast<const char *>(value.c_str()), (value.size()+1)*sizeof(wchar_t));
}
static void registry_snapshot(HKEY key, const std::wstring &path, std::vector<std::string> &entries, unsigned depth = 0) {
  require(depth < 16 && entries.size() < 4096, "Bound configuration snapshot");
  std::string marker("key"); append(marker, path); entries.push_back(marker);
  for (DWORD i = 0; ; i++) {
    wchar_t name[16384]; BYTE data[65536]; DWORD length = _countof(name), size = sizeof(data), type;
    LONG result = RegEnumValueW(key, i, name, &length, NULL, &type, data, &size);
    if (result == ERROR_NO_MORE_ITEMS) break;
    require(result == ERROR_SUCCESS && entries.size() < 4096, "Read complete configuration value");
    std::string entry("value"); append(entry, path); append(entry, std::wstring(name, length));
    entry.append(reinterpret_cast<const char *>(&type), sizeof(type));
    entry.append(reinterpret_cast<const char *>(data), size); entries.push_back(entry);
  }
  for (DWORD i = 0; ; i++) {
    wchar_t name[256]; DWORD length = _countof(name);
    LONG result = RegEnumKeyExW(key, i, name, &length, NULL, NULL, NULL, NULL);
    if (result == ERROR_NO_MORE_ITEMS) break;
    require(result == ERROR_SUCCESS, "Read complete configuration subkey");
    registry_key child(key, std::wstring(name, length));
    registry_snapshot(child.key, path + L"\\" + std::wstring(name, length), entries, depth+1);
  }
}
static std::vector<std::string> snapshot(service_fixture &owned) {
  registry_key parameters(HKEY_LOCAL_MACHINE, owned.path + L"\\Parameters");
  std::vector<std::string> entries; registry_snapshot(parameters.key, L"Parameters", entries);
  DWORD needed = 0;
  require(! QueryServiceConfigW(owned.service, NULL, 0, &needed) && GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed <= 65536, "Size owned SCM configuration");
  std::vector<BYTE> buffer(needed);
  QUERY_SERVICE_CONFIGW *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
  require(QueryServiceConfigW(owned.service, config, needed, &needed) != FALSE, "Read owned SCM configuration");
  std::string entry = std::to_string(config->dwServiceType) + ":" + std::to_string(config->dwStartType) + ":" + std::to_string(config->dwErrorControl);
  append(entry, config->lpBinaryPathName); append(entry, config->lpServiceStartName); append(entry, config->lpDisplayName);
  append(entry, config->lpLoadOrderGroup ? config->lpLoadOrderGroup : L"");
  for (const wchar_t *p = config->lpDependencies; p && *p; p += wcslen(p)+1) append(entry, p);
  entries.push_back(entry); std::sort(entries.begin(), entries.end()); return entries;
}
struct process {
  HANDLE handle;
  process() : handle(NULL) {}
  ~process() { if (handle) CloseHandle(handle); }
  void attach(DWORD pid) {
    require(pid != 0 && ! handle, "Pin an owned process once");
    handle = OpenProcess(SYNCHRONIZE, FALSE, pid); require(handle != NULL, "Pin owned process lifetime");
  }
  void exited() { require(WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0, "Owned process exited before replacement"); }
};
static int stop(const std::wstring &dir) {
  DWORD pid = strtoul(read(file(dir, L"ready.txt")).c_str(), NULL, 10);
  if (! pid) return 1;
  HANDLE app = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (! app) return GetLastError() == ERROR_INVALID_PARAMETER ? 0 : 2;
  write(file(dir, L"stop"), "stop");
  // Let the worker flush its logs before NSSM sends console control events.
  // This is an observer handle only; never terminate a process from a PID file.
  DWORD waited = WaitForSingleObject(app, 10000); CloseHandle(app);
  return waited == WAIT_OBJECT_0 ? 0 : 3;
}
static DWORD ready(const std::wstring &dir, unsigned generation) {
  ULONGLONG start = GetTickCount64();
  do {
    unsigned long pid = 0; unsigned observed = 0;
    if (sscanf_s(read(file(dir, L"ready.txt")).c_str(), "%lu %u", &pid, &observed) == 2 && observed == generation && pid) return pid;
    Sleep(50);
  } while (GetTickCount64() - start < 15000);
  require(false, "PowerShell worker ready with preserved application state"); return 0;
}
static void heartbeat(const std::wstring &dir, unsigned generation) {
  ULONGLONG start = GetTickCount64(); unsigned first = 0; bool have_first = false;
  do {
    unsigned observed = 0, tick = 0;
    if (sscanf_s(read(file(dir, L"heartbeat.txt")).c_str(), "%u %u", &observed, &tick) == 2 && observed == generation) {
      if (have_first && tick > first) return;
      first = tick; have_first = true;
    }
    Sleep(100);
  } while (GetTickCount64() - start < 5000);
  require(false, "Application makes progress after readiness");
}
static void phase(const std::wstring &active, lifecycle_live::fixture &owned,
    const std::vector<std::string> &configuration, unsigned generation, bool restart) {
  printf("Upgrade phase: generation %u, restart %u\n", generation, restart ? 1 : 0);
  command(active, L"start " + quoted(owned.name));
  require(control_live::wait_state(owned.service, SERVICE_RUNNING, 10000), "Wrapper reached RUNNING");
  SERVICE_STATUS_PROCESS status = {}; DWORD bytes;
  require(QueryServiceStatusEx(owned.service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status), sizeof(status), &bytes) != FALSE, "Read owned wrapper process");
  process wrapper, app; wrapper.attach(status.dwProcessId); app.attach(ready(owned.dir, generation));
  heartbeat(owned.dir, generation);
  require(snapshot(owned) == configuration, "Configuration unchanged after startup");
  process restarted;
  if (restart) {
    write(file(owned.dir, L"crash"), "crash");
    restarted.attach(ready(owned.dir, ++generation)); app.exited(); heartbeat(owned.dir, generation);
  }
  command(active, L"stop " + quoted(owned.name));
  app.exited(); if (restarted.handle) restarted.exited(); wrapper.exited();
  require(control_live::wait_state(owned.service, SERVICE_STOPPED, 3000), "Wrapper stopped before replacement");
  require(read(file(owned.dir, L"state.txt")) == std::to_string(generation), "Application state preserved after stop");
  require(snapshot(owned) == configuration, "Registry and SCM configuration unchanged after stop");
  require(read(file(owned.dir, L"stdout.log")).find("worker-final-" + std::to_string(generation)) != std::string::npos, "Graceful worker stdout tail retained");
  require(read(file(owned.dir, L"stderr.log")).find("worker-stderr-" + std::to_string(generation)) != std::string::npos, "Graceful worker stderr tail retained");
}
static void replace(const std::wstring &source, const std::wstring &active, const std::wstring &version) {
  // The caller has observed STOPPED and both retained process handles exiting.
  require(CopyFileW(source.c_str(), active.c_str(), FALSE) != FALSE, "Replace only owned stopped wrapper executable");
  result identity = run(active, L"version");
  require(! identity.code && identity.output == version, "Installed wrapper version matches selected binary");
  fwprintf(stdout, L"Installed comparison executable: %ls", identity.output.c_str());
}
static int test(const std::wstring &candidate, const std::wstring &legacy, const std::wstring &worker) {
  SC_HANDLE manager = NULL;
  try {
    result old_version = run(legacy, L"version"), new_version = run(candidate, L"version");
    require(! old_version.code && ! new_version.code && old_version.output != new_version.output, "Distinct legacy and candidate executable identities");
    require(old_version.output.find(L"2.24-103-gdee49fc") != std::wstring::npos, "Pinned legacy comparison version");
    {
      lifecycle_live::directory dir;
      wchar_t self[32768], system[MAX_PATH];
      require(GetModuleFileNameW(NULL, self, _countof(self)) && GetSystemDirectoryW(system, _countof(system)), "Resolve test helper and PowerShell");
      std::wstring powershell = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
      std::wstring helper = file(dir.path, L"helper with spaces.exe"), script = file(dir.path, L"worker with spaces.ps1");
      std::wstring active = file(dir.path, L"nssm with spaces.exe");
      require(CopyFileW(self, helper.c_str(), TRUE) && CopyFileW(worker.c_str(), script.c_str(), TRUE), "Copy owned helper and representative worker");
      replace(legacy, active, old_version.output); write(file(dir.path, L"input.txt"), "upgrade input\n");
      std::wstring marker = L"cwd-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".marker";
      write(file(dir.path, marker.c_str()), "owned working directory");
      manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
      require(manager != NULL, "Upgrade rehearsal requires an elevated disposable host");
      {
        lifecycle_live::fixture owned(manager, active, dir.path, L"NSSM-Upgrade-Test");
        // Configure with legacy CLI. Do not migrate settings or reinstall when
        // replacing the executable; compare every Parameters byte/type later.
        require(RegCreateKeyExW(HKEY_LOCAL_MACHINE, (owned.path + L"\\Parameters").c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &owned.key, NULL) == ERROR_SUCCESS, "Create owned Parameters");
        service_fixture::set(owned.key, L"Application", powershell, REG_EXPAND_SZ);
        const std::wstring set = L"set " + quoted(owned.name) + L" ";
        command(active, set + L"AppDirectory " + quoted(dir.path));
        std::wstring parameters = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quoted(script) + L" -Directory " + quoted(dir.path) + L" -WorkingDirectoryMarker " + marker;
        command(active, set + L"AppParameters " + argument(parameters));
        result stored = run(active, L"get " + quoted(owned.name) + L" AppParameters");
        require(! stored.code && stored.output.find(parameters) != std::wstring::npos, "Legacy CLI preserves the complete application command line");
        command(active, set + L"AppStdout " + quoted(file(dir.path, L"stdout.log")));
        command(active, set + L"AppStderr " + quoted(file(dir.path, L"stderr.log")));
        command(active, set + L"AppEnvironmentExtra " + quoted(L"NSSM_UPGRADE_VALUE=persistent value"));
        command(active, set + L"AppExit Default Restart");
        command(active, set + L"AppRestartDelay 100");
        HKEY hooks;
        require(RegCreateKeyExW(owned.key, L"AppEvents\\Stop", 0, NULL, 0, KEY_SET_VALUE, NULL, &hooks, NULL) == ERROR_SUCCESS, "Create owned graceful-stop hook");
        // An unquoted final marker avoids the legacy hook quote-stripping bug.
        std::wstring hook = quoted(helper) + L" --upgrade-stop " + quoted(dir.path) + L" marker";
        service_fixture::set(hooks, L"Pre", hook, REG_EXPAND_SZ); RegCloseKey(hooks);
        require(ChangeServiceConfigW(owned.service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE, "Enable only the owned rehearsal service");
        std::vector<std::string> configuration = snapshot(owned);
        result backup = run(candidate, L"dump " + quoted(owned.name));
        require(! backup.code && backup.output.find(L"AppParameters") != std::wstring::npos, "Export existing configuration before upgrade");
        phase(active, owned, configuration, 1, false);
        replace(candidate, active, new_version.output); phase(active, owned, configuration, 2, true);
        replace(legacy, active, old_version.output); phase(active, owned, configuration, 4, false);
      }
      CloseServiceHandle(manager); manager = NULL;
    }
    require(! service_fixture::cleanup_failed, "Upgrade fixtures cleaned up");
    puts("Legacy -> candidate -> legacy rollback passed: same service and executable path, unchanged SCM/Parameters, persistent PowerShell state, file input/environment, restart and log tails.");
    return 0;
  }
  catch (const std::exception &error) { if (manager) CloseServiceHandle(manager); fprintf(stderr, "Upgrade rehearsal failed: %s\n", error.what()); return 1; }
}
}
