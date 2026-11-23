// All window searches are restricted to the editor process created for the fixture.
#include "../resources/resource.h"
namespace edit_live {
struct editor {
  PROCESS_INFORMATION process = {};
  HWND window = NULL;
  explicit editor(const std::wstring &nssm, const std::wstring &name) {
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNORMAL;
    std::wstring command = quoted(nssm) + L" edit " + quoted(name);
    require(CreateProcessW(nssm.c_str(), &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &process) != FALSE,
            "Open fixture editor");
  }
  ~editor() {
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
  }
  struct search { HWND owner, found; };
  static BOOL CALLBACK find_window(HWND h, LPARAM parameter) {
    search &s = *reinterpret_cast<search *>(parameter);
    wchar_t kind[32];
    if (!GetClassNameW(h, kind, _countof(kind)) || wcscmp(kind, L"#32770")) return TRUE;
    if (s.owner) {
      if (h == s.owner || !IsWindowEnabled(h) || !GetWindow(h, GW_OWNER)) return TRUE;
    }
    else if (!GetDlgItem(h, IDC_NAME) || GetWindowLongPtrW(h, GWLP_USERDATA) != IDD_EDIT) return TRUE;
    s.found = h;
    return FALSE;
  }
  static BOOL CALLBACK describe_window(HWND h, LPARAM) {
    wchar_t title[128] = {};
    GetWindowTextW(h, title, _countof(title));
    fwprintf(stderr, L"Fixture window %ls (visible %d, owner %p)\n", title, IsWindowVisible(h), GetWindow(h, GW_OWNER));
    return TRUE;
  }
  HWND wait_window(HWND owner) {
    search s = {owner, NULL};
    ULONGLONG end = GetTickCount64() + 8000;
    do {
      EnumThreadWindows(process.dwThreadId, find_window, reinterpret_cast<LPARAM>(&s));
      if (s.found) return s.found;
    } while (WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT && GetTickCount64() < end);
    DWORD code = 0; GetExitCodeProcess(process.hProcess, &code);
    fprintf(stderr, "Timed out waiting for %s (editor exit code %lu)\n", owner ? "result" : "editor", code);
    EnumThreadWindows(process.dwThreadId, describe_window, 0);
    require(false, "Locate editor or save result dialog");
    return NULL;
  }
  void open() {
    window = wait_window(NULL);
    WaitForInputIdle(process.hProcess, 5000);
  }
  struct control { int id; HWND field; };
  static BOOL CALLBACK find_control(HWND h, LPARAM parameter) {
    control &c = *reinterpret_cast<control *>(parameter);
    if (GetDlgCtrlID(h) != c.id) return TRUE;
    c.field = h;
    return FALSE;
  }
  HWND field(int id) {
    control c = {id, NULL};
    EnumChildWindows(window, find_control, reinterpret_cast<LPARAM>(&c));
    require(c.field != NULL, "Find service edit control");
    return c.field;
  }
  void set(int id, const wchar_t *value) {
    require(SendMessageW(field(id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value)) != FALSE, "Enter service setting");
  }
  void save(bool success) {
    require(PostMessageW(window, WM_COMMAND, IDOK, 0) != FALSE, "Submit service edit");
    HWND result = wait_window(window);
    require(PostMessageW(result, WM_CLOSE, 0, 0) != FALSE, "Dismiss result");
    if (!success) {
      ULONGLONG end = GetTickCount64() + 5000;
      while (IsWindow(result) && GetTickCount64() < end) Sleep(20);
      require(!IsWindow(result) && IsWindowEnabled(window), "Failed save retained the editor");
      PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
    }
    require(WaitForSingleObject(process.hProcess, 8000) == WAIT_OBJECT_0, "Editor closed");
  }
};

static void rejected_edits(const std::wstring &nssm, service_fixture &owned, const std::wstring &duplicate_display) {
  for (bool deny_registry : {false, true}) {
    editor dialog(nssm, owned.name);
    dialog.open();
    dialog.set(IDC_FLAGS, L"edited arguments");
    dialog.set(IDC_DESCRIPTION, L"edited description");
    dialog.set(IDC_DISPLAYNAME, deny_registry ? L"NSSM fixture registry edit" : duplicate_display.c_str());
    require(SendMessageW(dialog.field(IDC_STARTUP), CB_SETCURSEL, 2, 0) != CB_ERR, "Request manual startup");
    if (deny_registry) {
      DWORD bytes = 0;
      require(RegGetKeySecurity(owned.key, DACL_SECURITY_INFORMATION, NULL, &bytes) == ERROR_INSUFFICIENT_BUFFER, "Size fixture DACL");
      std::vector<BYTE> original(bytes);
      require(RegGetKeySecurity(owned.key, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)original.data(), &bytes) == ERROR_SUCCESS, "Read fixture DACL");
      PSECURITY_DESCRIPTOR denied = NULL;
      require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x2;;;WD)(A;;KA;;;BA)(A;;KA;;;SY)", SDDL_REVISION_1, &denied, NULL) != FALSE, "Build fixture-only DACL");
      LONG status = RegSetKeySecurity(owned.key, DACL_SECURITY_INFORMATION, denied);
      LocalFree(denied);
      require(status == ERROR_SUCCESS, "Deny parameter writes");
      try { dialog.save(false); }
      catch (...) { RegSetKeySecurity(owned.key, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)original.data()); throw; }
      require(RegSetKeySecurity(owned.key, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)original.data()) == ERROR_SUCCESS, "Restore fixture DACL");
    }
    else dialog.save(false);
    const std::wstring get = L"get " + quoted(owned.name) + L" ";
    require(run(nssm, get + L"AppParameters").output.find(L"round trip") != std::wstring::npos, "Failed edit preserved arguments");
    result desc = run(nssm, get + L"Description");
    require(!desc.code && desc.output.find(L"edited description") == std::wstring::npos, "Failed edit restored description");
    require(run(nssm, get + L"Start").output.find(L"SERVICE_DISABLED") != std::wstring::npos, "Failed edit preserved startup");
  }
}
}
