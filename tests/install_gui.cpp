#include "install_fixture.h"
#include "gui_fixture.h"
#include <algorithm>
namespace gui_test {
bool active;
struct allocation { void *p; size_t bytes; bool secret; };
static std::vector<allocation> allocations;
static unsigned closes, saves;
static int token, save_result, fail_after = -1, read_failure;
static SC_HANDLE borrowed = reinterpret_cast<SC_HANDLE>(&token);
static const TCHAR *known_account;
LPVOID allocate(HANDLE h, DWORD f, SIZE_T n) {
  if (active && fail_after == 0) return NULL;
  if (active && fail_after > 0) --fail_after;
  void *p = HeapAlloc(h, f, n);
  if (active && p) { allocation a = {p, n, n == 16 * sizeof(TCHAR)}; allocations.push_back(a); }
  return p;
}
BOOL release(HANDLE h, DWORD f, LPVOID p) {
  for (size_t i = 0; i < allocations.size(); i++) if (allocations[i].p == p) {
    if (allocations[i].secret) for (size_t j = 0; j < allocations[i].bytes; j++) CHECK(static_cast<BYTE *>(p)[j] == 0);
    allocations.erase(allocations.begin() + i); break;
  }
  return HeapFree(h, f, p);
}
BOOL close(SC_HANDLE h) { CHECK(h == borrowed); closes++; return TRUE; }
static int save(nssm_service_t *s, bool editing) {
  if (! active) return edit_service(s, editing);
  if (! s) return 1;
  CHECK(s && s->handle == borrowed); CHECK(! closes); saves++; return save_result;
}
static const TCHAR *well_known(const TCHAR *s) { return active ? known_account : well_known_username(s); }
static int environment(TCHAR *s) { return active ? (_tcscmp(s, _T("invalid")) ? 0 : 1) : test_environment(s); }
static UINT WINAPI text(HWND h, int id, LPTSTR s, int n) {
  if (active && id == read_failure) { if (n) s[0] = 0; return 0; }
  return GetDlgItemText(h, id, s, n);
}
static BOOL WINAPI show(HWND h, int mode) {
  if (! active) return ShowWindow(h, mode);
  if (! GetParent(h) && mode == SW_SHOW) PostMessage(h, WM_COMMAND, IDCANCEL, 0);
  return ShowWindow(h, SW_HIDE);
}
static void reset() { REQUIRE(allocations.empty()); active = true; closes = saves = 0; save_result = read_failure = 0; fail_after = -1; known_account = NULL; }
static void finish() {
  CHECK(allocations.empty());
  for (auto a : allocations) { SecureZeroMemory(a.p, a.bytes); HeapFree(GetProcessHeap(), 0, a.p); }
  allocations.clear(); active = false;
}
}
#define popup_message install_test::popup
#define HeapAlloc gui_test::allocate
#define HeapFree gui_test::release
#define edit_service gui_test::save
#define well_known_username gui_test::well_known
#define test_environment gui_test::environment
#define ShowWindow gui_test::show
#undef GetDlgItemText
#define GetDlgItemText gui_test::text
#include "../src/gui.cpp"
#undef popup_message
#undef HeapAlloc
#undef HeapFree
#undef edit_service
#undef well_known_username
#undef test_environment
#undef ShowWindow
#undef GetDlgItemText
#define GetDlgItemText GetDlgItemTextW

void test_install_gui() {
  registry_fixture registry;
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
  REQUIRE(InitCommonControlsEx(&controls));
  nssm_service_t *defaults = new nssm_service_t();
  HWND window = dialog(MAKEINTRESOURCE(IDD_INSTALL), NULL, nssm_dlg, reinterpret_cast<LPARAM>(defaults));
  delete defaults;
  REQUIRE(window && ! IsWindowVisible(window));
  SetDlgItemText(window, IDC_NAME, _T("NSSM-Test-Install"));
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T("C:\\fixture.exe"));
  const int expected[] = {2, 5, 6, 6, 0};
  for (unsigned scenario = 0; scenario < _countof(expected); scenario++) {
    install_test::reset(scenario);
    CHECK(install(window) == expected[scenario]);
    install_test::finish();
  }
  DestroyWindow(window);
}

static HWND gui_window(nssm_service_t *original) {
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
  REQUIRE(InitCommonControlsEx(&controls));
  HWND window = dialog(MAKEINTRESOURCE(IDD_EDIT), NULL, nssm_dlg, reinterpret_cast<LPARAM>(original));
  REQUIRE(window && ! IsWindowVisible(window));
  SetDlgItemText(window, IDC_NAME, _T("NSSM-Test-GUI"));
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T("C:\\fixture.exe"));
  return window;
}
void test_gui_hook_launch(const TCHAR *);
void test_gui_retry() {
  registry_fixture registry;
  nssm_service_t original = {}; original.handle = gui_test::borrowed;
  HWND window = gui_window(&original);
  gui_test::reset();
  SetDlgItemText(window, IDC_NAME, _T(""));
  CHECK(edit(window, &original) == 2); CHECK(! gui_test::closes); CHECK(gui_test::allocations.empty());
  CHECK(install(window) == 2); CHECK(gui_test::allocations.empty());
  SetDlgItemText(window, IDC_NAME, _T("NSSM-Test-GUI"));
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T(""));
  CHECK(edit(window, &original) == 3); CHECK(! gui_test::closes); CHECK(gui_test::allocations.empty());
  CHECK(install(window) == 3); CHECK(gui_test::allocations.empty());
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T("C:\\fixture.exe"));
  SetDlgItemText(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, _T("invalid"));
  CHECK(edit(window, &original) == 5); CHECK(gui_test::closes == 0); CHECK(gui_test::allocations.empty());
  SetDlgItemText(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, _T("VALID=yes"));
  gui_test::save_result = 5;
  CHECK(edit(window, &original) == 6); CHECK(gui_test::closes == 0); CHECK(gui_test::allocations.empty());
  gui_test::save_result = 0;
  CHECK(edit(window, &original) == 0); CHECK(gui_test::closes == 0); CHECK(gui_test::allocations.empty());
  CHECK(gui_test::saves == 2);
  gui_test::fail_after = 0;
  CHECK(edit(window, &original) == 1); CHECK(! gui_test::closes);
  CHECK(install(window) == 1); CHECK(remove(window) == 1);
  gui_test::finish(); DestroyWindow(window);
  // Exercise the real dialog entry point and cancellation with display suppressed.
  MSG message; while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
  for (int resource : {IDD_EDIT, 9999}) {
    gui_test::reset(); nssm_service_t *owned = alloc_nssm_service(); REQUIRE(owned);
    owned->handle = gui_test::borrowed; _tcscpy_s(owned->name, _T("NSSM-Test-GUI"));
    CHECK(nssm_gui(resource, owned) == (resource == IDD_EDIT ? 0 : 1));
    CHECK(gui_test::closes == 1); gui_test::finish();
    while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
  }
}
void test_gui_password() {
  registry_fixture registry;
  nssm_service_t original = {}; original.native = true;
  HWND window = gui_window(&original);
  SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, BM_SETCHECK, BST_UNCHECKED, 0);
  SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_USERNAME, _T("Example"));
  SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, _T("ExampleSecret42"));
  SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, _T("ExampleSecret42"));
  for (int scenario = 0; scenario < 9; scenario++) {
    gui_test::reset();
    nssm_service_t *attempt = alloc_nssm_service(); REQUIRE(attempt);
    SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, scenario == 1 ? _T("MismatchSecret!") : _T("ExampleSecret42"));
    if (scenario == 2) gui_test::read_failure = IDC_PASSWORD1;
    if (scenario == 3) gui_test::read_failure = IDC_PASSWORD2;
    if (scenario >= 4 && scenario <= 6) gui_test::fail_after = scenario - 4;
    if (scenario == 7) SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, _T("short"));
    if (scenario == 8) SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, _T(""));
    CHECK((configure(window, attempt, &original) == 0) == (scenario == 0));
    if (! scenario) CHECK(attempt->password && ! _tcscmp(attempt->password, _T("ExampleSecret42")));
    cleanup_nssm_service(attempt); gui_test::finish();
  }
  for (bool fail : {false, true}) {
    gui_test::reset(); gui_test::known_account = NSSM_LOCALSERVICE_ACCOUNT;
    nssm_service_t *attempt = alloc_nssm_service(); REQUIRE(attempt);
    if (fail) gui_test::fail_after = 1; // Read username succeeds; canonical allocation fails.
    CHECK((configure(window, attempt, &original) == 0) == ! fail);
    if (! fail) CHECK(! _tcscmp(attempt->username, NSSM_LOCALSERVICE_ACCOUNT));
    cleanup_nssm_service(attempt); gui_test::finish();
  }
  DestroyWindow(window);
}
void test_gui_hooks() {
  registry_fixture registry;
  nssm_service_t original = {}; _tcscpy_s(original.name, _T("NSSM-Test-GUI"));
  TCHAR command[] = _T("\"%NSSM_GUI_HOOK_PATH%\\hook.exe\" \"final quoted argument\"");
  REQUIRE(SetEnvironmentVariable(_T("NSSM_GUI_HOOK_PATH"), _T("C:\\Original Path")));
  REQUIRE(set_hook(original.name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, command) == 0);
  HWND window = gui_window(&original);
  TCHAR displayed[CMD_LENGTH]; GetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, displayed, _countof(displayed));
  CHECK(! _tcscmp(displayed, command));
  CHECK(update_hooks(original.name) == 0);
  HKEY key = open_registry(original.name, _T("AppEvents\\Start"), KEY_READ); REQUIRE(key);
  CHECK(get_string(key, _T("Pre"), displayed, sizeof(displayed), false, false, true) == 0);
  CHECK(! _tcscmp(displayed, command)); RegCloseKey(key);
  DestroyWindow(window);
  // View/cancel and unrelated saves retain both the literal text and its type.
  for (DWORD type : {REG_SZ, REG_EXPAND_SZ}) {
    key = open_registry(original.name, _T("AppEvents\\Start"), KEY_WRITE); REQUIRE(key);
    REQUIRE(set_string(key, _T("Pre"), command, type == REG_EXPAND_SZ) == 0); RegCloseKey(key);
    window = gui_window(&original); DestroyWindow(window);
    window = gui_window(&original); CHECK(update_hooks(original.name) == 0);
    key = open_registry(original.name, _T("AppEvents\\Start"), KEY_READ); REQUIRE(key);
    DWORD stored_type, bytes = sizeof(displayed);
    CHECK(RegQueryValueEx(key, _T("Pre"), NULL, &stored_type, reinterpret_cast<BYTE *>(displayed), &bytes) == 0);
    CHECK(stored_type == type && ! _tcscmp(displayed, command)); RegCloseKey(key);
    TCHAR changed[] = _T("\"%NSSM_GUI_HOOK_PATH%\\edited.exe\" \"final quoted argument\"");
    SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, changed); CHECK(update_hooks(original.name) == 0);
    key = open_registry(original.name, _T("AppEvents\\Start"), KEY_READ); REQUIRE(key); bytes = sizeof(displayed);
    CHECK(RegQueryValueEx(key, _T("Pre"), NULL, &stored_type, reinterpret_cast<BYTE *>(displayed), &bytes) == 0);
    CHECK(stored_type == type && ! _tcscmp(displayed, changed)); RegCloseKey(key); DestroyWindow(window);
  }
  // A malformed entry cannot be loaded: an unrelated save must leave it alone.
  key = open_registry(original.name, _T("AppEvents\\Start"), KEY_WRITE); REQUIRE(key);
  DWORD number = 42; REQUIRE(RegSetValueEx(key, _T("Pre"), 0, REG_DWORD, reinterpret_cast<BYTE *>(&number), sizeof(number)) == 0); RegCloseKey(key);
  window = gui_window(&original); CHECK(update_hooks(original.name) == 0); DestroyWindow(window);
  key = open_registry(original.name, _T("AppEvents\\Start"), KEY_READ); REQUIRE(key);
  DWORD stored_type, bytes = sizeof(number), actual;
  CHECK(RegQueryValueEx(key, _T("Pre"), NULL, &stored_type, reinterpret_cast<BYTE *>(&actual), &bytes) == 0);
  CHECK(stored_type == REG_DWORD && actual == number); RegCloseKey(key);
  // Save a reference, then change the environment before invoking the real hook.
  TCHAR launch[] = _T("\"%NSSM_GUI_HOOK_EXE%\" --hook-child \"final quoted argument\"");
  REQUIRE(SetEnvironmentVariable(_T("NSSM_GUI_HOOK_EXE"), _T("C:\\Does Not Exist\\probe.exe")));
  REQUIRE(set_hook(original.name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, launch) == 0);
  window = gui_window(&original); CHECK(update_hooks(original.name) == 0); DestroyWindow(window);
  TCHAR exe[PATH_LENGTH]; REQUIRE(GetModuleFileName(NULL, exe, _countof(exe)));
  REQUIRE(SetEnvironmentVariable(_T("NSSM_GUI_HOOK_EXE"), exe));
  test_gui_hook_launch(original.name);
  SetEnvironmentVariable(_T("NSSM_GUI_HOOK_EXE"), NULL);
  SetEnvironmentVariable(_T("NSSM_GUI_HOOK_PATH"), NULL);
  // New install dialogs must not inherit the preceding dialog's hook scratch values.
  ZeroMemory(&original, sizeof(original)); window = gui_window(&original);
  GetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, displayed, _countof(displayed)); CHECK(! displayed[0]); DestroyWindow(window);
}
