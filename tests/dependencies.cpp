#include "dependency_fixture.h"
#include <algorithm>
namespace dependency_test {
bool active;
static std::vector<std::basic_string<TCHAR>> current;
static std::vector<void *> owned;
static unsigned manager_opens, manager_closes, service_opens, service_closes, writes, queries;
static bool fail_query, fail_change, fail_manager, fail_service;
static bool fail_name;
static int fail_after = -1, malformed_query;
static SC_HANDLE manager_handle = reinterpret_cast<SC_HANDLE>(1), dependency_handle = reinterpret_cast<SC_HANDLE>(2);
LPVOID allocate(HANDLE h, DWORD f, SIZE_T n) {
  if (active && fail_after == 0) return NULL;
  if (active && fail_after > 0) --fail_after;
  void *p = HeapAlloc(h, f, n);
  if (active && p) owned.push_back(p);
  return p;
}
BOOL release(HANDLE h, DWORD f, LPVOID p) {
  auto i = std::find(owned.begin(), owned.end(), p); if (i != owned.end()) owned.erase(i);
  return HeapFree(h, f, p);
}
SC_HANDLE manager(DWORD) {
  if (fail_manager) { SetLastError(ERROR_ACCESS_DENIED); return NULL; }
  manager_opens++; return manager_handle;
}
SC_HANDLE open(LPCTSTR value, DWORD) {
  if (fail_service || ! _tcsnicmp(value, _T("Missing"), 7)) { SetLastError(ERROR_ACCESS_DENIED); return NULL; }
  service_opens++; return dependency_handle;
}
BOOL close(SC_HANDLE h) {
  if (h == manager_handle) manager_closes++;
  else { CHECK(h == dependency_handle); service_closes++; }
  return TRUE;
}
BOOL name(LPCTSTR value, LPTSTR output, LPDWORD capacity) {
  if (fail_name) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  return _tcscpy_s(output, *capacity, value) == 0;
}
static std::vector<TCHAR> block() {
  std::vector<TCHAR> result;
  for (const auto &item : current) { result.insert(result.end(), item.begin(), item.end()); result.push_back(0); }
  if (result.empty()) result.push_back(0);
  result.push_back(0); return result;
}
BOOL WINAPI query(SC_HANDLE h, LPQUERY_SERVICE_CONFIG out, DWORD bytes, LPDWORD needed) {
  if (! active) return QueryServiceConfig(h, out, bytes, needed);
  queries++;
  if (fail_query) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  auto data = block(); *needed = static_cast<DWORD>(sizeof(*out) + data.size() * sizeof(TCHAR));
  if (! out || bytes < *needed) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  ZeroMemory(out, bytes); out->lpDependencies = reinterpret_cast<TCHAR *>(out + 1);
  memcpy(out->lpDependencies, &data[0], data.size() * sizeof(TCHAR));
  if (malformed_query == 1) out->lpDependencies = reinterpret_cast<TCHAR *>(reinterpret_cast<BYTE *>(out) + bytes);
  if (malformed_query == 2) out->lpDependencies = reinterpret_cast<TCHAR *>(reinterpret_cast<BYTE *>(out + 1) + 1);
  if (malformed_query == 3) out->lpDependencies[data.size() - 1] = _T('x');
  if (malformed_query == 4) out->lpDependencies = reinterpret_cast<TCHAR *>(out);
  return TRUE;
}
BOOL WINAPI change(SC_HANDLE h, DWORD type, DWORD start, DWORD error, LPCTSTR path, LPCTSTR group, LPDWORD tag, LPCTSTR deps, LPCTSTR account, LPCTSTR password, LPCTSTR display) {
  if (! active) return ChangeServiceConfig(h, type, start, error, path, group, tag, deps, account, password, display);
  CHECK(type == SERVICE_NO_CHANGE && start == SERVICE_NO_CHANGE && error == SERVICE_NO_CHANGE);
  CHECK(! path && ! group && ! tag && ! account && ! password && ! display); writes++;
  if (fail_change) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  CHECK(deps != NULL); // NULL means unchanged to SCM, not an empty dependency list.
  if (! deps) return TRUE;
  current.clear(); for (const TCHAR *s = deps; *s; s += _tcslen(s) + 1) current.push_back(s);
  return TRUE;
}
static void reset(std::initializer_list<const TCHAR *> entries) {
  REQUIRE(owned.empty()); active = true; current.assign(entries.begin(), entries.end());
  manager_opens = manager_closes = service_opens = service_closes = writes = queries = 0;
  fail_query = fail_change = fail_manager = fail_service = fail_name = false; fail_after = -1; malformed_query = 0;
}
static void expect(std::initializer_list<const TCHAR *> entries) {
  std::vector<std::basic_string<TCHAR>> expected(entries.begin(), entries.end()); CHECK(current == expected);
}
static void finish() {
  CHECK(owned.empty()); CHECK(manager_opens == manager_closes); CHECK(service_opens == service_closes);
  for (void *p : owned) HeapFree(GetProcessHeap(), 0, p); owned.clear(); active = false;
}
}
void test_dependencies() {
  using namespace dependency_test;
  registry_fixture registry;
  HKEY key; REQUIRE(RegCreateKeyEx(HKEY_LOCAL_MACHINE, NSSM_REGISTRY_GROUPS, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS);
  TCHAR groups[] = _T("Alpha\0Beta\0");
  REQUIRE(RegSetValueEx(key, NSSM_REG_GROUPS, 0, REG_MULTI_SZ, reinterpret_cast<BYTE *>(groups), sizeof(groups)) == ERROR_SUCCESS); RegCloseKey(key);
  reset({_T("ServiceA")}); CHECK(apply(DEPENDENCY_SERVICES, NULL) == 0); expect({}); finish();
  reset({_T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, NULL) == 0); expect({}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_SERVICES, _T("-ServiceA")) >= 0); expect({_T("+Alpha")}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, _T("-Alpha")) >= 0); expect({_T("ServiceA")}); finish();
  for (int type : {DEPENDENCY_SERVICES, DEPENDENCY_GROUPS}) {
    reset({}); CHECK(apply(type, NULL) == 0); expect({}); CHECK(writes == 1); finish();
    reset({}); CHECK(apply(type, _T("-Missing")) >= 0); expect({}); finish();
    reset({_T("ServiceA"), _T("+Alpha"), _T("ServiceB"), _T("+Beta")});
    CHECK(apply(type, NULL) == 0);
    if (type == DEPENDENCY_SERVICES) expect({_T("+Alpha"), _T("+Beta")}); else expect({_T("ServiceA"), _T("ServiceB")});
    CHECK(queries == 2 && writes == 1); finish();
    reset({_T("ServiceA"), _T("+Alpha")}); fail_change = true;
    CHECK(apply(type, NULL) == -1); expect({_T("ServiceA"), _T("+Alpha")}); finish();
    reset({_T("ServiceA"), _T("+Alpha")}); fail_query = true;
    CHECK(apply(type, _T(":New")) == -1); CHECK(! writes); expect({_T("ServiceA"), _T("+Alpha")}); finish();
    reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(type, _T("")) == 0);
    if (type == DEPENDENCY_SERVICES) expect({_T("+Alpha")}); else expect({_T("ServiceA")}); finish();
    reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(type, _T(":")) == 0);
    if (type == DEPENDENCY_SERVICES) expect({_T("+Alpha")}); else expect({_T("ServiceA")}); finish();
  }
  reset({_T("ServiceA"), _T("+Alpha"), _T("ServiceB"), _T("+Beta")});
  CHECK(apply(DEPENDENCY_SERVICES, _T(":New\r\nOther\r\nnew")) == 1); expect({_T("New"), _T("Other"), _T("+Alpha"), _T("+Beta")}); finish();
  reset({_T("ServiceA"), _T("+Alpha"), _T("ServiceB"), _T("+Beta")});
  CHECK(apply(DEPENDENCY_GROUPS, _T(":beta\n+Beta\n\nAlpha")) == 1); expect({_T("ServiceA"), _T("+Beta"), _T("+Alpha"), _T("ServiceB")}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_SERVICES, _T("+servicea")) == 1); expect({_T("ServiceA"), _T("+Alpha")}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, _T("++alpha")) == 1); expect({_T("ServiceA"), _T("+Alpha")}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_SERVICES, _T("+ServiceB")) == 1); expect({_T("ServiceA"), _T("+Alpha"), _T("ServiceB")}); finish();
  reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, _T("+Beta")) == 1); expect({_T("ServiceA"), _T("+Alpha"), _T("+Beta")}); finish();
  reset({_T("ServiceA"), _T("+Alpha"), _T("SERVICEA")}); CHECK(apply(DEPENDENCY_SERVICES, _T("-servicea")) == 1); expect({_T("+Alpha")}); finish();
  reset({_T("ServiceA"), _T("+Alpha"), _T("+ALPHA")}); CHECK(apply(DEPENDENCY_GROUPS, _T("-+alpha")) == 1); expect({_T("ServiceA")}); finish();
  reset({_T("A")}); CHECK(apply(DEPENDENCY_GROUPS, NULL) == 0); expect({_T("A")}); finish();
  reset({_T("ServiceA"), _T("+Alpha"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_SERVICES, _T(":New")) == 1); expect({_T("New"), _T("+Alpha"), _T("+Alpha")}); finish();
  reset({_T("ServiceA"), _T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, NULL) == 0); expect({_T("ServiceA"), _T("ServiceA")}); finish();
  // Resets/removals preserve existing names even when they are no longer resolvable.
  reset({_T("MissingService"), _T("+UnknownGroup")}); CHECK(apply(DEPENDENCY_GROUPS, NULL) == 0); expect({_T("MissingService")}); CHECK(! manager_opens); finish();
  reset({_T("MissingService"), _T("+UnknownGroup")}); CHECK(apply(DEPENDENCY_SERVICES, _T("-MissingService")) == 1); expect({_T("+UnknownGroup")}); CHECK(! manager_opens); finish();
  for (const TCHAR *bad : {_T("+"), _T("-"), _T(":+Alpha"), _T("+A\nB")}) {
    reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_SERVICES, bad) == -1); CHECK(! writes); expect({_T("ServiceA"), _T("+Alpha")}); finish();
  }
  for (int failure = 0; failure < 4; failure++) {
    reset({_T("ServiceA"), _T("+Alpha")});
    fail_manager = failure == 0; fail_service = failure == 1; fail_name = failure == 2; fail_change = failure == 3;
    CHECK(apply(DEPENDENCY_SERVICES, _T(":New")) == -1); expect({_T("ServiceA"), _T("+Alpha")}); finish();
  }
  // Deny each tracked settings/service allocation; unsuccessful attempts cannot write.
  for (int allocation = 0; allocation < 9; allocation++) {
    reset({_T("ServiceA"), _T("+Alpha")}); fail_after = allocation;
    int result = apply(DEPENDENCY_SERVICES, _T(":New"));
    if (result < 0) { CHECK(! writes); expect({_T("ServiceA"), _T("+Alpha")}); }
    else expect({_T("New"), _T("+Alpha")}); finish();
  }
  for (int mode = 0; mode < 5; mode++) {
    reset({_T("A"), _T("+Alpha")}); malformed_query = mode;
    TCHAR *out = reinterpret_cast<TCHAR *>(1); unsigned long count = 99;
    int result = get_service_dependencies(_T("Target"), reinterpret_cast<SC_HANDLE>(3), &out, &count);
    if (mode) { CHECK(result != 0 && ! out && ! count); }
    else { CHECK(result == 0 && count == 10); CHECK(! _tcscmp(out, _T("A")) && ! _tcscmp(out + 2, _T("+Alpha"))); release(GetProcessHeap(), 0, out); }
    finish();
  }
  reset({_T("ServiceA")}); fail_after = 2; CHECK(dump(DEPENDENCY_SERVICES) != 0); finish();
  reset({}); TCHAR lower[] = _T("+alpha\0");
  CHECK(set_service_dependencies(_T("Target"), reinterpret_cast<SC_HANDLE>(3), lower) == 0);
  CHECK(! _tcscmp(lower, _T("+alpha"))); expect({_T("+Alpha")}); finish();
  std::basic_string<TCHAR> long_name(255, _T('A'));
  reset({}); CHECK(apply(DEPENDENCY_SERVICES, long_name.c_str()) == 1); CHECK(current.size() == 1 && current[0] == long_name); finish();
  long_name += _T('A'); reset({}); CHECK(apply(DEPENDENCY_SERVICES, long_name.c_str()) == -1); CHECK(! writes); finish();
  DWORD before = test_handle_count();
  for (unsigned iteration = 0; iteration < 50; iteration++) {
    reset({_T("ServiceA"), _T("+Alpha")}); CHECK(apply(DEPENDENCY_GROUPS, _T(":MissingGroup")) == -1); CHECK(! writes); finish();
  }
  CHECK(test_handle_count() == before);
  // Failed group metadata queries must not alias or free the caller's input.
  for (DWORD type : {REG_SZ, REG_BINARY}) {
    REQUIRE(RegOpenKeyEx(HKEY_LOCAL_MACHINE, NSSM_REGISTRY_GROUPS, 0, KEY_SET_VALUE, &key) == 0);
    REQUIRE(RegSetValueEx(key, NSSM_REG_GROUPS, 0, type, reinterpret_cast<BYTE *>(groups), sizeof(groups)) == 0); RegCloseKey(key);
    reset({_T("ServiceA")}); CHECK(apply(DEPENDENCY_GROUPS, _T(":Alpha")) == -1); CHECK(! writes); expect({_T("ServiceA")}); finish();
  }
  REQUIRE(RegOpenKeyEx(HKEY_LOCAL_MACHINE, NSSM_REGISTRY_GROUPS, 0, KEY_SET_VALUE, &key) == 0);
  REQUIRE(RegDeleteValue(key, NSSM_REG_GROUPS) == 0); RegCloseKey(key);
  reset({_T("ServiceA")}); CHECK(apply(DEPENDENCY_GROUPS, _T(":Alpha")) == -1); CHECK(! writes); expect({_T("ServiceA")}); finish();
  REQUIRE(RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY_GROUPS) == 0);
  reset({_T("ServiceA")}); CHECK(apply(DEPENDENCY_GROUPS, _T(":Alpha")) == -1); CHECK(! writes); expect({_T("ServiceA")}); finish();
}
