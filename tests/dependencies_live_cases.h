// Uses the export integration runner's bounded CLI helper and disabled fixtures.
// Existing service-group metadata is read only; no group membership is modified.
static std::vector<std::wstring> live_dependency_groups() {
  HKEY key = NULL;
  require(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS, "Read existing group names");
  struct key_owner { HKEY value; ~key_owner() { RegCloseKey(value); } } owner = {key};
  DWORD type, bytes = 0;
  require(RegQueryValueExW(key, L"List", NULL, &type, NULL, &bytes) == ERROR_SUCCESS && type == REG_MULTI_SZ && bytes >= 4 && bytes % sizeof(wchar_t) == 0, "Group-list metadata");
  std::vector<wchar_t> data(bytes / sizeof(wchar_t));
  require(RegQueryValueExW(key, L"List", NULL, &type, reinterpret_cast<BYTE *>(&data[0]), &bytes) == ERROR_SUCCESS && type == REG_MULTI_SZ, "Read group list");
  std::vector<std::wstring> groups;
  size_t position = 0;
  while (position < data.size() && data[position] && groups.size() < 2) {
    size_t end = position; while (end < data.size() && data[end]) end++;
    require(end < data.size(), "Bounded group name");
    if (end - position < 254) groups.push_back(std::wstring(&data[position], end - position));
    position = end + 1;
  }
  require(groups.size() == 2, "Two existing service groups available"); return groups;
}
static void expect_dependencies(SC_HANDLE service, const std::vector<std::wstring> &services, const std::vector<std::wstring> &groups) {
  DWORD bytes = 0;
  QueryServiceConfigW(service, NULL, 0, &bytes);
  require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes >= sizeof(QUERY_SERVICE_CONFIGW), "Dependency query size");
  std::vector<BYTE> buffer(bytes); auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(&buffer[0]);
  require(QueryServiceConfigW(service, config, bytes, &bytes) != FALSE, "Query changed dependencies");
  std::vector<std::wstring> actual_services, actual_groups;
  if (config->lpDependencies) for (const wchar_t *entry = config->lpDependencies; *entry; entry += wcslen(entry) + 1) {
    if (*entry == L'+') actual_groups.push_back(entry + 1); else actual_services.push_back(entry);
  }
  require(actual_services == services && actual_groups == groups, "Only requested dependencies changed, with expected order and prefixes");
}
static int dependencies_live(const std::wstring &exe) {
  SC_HANDLE manager = NULL;
  try {
    auto groups = live_dependency_groups();
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    require(manager != NULL, "Create isolated dependency fixtures on disposable host");
    {
      service_fixture a(manager, exe, L"a", L"NSSM-Dependency-Test");
      service_fixture b(manager, exe, L"b", L"NSSM-Dependency-Test");
      service_fixture target(manager, exe, L"target", L"NSSM-Dependency-Test", SERVICE_CHANGE_CONFIG);
      service_fixture imported(manager, exe, L"import", L"NSSM-Dependency-Test", SERVICE_CHANGE_CONFIG);
      target.configure(exe); imported.configure(exe);
      auto set = [&](const wchar_t *kind, std::initializer_list<std::wstring> values) {
        std::wstring arguments = L"set " + quoted(target.name) + L" " + kind;
        for (const auto &value : values) arguments += L" " + quoted(value);
        require(run(exe, arguments).code == 0, "Set dependency via real NSSM CLI");
      };
      auto reset = [&](const wchar_t *kind) {
        require(run(exe, L"reset " + quoted(target.name) + L" " + kind).code == 0, "Reset dependency via real NSSM CLI");
      };
      set(L"DependOnService", {L":" + a.name}); expect_dependencies(target.service, {a.name}, {});
      reset(L"DependOnService"); expect_dependencies(target.service, {}, {});
      set(L"DependOnGroup", {L":" + groups[0]}); expect_dependencies(target.service, {}, {groups[0]});
      reset(L"DependOnGroup"); expect_dependencies(target.service, {}, {});
      // Both portions and duplicate operations, then reset with the other retained.
      set(L"DependOnService", {L":" + a.name, b.name, a.name});
      set(L"DependOnGroup", {L":" + groups[0], L"+" + groups[1], groups[0]});
      expect_dependencies(target.service, {a.name, b.name}, groups);
      set(L"DependOnService", {L"+" + a.name}); set(L"DependOnGroup", {L"++" + groups[0]});
      expect_dependencies(target.service, {a.name, b.name}, groups);
      result read = run(exe, L"get " + quoted(target.name) + L" DependOnService");
      require(read.code == 0 && read.output.find(a.name) != std::wstring::npos && read.output.find(b.name) != std::wstring::npos, "Get service dependencies");
      reset(L"DependOnService"); expect_dependencies(target.service, {}, groups);
      set(L"DependOnService", {L":" + a.name}); reset(L"DependOnGroup"); expect_dependencies(target.service, {a.name}, {});
      // Removing the final entry must retain the other dependency type.
      set(L"DependOnGroup", {L":" + groups[0]}); set(L"DependOnService", {L"-" + a.name});
      expect_dependencies(target.service, {}, {groups[0]});
      set(L"DependOnService", {L"+" + a.name}); set(L"DependOnGroup", {L"-+" + groups[0]});
      expect_dependencies(target.service, {a.name}, {});
      set(L"DependOnService", {L""}); expect_dependencies(target.service, {}, {});
      reset(L"DependOnService"); reset(L"DependOnGroup"); expect_dependencies(target.service, {}, {});
      // Failed validation and an SCM circular-dependency rejection preserve the list.
      set(L"DependOnService", {L":" + a.name}); set(L"DependOnGroup", {L":" + groups[0]});
      require(run(exe, L"set " + quoted(target.name) + L" DependOnService " + quoted(L":" + target.name)).code != 0, "SCM rejects self-dependency");
      expect_dependencies(target.service, {a.name}, {groups[0]});
      require(run(exe, L"set " + quoted(target.name) + L" DependOnService " + quoted(L":Missing-" + target.name)).code != 0, "Reject missing service");
      require(run(exe, L"set " + quoted(target.name) + L" DependOnGroup " + quoted(L":Missing-" + target.name)).code != 0, "Reject missing group");
      expect_dependencies(target.service, {a.name}, {groups[0]});
      // Replay only dependency setters into the owned destination fixture.
      set(L"DependOnService", {L":" + a.name, b.name}); set(L"DependOnGroup", {L":" + groups[0], groups[1]});
      result exported = run(exe, L"dump " + quoted(target.name) + L" " + quoted(imported.name));
      require(exported.code == 0, "Export dependency configuration");
      unsigned replayed = 0; size_t offset = 0;
      while (offset < exported.output.size()) {
        size_t end = exported.output.find(L'\n', offset); if (end == std::wstring::npos) end = exported.output.size();
        std::wstring line = exported.output.substr(offset, end - offset); if (! line.empty() && line.back() == L'\r') line.pop_back();
        if (line.find(L" DependOnService ") != std::wstring::npos || line.find(L" DependOnGroup ") != std::wstring::npos) {
          size_t command = line.find(L" set "); require(command != std::wstring::npos && line.find(imported.name) != std::wstring::npos, "Replay only into the destination fixture");
          require(run(exe, line.substr(command + 1)).code == 0, "Replay exported dependency setter"); replayed++;
        }
        offset = end + 1;
      }
      require(replayed == 4, "Export all four dependency entries"); expect_dependencies(imported.service, {a.name, b.name}, groups);
      target.query_only();
      require(run(exe, L"reset " + quoted(target.name) + L" DependOnGroup").code != 0, "Query-only caller cannot reset dependencies");
      expect_dependencies(target.service, {a.name, b.name}, groups);
    }
    CloseServiceHandle(manager); manager = NULL;
    require(! service_fixture::cleanup_failed, "Dependency fixture cleanup succeeded");
    puts("Dependency SCM round trips passed; all four disabled fixtures were deleted without starting."); return 0;
  }
  catch (const std::exception &error) {
    if (manager) CloseServiceHandle(manager);
    fprintf(stderr, "SCM dependency test failed: %s\n", error.what()); return 1;
  }
}
