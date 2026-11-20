#include "install_fixture.h"
#include "gui_fixture.h"
#include <algorithm>
namespace gui_test {
bool active;
struct allocation { void *p; size_t bytes; bool secret; };
static std::vector<allocation> allocations;
static unsigned closes, saves, released_icons;
static BOOL WINAPI release_icon(HICON icon) {
  BOOL released = DestroyIcon(icon);
  if (released) ++released_icons;
  return released;
}
static bool fail_hook_save;
static int write_hook(const TCHAR *service, const TCHAR *event, const TCHAR *action, TCHAR *command, bool expand) {
  return fail_hook_save ? 1 : set_hook(service, event, action, command, expand);
}
static int token, save_result, fail_after = -1, read_failure;
static int display_failure, short_read;
static bool short_display;
static BOOL WINAPI display(HWND field, LPCTSTR value) {
  if (display_failure && GetDlgCtrlID(field) == display_failure) {
    return short_display ? SetWindowText(field, _T("x")) : FALSE;
  }
  return SetWindowText(field, value);
}
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
static UINT WINAPI text(HWND h, int id, LPTSTR s, int n) {
  if (active && id == read_failure) { if (n) s[0] = 0; return 0; }
  if (active && id == short_read && n > 2) return GetDlgItemText(h, id, s, n - 1);
  return GetDlgItemText(h, id, s, n);
}
static BOOL WINAPI show(HWND h, int mode) {
  if (! active) return ShowWindow(h, mode);
  if (! GetParent(h) && mode == SW_SHOW) PostMessage(h, WM_COMMAND, IDCANCEL, 0);
  return ShowWindow(h, SW_HIDE);
}
static unsigned labels, browse_calls;
static bool long_filter;
static TCHAR *label(DWORD id) {
  TCHAR *value;
  if (long_filter && id == NSSM_GUI_BROWSE_FILTER_APPLICATIONS) {
    value = (TCHAR *) LocalAlloc(LPTR, 300 * sizeof(TCHAR));
    REQUIRE(value);
    std::fill_n(value, 299, _T('a'));
  } else value = message_string(id);
  if (value) ++labels;
  return value;
}
static HLOCAL WINAPI release_label(HLOCAL p) {
  REQUIRE(labels > 0);
  --labels;
  return LocalFree(p);
}
static BOOL WINAPI browse_file(LPOPENFILENAME info) {
  ++browse_calls;
  CHECK(info->lpstrTitle && *info->lpstrTitle && (info->Flags & OFN_NOCHANGEDIR));
  if (long_filter) CHECK(!info->lpstrFilter);
  else {
    const TCHAR *p = info->lpstrFilter;
    REQUIRE(p && *p);
    for (const TCHAR *pattern : {_T("*.exe;*.bat;*.cmd"), _T("*.*")}) {
      REQUIRE(*p);
      p += _tcslen(p) + 1;
      CHECK(!_tcscmp(p, pattern));
      p += _tcslen(p) + 1;
    }
    CHECK(!*p);
  }
  return FALSE;
}
static void reset() { REQUIRE(allocations.empty()); active = true; closes = saves = 0; save_result = read_failure = short_read = 0; fail_after = -1; known_account = NULL; }
static void finish() {
  CHECK(allocations.empty());
  for (auto a : allocations) { SecureZeroMemory(a.p, a.bytes); HeapFree(GetProcessHeap(), 0, a.p); }
  allocations.clear(); active = false;
}
}
#undef SetWindowText
#define SetWindowText gui_test::display
#define DestroyIcon gui_test::release_icon
#define popup_message install_test::popup
#define HeapAlloc gui_test::allocate
#define HeapFree gui_test::release
#define edit_service gui_test::save
#define well_known_username gui_test::well_known
#define ShowWindow gui_test::show
#undef GetDlgItemText
#define GetDlgItemText gui_test::text
#define set_hook gui_test::write_hook
#define message_string gui_test::label
#define LocalFree gui_test::release_label
#undef GetOpenFileName
#define GetOpenFileName gui_test::browse_file
#include "../src/gui.cpp"
#undef message_string
#undef LocalFree
#undef GetOpenFileName
#undef set_hook
#undef SetWindowText
#define SetWindowText SetWindowTextW
#undef DestroyIcon
#undef popup_message
#undef HeapAlloc
#undef HeapFree
#undef edit_service
#undef well_known_username
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
  CHECK(gui_test::labels == 0);
  for (unsigned i = 0; i < 10; i++) set_hook_tab(0, 0);
  CHECK(gui_test::labels == 0);
  for (bool oversized : {false, true}) {
    gui_test::long_filter = oversized;
    browse(window, _T(""), 0, NSSM_GUI_BROWSE_FILTER_APPLICATIONS, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
    CHECK(gui_test::labels == 0);
  }
  gui_test::long_filter = false;
  CHECK(gui_test::browse_calls == 2);
  const std::basic_string<TCHAR> maximum(256, _T('n'));
  const auto oversized = maximum + _T('x');
  SetDlgItemText(window, IDC_NAME, oversized.c_str());
  CHECK(remove(window) == 2);
  nssm_service_t *draft = alloc_nssm_service();
  REQUIRE(draft);
  CHECK(configure(window, draft, NULL) == 2);
  SetDlgItemText(window, IDC_NAME, maximum.c_str());
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T("C:\\fixture.exe"));
  SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, oversized.c_str());
  CHECK(configure(window, draft, NULL) == 5);
  SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, maximum.c_str());
  CHECK(configure(window, draft, NULL) == 0);
  CHECK(maximum == draft->name && maximum == draft->displayname);
  struct choice_t { int tab, control; };
  const choice_t choices[] = {{NSSM_TAB_DETAILS, IDC_STARTUP}, {NSSM_TAB_PROCESS, IDC_PRIORITY}, {NSSM_TAB_EXIT, IDC_APPEXIT}};
  for (const auto &choice : choices) {
    HWND combo = GetDlgItem(tablist[choice.tab], choice.control);
    REQUIRE(combo);
    LRESULT selected = SendMessage(combo, CB_GETCURSEL, 0, 0);
    SendMessage(combo, CB_SETCURSEL, (WPARAM)-1, 0);
    CHECK(configure(window, draft, NULL) == 5);
    SendMessage(combo, CB_SETCURSEL, selected, 0);
    SetWindowLongPtr(combo, GWLP_ID, 0);
    CHECK(configure(window, draft, NULL) == 5);
    SetWindowLongPtr(combo, GWLP_ID, choice.control);
    LRESULT count = SendMessage(combo, CB_GETCOUNT, 0, 0);
    LRESULT length = SendMessage(combo, CB_GETLBTEXTLEN, count - 1, 0);
    REQUIRE(length >= 0);
    std::vector<TCHAR> label((size_t)length + 1);
    REQUIRE(SendMessage(combo, CB_GETLBTEXT, count - 1, (LPARAM)label.data()) == length);
    SendMessage(combo, CB_DELETESTRING, count - 1, 0);
    CHECK(configure(window, draft, NULL) == 5);
    REQUIRE(SendMessage(combo, CB_INSERTSTRING, count - 1, (LPARAM)label.data()) == count - 1);
    SendMessage(combo, CB_SETCURSEL, selected, 0);
    REQUIRE(configure(window, draft, NULL) == 0);
  }
  HWND exit_tab = tablist[NSSM_TAB_EXIT];
  for (const TCHAR *invalid : {_T(""), _T("42ms"), _T("-1"), _T("4294967296")}) {
    SetDlgItemText(exit_tab, IDC_THROTTLE, invalid);
    CHECK(configure(window, draft, NULL) == 5);
  }
  SetDlgItemText(exit_tab, IDC_THROTTLE, _T("000000000004294967295"));
  REQUIRE(configure(window, draft, NULL) == 0);
  CHECK(draft->throttle_delay == MAXDWORD);
  SetDlgItemInt(exit_tab, IDC_THROTTLE, 0, FALSE);
  REQUIRE(configure(window, draft, NULL) == 0);
  CHECK(!draft->throttle_delay);
  HWND rotation = tablist[NSSM_TAB_ROTATION];
  SendDlgItemMessage(rotation, IDC_ROTATE, BM_SETCHECK, BST_CHECKED, 0);
  SetDlgItemText(rotation, IDC_ROTATE_BYTES_LOW, _T("200g"));
  CHECK(configure(window, draft, NULL) == 5);
  SetDlgItemInt(rotation, IDC_ROTATE_BYTES_LOW, 42, FALSE);
  REQUIRE(configure(window, draft, NULL) == 0);
  CHECK(draft->rotate_bytes_low == 42);
  SendDlgItemMessage(rotation, IDC_ROTATE, BM_SETCHECK, BST_UNCHECKED, 0);
  TCHAR field[5];
  HWND application = tablist[NSSM_TAB_APPLICATION];
  SetDlgItemText(application, IDC_DIR, _T("1234"));
  CHECK(read_edit_text(application, IDC_DIR, field, _countof(field)) && !_tcscmp(field, _T("1234")));
  SetDlgItemText(application, IDC_DIR, _T("12345"));
  CHECK(!read_edit_text(application, IDC_DIR, field, _countof(field)));
  gui_test::reset(); gui_test::read_failure = IDC_DIR;
  CHECK(configure(window, draft, NULL) == 3);
  gui_test::finish();
  SetDlgItemText(application, IDC_DIR, _T(""));
  std::wstring long_description(_countof(draft->description), L'x');
  SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, long_description.c_str());
  CHECK(configure(window, draft, NULL) == 5);
  SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, _T(""));
  cleanup_nssm_service(draft);
  SetDlgItemText(window, IDC_NAME, _T("NSSM-Test-Install"));
  SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, _T(""));
  const int expected[] = {2, 5, 6, 6, 0};
  for (unsigned scenario = 0; scenario < _countof(expected); scenario++) {
    install_test::reset(scenario);
    CHECK(install(window) == expected[scenario]);
    install_test::finish();
  }
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T("test-hook"));
  install_test::reset(5);
  gui_test::fail_hook_save = true;
  CHECK(install(window) == 6);
  gui_test::fail_hook_save = false;
  install_test::finish();
  for (unsigned scenario : {6U, 7U}) {
    install_test::reset(scenario);
    SendDlgItemMessage(tablist[NSSM_TAB_DETAILS], IDC_STARTUP, CB_SETCURSEL,
      scenario == 6 ? NSSM_STARTUP_DELAYED : NSSM_STARTUP_AUTOMATIC, 0);
    CHECK(install(window) == (scenario == 6 ? 6 : 0));
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
static void test_edit_list_load() {
  TCHAR block[] = _T("ONE=1\0TWO=2\0");
  nssm_service_t *service = new nssm_service_t();
  HWND window = gui_window(service);
  service->dependencies = service->env = block;
  service->dependencieslen = service->envlen = _countof(block);
  REQUIRE(load_edit_lists(window, service));
  TCHAR text[40];
  GetDlgItemText(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, text, _countof(text));
  CHECK(!_tcscmp(text, _T("ONE=1\r\nTWO=2")));
  GetDlgItemText(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, text, _countof(text));
  CHECK(!_tcscmp(text, _T("ONE=1\r\nTWO=2")));
  CHECK(IsDlgButtonChecked(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE) == BST_CHECKED);
  service->handle = gui_test::borrowed;
  for (int id : {IDC_DEPENDENCIES, IDC_ENVIRONMENT}) {
    HWND tab = tablist[id == IDC_DEPENDENCIES ? NSSM_TAB_DEPENDENCIES : NSSM_TAB_ENVIRONMENT];
    HWND field = GetDlgItem(tab, id);
    gui_test::reset();
    gui_test::short_read = id;
    CHECK(edit(window, service) != 0 && !gui_test::saves);
    gui_test::short_read = 0;
    SetWindowLongPtr(field, GWLP_ID, 0);
    CHECK(edit(window, service) != 0 && !gui_test::saves);
    SetWindowLongPtr(field, GWLP_ID, id);
    gui_test::finish();
  }
  HWND selector = GetDlgItem(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE);
  SetWindowLongPtr(selector, GWLP_ID, 0);
  gui_test::reset();
  CHECK(edit(window, service) != 0 && !gui_test::saves);
  gui_test::finish();
  DestroyWindow(window);
  delete service;
  MSG message;
  while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
  for (int field : {IDC_NAME, IDC_PATH, IDC_DIR, IDC_FLAGS, IDC_STDIN, IDC_STDOUT, IDC_STDERR,
                    IDC_DISPLAYNAME, IDC_DESCRIPTION, IDC_USERNAME, IDC_KILL_WINDOW, IDC_ROTATE_BYTES_LOW}) {
    for (bool partial : {false, true}) {
      gui_test::reset();
      nssm_service_t *owned = alloc_nssm_service(); REQUIRE(owned);
      owned->handle = gui_test::borrowed;
      _tcscpy_s(owned->flags, _T("original arguments"));
      owned->username = (TCHAR *)HeapAlloc(GetProcessHeap(), 0, sizeof(_T("Example")));
      REQUIRE(owned->username);
      memcpy(owned->username, _T("Example"), sizeof(_T("Example")));
      gui_test::display_failure = field;
      gui_test::short_display = partial;
      CHECK(nssm_gui(IDD_EDIT, owned) == 1 && gui_test::closes == 1 && !gui_test::saves);
      gui_test::display_failure = 0;
      gui_test::finish();
      while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    }
  }
  for (int field : {IDC_DEPENDENCIES, IDC_ENVIRONMENT}) {
    for (int failure = 0; failure < 4; ++failure) {
      gui_test::reset();
      nssm_service_t *owned = alloc_nssm_service(); REQUIRE(owned);
      owned->handle = gui_test::borrowed;
      TCHAR multiline[] = _T("ONE=1\r\nTWO=2\0");
      TCHAR *copy = copy_environment_block(failure == 3 ? multiline : block); REQUIRE(copy);
      DWORD length = failure == 3 ? _countof(multiline) : failure ? _countof(block) : 3;
      if (field == IDC_DEPENDENCIES) { owned->dependencies = copy; owned->dependencieslen = length; }
      else { owned->env_extra = copy; owned->env_extralen = length; }
      gui_test::display_failure = failure == 1 || failure == 2 ? field : 0;
      gui_test::short_display = failure == 2;
      CHECK(nssm_gui(IDD_EDIT, owned) == 1);
      CHECK(gui_test::closes == 1 && !gui_test::saves);
      gui_test::display_failure = 0;
      gui_test::finish();
      while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    }
  }
}

void test_gui_retry() {
  test_edit_list_load();
  registry_fixture registry;
  nssm_service_t original = {}; original.handle = gui_test::borrowed;
  HWND window = gui_window(&original);
  original.stdout_copy_and_truncate = original.stderr_copy_and_truncate = true;
  original.rotate_delay = 71; original.stderr_disposition = OPEN_EXISTING;
  original.rotate_files = true; original.rotate_bytes_high = 2; original.rotate_bytes_low = 1234;
  SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_SETCHECK, BST_CHECKED, 0);
  original.affinity = (1LL << 40) | 2;
  HWND process_tab = tablist[NSSM_TAB_PROCESS];
  HWND cpus = GetDlgItem(process_tab, IDC_AFFINITY);
  populate_affinity_list(cpus, 15);
  select_affinity_list(cpus, original.affinity);
  SendDlgItemMessage(process_tab, IDC_AFFINITY_ALL, BM_SETCHECK, BST_UNCHECKED, 0);
  original.stdout_sharing = FILE_SHARE_DELETE;
  original.stdout_type = REG_SZ;
  _tcscpy_s(original.stdout_path, _T("%UserProfile%\\app.log"));
  SetDlgItemText(tablist[NSSM_TAB_IO], IDC_STDOUT, original.stdout_path);
  nssm_service_t *edited = alloc_nssm_service(); REQUIRE(edited);
  SendDlgItemMessage(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE, BM_SETCHECK, BST_CHECKED, 0);
  REQUIRE(configure(window, edited, &original) == 0);
  CHECK(!edited->dir[0] && edited->envlen == 2 && edited->env && !edited->env[0] && !edited->env[1]);
  CHECK(edited->stdout_copy_and_truncate && edited->stderr_copy_and_truncate);
  CHECK(edited->rotate_delay == 71 && edited->stderr_disposition == OPEN_EXISTING);
  CHECK(edited->affinity == original.affinity);
  CHECK(edited->rotate_bytes_high == 2 && edited->rotate_bytes_low == 1234);
  CHECK(edited->stdout_sharing == FILE_SHARE_DELETE && edited->stdout_type == REG_SZ && edited->preserve_stdout_path);
  cleanup_nssm_service(edited);
  SendMessage(process_tab, WM_COMMAND, MAKEWPARAM(IDC_AFFINITY, LBN_SELCHANGE), (LPARAM)cpus);
  edited = alloc_nssm_service(); REQUIRE(edited && !configure(window, edited, &original));
  CHECK(edited->affinity == 2); cleanup_nssm_service(edited);
  SendDlgItemMessage(process_tab, IDC_AFFINITY_ALL, BM_SETCHECK, BST_CHECKED, 0);
  SendMessage(process_tab, WM_COMMAND, MAKEWPARAM(IDC_AFFINITY_ALL, BN_CLICKED), 0);
  edited = alloc_nssm_service(); REQUIRE(edited && !configure(window, edited, &original));
  CHECK(!edited->affinity); cleanup_nssm_service(edited);
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
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T("test-hook"));
  gui_test::fail_hook_save = true;
  CHECK(edit(window, &original) == 6 && ! gui_test::closes && gui_test::allocations.empty());
  gui_test::fail_hook_save = false;
  CHECK(edit(window, &original) == 0);
  gui_test::fail_after = 0;
  CHECK(edit(window, &original) == 1); CHECK(! gui_test::closes);
  CHECK(install(window) == 1); CHECK(remove(window) == 1);
  gui_test::finish(); DestroyWindow(window);
  // Exercise the real dialog entry point and cancellation with display suppressed.
  MSG message; while (PeekMessage(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
  for (int resource : {IDD_EDIT, 9999}) {
    gui_test::reset(); nssm_service_t *owned = alloc_nssm_service(); REQUIRE(owned);
    owned->handle = gui_test::borrowed; _tcscpy_s(owned->name, _T("NSSM-Test-GUI"));
    gui_test::released_icons = 0;
    CHECK(nssm_gui(resource, owned) == (resource == IDD_EDIT ? 0 : 1));
    CHECK(gui_test::released_icons == (resource == IDD_EDIT ? 2U : 0U));
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
  for (int scenario = 0; scenario < 12; scenario++) {
    gui_test::reset();
    nssm_service_t *attempt = alloc_nssm_service(); REQUIRE(attempt);
    SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, scenario == 1 ? _T("MismatchSecret!") : _T("ExampleSecret42"));
    SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, _T("ExampleSecret42"));
    if (scenario >= 9) gui_test::short_read = scenario == 9 ? IDC_USERNAME : scenario == 10 ? IDC_PASSWORD1 : IDC_PASSWORD2;
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
  REQUIRE(SetEnvironmentVariable(_T("NSSM_HOOK_Start_Pre"), _T("inherited")));
  HWND window = gui_window(&original);
  TCHAR inherited[32];
  CHECK(GetEnvironmentVariable(_T("NSSM_HOOK_Start_Pre"), inherited, _countof(inherited)) == 9 &&
        !_tcscmp(inherited, _T("inherited")));
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
  window = gui_window(&original);
  TCHAR external[] = _T("cli-changed");
  REQUIRE(!set_hook(original.name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, external));
  REQUIRE(!set_hook(original.name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_POST, external));
  set_hook_tab(CB_ERR, 0);
  set_hook_tab(0, 1);
  CHECK(!update_hooks(original.name));
  for (const TCHAR *action : {NSSM_HOOK_ACTION_PRE, NSSM_HOOK_ACTION_POST}) {
    REQUIRE(!get_hook(original.name, NSSM_HOOK_EVENT_START, action, displayed, sizeof(displayed), false));
    CHECK(!_tcscmp(displayed, external));
  }
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T("replacement"));
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T(""));
  REQUIRE(!update_hooks(original.name));
  REQUIRE(!get_hook(original.name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_POST, displayed, sizeof(displayed), false));
  CHECK(!displayed[0]);
  DestroyWindow(window);
  CHECK(GetEnvironmentVariable(_T("NSSM_HOOK_Start_Pre"), inherited, _countof(inherited)) == 9 &&
        !_tcscmp(inherited, _T("inherited")));
  SetEnvironmentVariable(_T("NSSM_HOOK_Start_Pre"), NULL);
  window = gui_window(&original);
  gui_test::reset(); gui_test::read_failure = IDC_HOOK;
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T("read failed"));
  CHECK(update_hooks(original.name) != 0);
  nssm_service_t *draft = alloc_nssm_service(); REQUIRE(draft);
  CHECK(configure(window, draft, &original) == 4);
  gui_test::read_failure = 0;
  SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, _T("corrected"));
  CHECK(!update_hooks(original.name));
  cleanup_nssm_service(draft); gui_test::finish(); DestroyWindow(window);
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
