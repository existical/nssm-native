#include "install_fixture.h"
namespace install_test {
bool active, fail_allocation;
static unsigned failure, frees, manager_closes, service_closes, deletes;
static int manager_token, service_token;
static SC_HANDLE manager_handle = reinterpret_cast<SC_HANDLE>(&manager_token), service_handle = reinterpret_cast<SC_HANDLE>(&service_token);
static std::vector<void *> objects;
void reset(unsigned scenario) {
  REQUIRE(objects.empty()); active = true; fail_allocation = false; failure = scenario;
  frees = manager_closes = service_closes = deletes = 0;
}
LPVOID allocate(HANDLE heap, DWORD flags, SIZE_T size) {
  if (active && size == sizeof(nssm_service_t) && fail_allocation) return NULL;
  void *p = HeapAlloc(heap, flags, size);
  if (active && p && size == sizeof(nssm_service_t)) objects.push_back(p);
  return p;
}
BOOL release(HANDLE heap, DWORD flags, LPVOID p) {
  if (active) for (size_t i = 0; i < objects.size(); i++) if (objects[i] == p) {
    CHECK(frees == 0); frees++; return TRUE; // Quarantine to observe legacy double destruction safely.
  }
  return HeapFree(heap, flags, p);
}
SC_HANDLE manager() { SetLastError(ERROR_ACCESS_DENIED); return failure == 0 ? NULL : manager_handle; }
BOOL close(SC_HANDLE handle) {
  if (handle == manager_handle) { manager_closes++; CHECK(manager_closes == 1); }
  else { CHECK(handle == service_handle); service_closes++; CHECK(service_closes == 1); }
  return TRUE;
}
SC_HANDLE WINAPI create(SC_HANDLE h, LPCTSTR name, LPCTSTR display, DWORD access, DWORD type, DWORD start, DWORD error, LPCTSTR path, LPCTSTR group, LPDWORD tag, LPCTSTR dependencies, LPCTSTR account, LPCTSTR password) {
  if (! active) return CreateService(h, name, display, access, type, start, error, path, group, tag, dependencies, account, password);
  CHECK(h == manager_handle); SetLastError(ERROR_ACCESS_DENIED); return failure == 1 ? NULL : service_handle;
}
BOOL WINAPI change(SC_HANDLE h, DWORD type, DWORD start, DWORD error, LPCTSTR path, LPCTSTR group, LPDWORD tag, LPCTSTR dependencies, LPCTSTR account, LPCTSTR password, LPCTSTR display) {
  if (! active) return ChangeServiceConfig(h, type, start, error, path, group, tag, dependencies, account, password, display);
  CHECK(h == service_handle); SetLastError(ERROR_ACCESS_DENIED); return failure != 2;
}
BOOL WINAPI change2(SC_HANDLE h, DWORD level, LPVOID info) { return active ? TRUE : ChangeServiceConfig2(h, level, info); }
BOOL WINAPI remove(SC_HANDLE h) { if (! active) return DeleteService(h); CHECK(h == service_handle); deletes++; return TRUE; }
int parameters(nssm_service_t *service, bool editing) { return active ? (failure == 3 ? 1 : 0) : create_parameters(service, editing); }
int popup(HWND, unsigned int, unsigned long, ...) { return IDOK; } // No dialogs in regression tests.
void finish() {
  CHECK(frees == objects.size());
  if (! objects.empty()) {
    CHECK(manager_closes == (failure == 0 ? 0U : 1U));
    CHECK(service_closes == (failure < 2 ? 0U : 1U));
    CHECK(deletes == (failure == 2 || failure == 3 ? 1U : 0U));
  }
  for (size_t i = 0; i < objects.size(); i++) HeapFree(GetProcessHeap(), 0, objects[i]);
  objects.clear(); active = false;
}
}
void test_install_ownership() {
  const int expected[] = {2, 5, 6, 6, 0};
  TCHAR *args[] = {_T("NSSM-Test-Install"), _T("C:\\fixture.exe")};
  for (unsigned scenario = 0; scenario < _countof(expected); scenario++) {
    install_test::reset(scenario);
    CHECK(pre_install_service(2, args) == expected[scenario]);
    install_test::finish();
  }
  install_test::reset(0); install_test::fail_allocation = true;
  CHECK(pre_install_service(2, args) == 1);
  CHECK(pre_install_service(1, args) == 1);
  install_test::finish();
  std::basic_string<TCHAR> long_flags(CMD_LENGTH + 1, _T('x'));
  TCHAR *long_args[] = {args[0], args[1], &long_flags[0]};
  install_test::reset(0);
  CHECK(pre_install_service(3, long_args) == 2);
  install_test::finish();
  test_install_gui();
}
