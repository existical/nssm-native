#include "nssm.h"

extern const TCHAR *hook_event_strings[];
extern const TCHAR *hook_action_strings[];

static enum { NSSM_TAB_APPLICATION, NSSM_TAB_DETAILS, NSSM_TAB_LOGON, NSSM_TAB_DEPENDENCIES, NSSM_TAB_PROCESS, NSSM_TAB_SHUTDOWN, NSSM_TAB_EXIT, NSSM_TAB_IO, NSSM_TAB_ROTATION, NSSM_TAB_ENVIRONMENT, NSSM_TAB_HOOKS, NSSM_NUM_TABS } nssm_tabs;
static HWND tablist[NSSM_NUM_TABS];
static int selected_tab;

class gui_message {
  TCHAR *text;
  gui_message(const gui_message &) = delete;
  gui_message &operator=(const gui_message &) = delete;
public:
  explicit gui_message(DWORD id) : text(message_string(id)) {}
  ~gui_message() { if (text) LocalFree(text); }
  TCHAR *get() const { return text ? text : _T(""); }
};

static void insert_tab(HWND tabs, int index, DWORD id) {
  gui_message label(id);
  TCITEM tab = {};
  tab.mask = TCIF_TEXT;
  tab.pszText = label.get();
  SendMessage(tabs, TCM_INSERTITEM, index, (LPARAM) &tab);
}

static int edit_text_length(HWND parent, int id) {
  HWND control = GetDlgItem(parent, id);
  if (!control) return -1;
  SetLastError(ERROR_SUCCESS);
  int length = GetWindowTextLength(control);
  return (!length && GetLastError()) || length == INT_MAX ? -1 : length;
}

static bool read_edit_text(HWND parent, int id, TCHAR *buffer, int capacity) {
  int length = edit_text_length(parent, id);
  if (length < 0 || length >= capacity) return false;
  buffer[0] = 0;
  return !length || GetDlgItemText(parent, id, buffer, capacity) == length;
}

static bool read_edit_list(HWND tab, int id, TCHAR **block, DWORD *count) {
  int length = edit_text_length(tab, id);
  if (length < 0) return false;
  if (!length) return true;
  size_t bytes = (static_cast<size_t>(length) + 1) * sizeof(TCHAR);
  TCHAR *text = static_cast<TCHAR *>(HeapAlloc(GetProcessHeap(), 0, bytes));
  if (!text) return false;
  bool ok = GetDlgItemText(tab, id, text, length + 1) == length &&
            !unformat_double_null(text, length, block, count);
  HeapFree(GetProcessHeap(), 0, text);
  return ok;
}

/* Store the CPU identifier separately: list rows need not be dense CPU IDs. */
int populate_affinity_list(HWND list, DWORD_PTR available) {
  SendMessage(list, LB_RESETCONTENT, 0, 0);
  int count = 0;
  for (unsigned int cpu = 0; cpu < sizeof(available) * 8; cpu++) {
    if (! (available & ((DWORD_PTR) 1 << cpu))) continue;
    TCHAR label[3];
    _sntprintf_s(label, _countof(label), _TRUNCATE, _T("%u"), cpu);
    LRESULT row = SendMessage(list, LB_ADDSTRING, 0, (LPARAM) label);
    if (row < 0 || SendMessage(list, LB_SETITEMDATA, row, cpu) == LB_ERR) {
      SendMessage(list, LB_RESETCONTENT, 0, 0);
      return 0;
    }
    count++;
  }
  return count;
}

void select_affinity_list(HWND list, __int64 mask) {
  int count = (int) SendMessage(list, LB_GETCOUNT, 0, 0);
  for (int row = 0; row < count; row++) {
    LRESULT cpu = SendMessage(list, LB_GETITEMDATA, row, 0);
    bool selected = cpu >= 0 && cpu < 64 && (((unsigned __int64) mask) & (1ULL << cpu));
    SendMessage(list, LB_SETSEL, selected, row);
  }
}

int affinity_from_list(HWND list, __int64 *mask) {
  if (! mask) return 1;
  *mask = 0;
  int count = (int) SendMessage(list, LB_GETCOUNT, 0, 0);
  int selected = (int) SendMessage(list, LB_GETSELCOUNT, 0, 0);
  if (count <= 0 || count > 64 || selected <= 0 || selected > count) return 1;
  unsigned __int64 parsed = 0;
  for (int row = 0; row < count; row++) {
    LRESULT state = SendMessage(list, LB_GETSEL, row, 0);
    LRESULT cpu = SendMessage(list, LB_GETITEMDATA, row, 0);
    if (state == LB_ERR || cpu < 0 || cpu >= 64) return 1;
    if (state) parsed |= 1ULL << cpu;
  }
  /* Preserve the existing convention: all rows selected means no restriction. */
  if (selected < count) *mask = (__int64) parsed;
  return 0;
}

static HWND dialog(const TCHAR *templ, HWND parent, DLGPROC function, LPARAM l) {
  /* The caller will deal with GetLastError()... */
  HRSRC resource = FindResourceEx(0, RT_DIALOG, templ, GetUserDefaultLangID());
  if (! resource) {
    if (GetLastError() != ERROR_RESOURCE_LANG_NOT_FOUND) return 0;
    resource = FindResourceEx(0, RT_DIALOG, templ, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    if (! resource) return 0;
  }

  HGLOBAL ret = LoadResource(0, resource);
  if (! ret) return 0;

  return CreateDialogIndirectParam(0, (DLGTEMPLATE *) ret, parent, function, l);
}

static HWND dialog(const TCHAR *templ, HWND parent, DLGPROC function) {
  return dialog(templ, parent, function, 0);
}

static inline void set_logon_enabled(unsigned char interact_enabled, unsigned char credentials_enabled) {
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_INTERACT), interact_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_USERNAME), credentials_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1), credentials_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2), credentials_enabled);
}

static bool display_text(HWND tab, int control, const TCHAR *text) {
  HWND field = GetDlgItem(tab, control);
  size_t length = _tcslen(text);
  if (!field || length >= INT_MAX || !SetWindowText(field, text) || (size_t)GetWindowTextLength(field) != length) return false;
  if (!length) return true;
  std::unique_ptr<TCHAR[]> shown(new (std::nothrow) TCHAR[length + 1]);
  return shown && GetWindowText(field, shown.get(), (int)length + 1) == (int)length && !_tcscmp(shown.get(), text);
}

static bool display_edit_list(HWND tab, int control, TCHAR *block, DWORD length) {
  if (!length) return true;
  TCHAR *formatted;
  DWORD capacity;
  if (format_double_null(block, length, &formatted, &capacity)) return false;
  // A literal line break would become an entry separator on the next save.
  for (const TCHAR *entry = block; *entry; entry += _tcslen(entry) + 1) {
    if (_tcspbrk(entry, _T("\r\n"))) {
      HeapFree(GetProcessHeap(), 0, formatted);
      return false;
    }
  }
  const TCHAR *text = formatted ? formatted : _T("");
  bool complete = display_text(tab, control, text);
  if (formatted) HeapFree(GetProcessHeap(), 0, formatted);
  return complete;
}

static bool load_edit_lists(HWND window, nssm_service_t *service) {
  if (!display_edit_list(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, service->dependencies, service->dependencieslen)) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SETTING_LOAD_FAILED, gui_message(NSSM_GUI_TAB_DEPENDENCIES).get());
    return false;
  }
  if (service->native) return true;
  HWND tab = tablist[NSSM_TAB_ENVIRONMENT];
  bool extra = service->env_extralen != 0;
  if ((!extra && service->envlen && !CheckDlgButton(tab, IDC_ENVIRONMENT_REPLACE, BST_CHECKED)) ||
      !display_edit_list(tab, IDC_ENVIRONMENT, extra ? service->env_extra : service->env,
                         extra ? service->env_extralen : service->envlen)) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SETTING_LOAD_FAILED, extra ? NSSM_REG_ENV_EXTRA : NSSM_REG_ENV);
    return false;
  }
  return true;
}

static bool display_setting(HWND window, HWND tab, int control, const TCHAR *text, const TCHAR *name) {
  if (display_text(tab, control, text)) return true;
  popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SETTING_LOAD_FAILED, name);
  return false;
}

static bool load_edit_text(HWND window, nssm_service_t *service) {
  HWND application = tablist[NSSM_TAB_APPLICATION], details = tablist[NSSM_TAB_DETAILS], io = tablist[NSSM_TAB_IO];
  if (!display_setting(window, window, IDC_NAME, service->name, NSSM_NATIVE_NAME) ||
      !display_setting(window, details, IDC_DISPLAYNAME, service->displayname, NSSM_NATIVE_DISPLAYNAME) ||
      !display_setting(window, details, IDC_DESCRIPTION, service->description, NSSM_NATIVE_DESCRIPTION)) return false;
  if (service->username && !is_virtual_account(service->name, service->username) &&
      !display_setting(window, tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username, NSSM_NATIVE_OBJECTNAME)) return false;
  if (service->native) return display_setting(window, application, IDC_PATH, service->image, NSSM_NATIVE_IMAGEPATH);
  struct number_field { int tab, id; DWORD number; const TCHAR *name; };
  const number_field numbers[] = {
    {NSSM_TAB_SHUTDOWN, IDC_KILL_CONSOLE, service->kill_console_delay, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD},
    {NSSM_TAB_SHUTDOWN, IDC_KILL_WINDOW, service->kill_window_delay, NSSM_REG_KILL_WINDOW_GRACE_PERIOD},
    {NSSM_TAB_SHUTDOWN, IDC_KILL_THREADS, service->kill_threads_delay, NSSM_REG_KILL_THREADS_GRACE_PERIOD},
    {NSSM_TAB_EXIT, IDC_THROTTLE, service->throttle_delay, NSSM_REG_THROTTLE},
    {NSSM_TAB_EXIT, IDC_RESTART_DELAY, service->restart_delay, NSSM_REG_RESTART_DELAY},
    {NSSM_TAB_ROTATION, IDC_ROTATE_SECONDS, service->rotate_seconds, NSSM_REG_ROTATE_SECONDS},
    {NSSM_TAB_ROTATION, IDC_ROTATE_BYTES_LOW, service->rotate_bytes_low, NSSM_REG_ROTATE_BYTES_LOW}
  };
  for (const auto &field : numbers) {
    TCHAR digits[16] = {};
    if (field.id != IDC_ROTATE_BYTES_LOW || !service->rotate_bytes_high) _stprintf_s(digits, _T("%lu"), field.number);
    if (!display_setting(window, tablist[field.tab], field.id, digits, field.name)) return false;
  }
  return display_setting(window, application, IDC_PATH, service->exe, NSSM_REG_EXE) &&
         display_setting(window, application, IDC_DIR, service->dir, NSSM_REG_DIR) &&
         display_setting(window, application, IDC_FLAGS, service->flags, NSSM_REG_FLAGS) &&
         display_setting(window, io, IDC_STDIN, service->stdin_path, NSSM_REG_STDIN) &&
         display_setting(window, io, IDC_STDOUT, service->stdout_path, NSSM_REG_STDOUT) &&
         display_setting(window, io, IDC_STDERR, service->stderr_path, NSSM_REG_STDERR);
}

int nssm_gui(int resource, nssm_service_t *service) {
  /* CLI callers transfer the original object and its SCM handle here. */
  struct service_owner_t {
    nssm_service_t *value;
    ~service_owner_t() { cleanup_nssm_service(value); }
  } owner = { service };
  if (! service) return 1;
  /* Create window */
  HWND dlg = dialog(MAKEINTRESOURCE(resource), 0, nssm_dlg, (LPARAM) service);
  if (! dlg) {
    popup_message(0, MB_OK, NSSM_GUI_CREATEDIALOG_FAILED, error_string(GetLastError()));
    return 1;
  }

  if (resource == IDD_EDIT && (!load_edit_text(dlg, service) || !load_edit_lists(dlg, service))) {
    DestroyWindow(dlg);
    return 1;
  }

  /* Load the icon. */
  HICON small_icon = (HICON) LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_NSSM), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  if (small_icon) SendMessage(dlg, WM_SETICON, ICON_SMALL, (LPARAM) small_icon);
  HICON large_icon = (HICON) LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_NSSM), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  if (large_icon) SendMessage(dlg, WM_SETICON, ICON_BIG, (LPARAM) large_icon);

  /* Remember what the window is for. */
  SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR) resource);

  /* Display the window */
  centre_window(dlg);
  ShowWindow(dlg, SW_SHOW);

  /* Set service name if given */
  if (resource != IDD_EDIT && service->name[0]) {
    SetDlgItemText(dlg, IDC_NAME, service->name);
    /* No point making user click remove if the name is already entered */
    if (resource == IDD_REMOVE) {
      HWND button = GetDlgItem(dlg, IDC_REMOVE);
      if (button) {
        SendMessage(button, WM_LBUTTONDOWN, 0, 0);
        SendMessage(button, WM_LBUTTONUP, 0, 0);
      }
    }
  }

  if (resource == IDD_EDIT) {
    /* We'll need the service handle later. */
    SetWindowLongPtr(dlg, DWLP_USER, (LONG_PTR) service);

    /* Service name can't be edited. */
    EnableWindow(GetDlgItem(dlg, IDC_NAME), 0);
    SetFocus(GetDlgItem(dlg, IDOK));

    /* Set existing details. */
    HWND combo;
    HWND list;

    /* Details tab. */
    combo = GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
    SendMessage(combo, CB_SETCURSEL, service->startup, 0);

    /* Log on tab. */
    if (service->username) {
      if (is_virtual_account(service->name, service->username)) {
        CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_VIRTUAL_SERVICE);
        set_logon_enabled(0, 0);
      }
      else {
        CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_ACCOUNT);
        set_logon_enabled(0, 1);
      }
    }
    else {
      CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_LOCALSYSTEM);
      if (service->type & SERVICE_INTERACTIVE_PROCESS) SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_SETCHECK, BST_CHECKED, 0);
    }

    /* Process tab. */
    if (service->priority) {
      int priority = priority_constant_to_index(service->priority);
      combo = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
      SendMessage(combo, CB_SETCURSEL, priority, 0);
    }

    if (service->affinity) {
      list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY), 1);

      DWORD_PTR affinity, system_affinity;
      if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) {
        if ((service->affinity & (__int64) system_affinity) != service->affinity) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_AFFINITY);
      }

      select_affinity_list(list, service->affinity);
    }

    if (service->no_console) {
      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    /* Shutdown tab. */
    if (! (service->stop_method & NSSM_STOP_METHOD_CONSOLE)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE), 0);
    }
    if (! (service->stop_method & NSSM_STOP_METHOD_WINDOW)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW), 0);
    }
    if (! (service->stop_method & NSSM_STOP_METHOD_THREADS)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS), 0);
    }
    if (! (service->stop_method & NSSM_STOP_METHOD_TERMINATE)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    if (! service->kill_process_tree) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    /* Restart tab. */
    combo = GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
    SendMessage(combo, CB_SETCURSEL, service->default_exit_action, 0);

    /* I/O tab. */
    if (service->timestamp_log) SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, BST_CHECKED, 0);

    /* Rotation tab. */
    if (service->stdout_disposition == CREATE_ALWAYS) SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_SETCHECK, BST_CHECKED, 0);
    if (service->rotate_files) {
      SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_SETCHECK, BST_CHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), 1);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), 1);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), 1);
    }
    if (service->rotate_stdout_online || service->rotate_stderr_online) SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, BST_CHECKED, 0);

    /* Hooks tab. */
    if (service->hook_share_output_handles) SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, BST_CHECKED, 0);

    /* Check if advanced settings are in use. */
    if (service->stdout_disposition != service->stderr_disposition || (service->stdout_disposition && service->stdout_disposition != NSSM_STDOUT_DISPOSITION && service->stdout_disposition != CREATE_ALWAYS) || (service->stderr_disposition && service->stderr_disposition != NSSM_STDERR_DISPOSITION && service->stderr_disposition != CREATE_ALWAYS)) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_STDIO);
    if (service->rotate_bytes_high) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_ROTATE_BYTES);

    if (service->envlen && service->env_extralen) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_ENVIRONMENT);
  }

  /* Go! */
  MSG message = {};
  BOOL received;
  while ((received = GetMessage(&message, 0, 0, 0)) > 0) {
    if (IsDialogMessage(dlg, &message)) continue;
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  if (IsWindow(dlg)) DestroyWindow(dlg);
  if (large_icon) DestroyIcon(large_icon);
  if (small_icon) DestroyIcon(small_icon);
  return received < 0 ? 1 : (int) message.wParam;
}

void centre_window(HWND window) {
  HWND desktop;
  RECT size, desktop_size;
  unsigned long x, y;

  if (! window) return;

  /* Find window size */
  if (! GetWindowRect(window, &size)) return;

  /* Find desktop window */
  desktop = GetDesktopWindow();
  if (! desktop) return;

  /* Find desktop window size */
  if (! GetWindowRect(desktop, &desktop_size)) return;

  /* Centre window */
  x = (desktop_size.right - size.right) / 2;
  y = (desktop_size.bottom - size.bottom) / 2;
  MoveWindow(window, x, y, size.right - size.left, size.bottom - size.top, 0);
}

static inline void check_stop_method(nssm_service_t *service, unsigned long method, unsigned long control) {
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & BST_CHECKED) return;
  service->stop_method &= ~method;
}

static bool read_choice(HWND tab, int control, int count, const TCHAR *setting, DWORD *value) {
  HWND combo = GetDlgItem(tab, control);
  if (combo && SendMessage(combo, CB_GETCOUNT, 0, 0) == count) {
    LRESULT selected = SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (selected >= 0 && selected < count) {
      *value = (DWORD)selected;
      return true;
    }
  }
  popup_message(GetParent(tab), MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SETTING_LOAD_FAILED, setting);
  return false;
}

static bool read_number(HWND tab, int control, const TCHAR *setting, DWORD *value) {
  int length = GetWindowTextLength(GetDlgItem(tab, control));
  if (length > 0 && length < INT_MAX) {
    size_t bytes = (size_t)(length + 1) * sizeof(TCHAR);
    TCHAR *digits = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!digits) {
      popup_message(GetParent(tab), MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OUT_OF_MEMORY, setting, _T("read_number()"));
      return false;
    }
    DWORD number = 0;
    bool valid = read_edit_text(tab, control, digits, length + 1);
    for (int i = 0; valid && i < length; i++) {
      DWORD digit = digits[i] - _T('0');
      valid = digit <= 9 && number <= (MAXDWORD - digit) / 10;
      if (valid) number = number * 10 + digit;
    }
    HeapFree(GetProcessHeap(), 0, digits);
    if (valid) { *value = number; return true; }
  }
  popup_message(GetParent(tab), MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_NUMBER, setting);
  return false;
}

static inline void set_timeout_enabled(unsigned long control, unsigned long dependent) {
  unsigned char enabled = 0;
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 1;
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], dependent), enabled);
}

static inline void set_affinity_enabled(unsigned char enabled) {
  HWND list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
  EnableWindow(list, enabled && SendMessage(list, LB_GETCOUNT, 0, 0) > 0);
}

static inline void set_rotation_enabled(unsigned char enabled) {
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), enabled);
}

struct hook_name_t {
  const TCHAR *event;
  const TCHAR *action;
};
static const hook_name_t hook_names[] = {
  { NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE },
  { NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_POST },
  { NSSM_HOOK_EVENT_STOP, NSSM_HOOK_ACTION_PRE },
  { NSSM_HOOK_EVENT_EXIT, NSSM_HOOK_ACTION_POST },
  { NSSM_HOOK_EVENT_POWER, NSSM_HOOK_ACTION_CHANGE },
  { NSSM_HOOK_EVENT_POWER, NSSM_HOOK_ACTION_RESUME },
  { NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_PRE },
  { NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_POST }
};
struct hook_draft_t {
  TCHAR command[CMD_LENGTH];
  DWORD type;
  bool edited;
  bool read_failed;
};
static hook_draft_t hook_drafts[_countof(hook_names)];
static hook_draft_t *selected_hook;

static void set_hook_tab(int event_index, int action_index) {
  selected_hook = NULL;
  if (event_index < 0 || event_index > NSSM_GUI_HOOK_EVENT_ROTATE - NSSM_GUI_HOOK_EVENT_START) return;
  int first_event = NSSM_GUI_HOOK_EVENT_START;
  HWND combo;
  combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
  SendMessage(combo, CB_SETCURSEL, event_index, 0);
  combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_ACTION);
  SendMessage(combo, CB_RESETCONTENT, 0, 0);

  const TCHAR *hook_event = hook_event_strings[event_index];
  TCHAR *hook_action = 0;
  int i;
  switch (event_index + first_event) {
    case NSSM_GUI_HOOK_EVENT_ROTATE:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_ROTATE_PRE).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_ROTATE_POST).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_START:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_START_PRE).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_START_POST).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_STOP:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_STOP_PRE).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      break;

    case NSSM_GUI_HOOK_EVENT_EXIT:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_EXIT_POST).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_POWER:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_POWER_CHANGE).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_CHANGE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) gui_message(NSSM_GUI_HOOK_ACTION_POWER_RESUME).get());
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_RESUME;
      break;
  }

  SendMessage(combo, CB_SETCURSEL, action_index, 0);

  if (! hook_action) return;
  for (size_t i = 0; i < _countof(hook_names); i++) {
    if (str_equiv(hook_event, hook_names[i].event) && str_equiv(hook_action, hook_names[i].action)) {
      // WM_SETTEXT can synchronously notify EN_CHANGE.
      SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, hook_drafts[i].command);
      selected_hook = &hook_drafts[i];
      break;
    }
  }
}

static int update_hooks(TCHAR *service_name) {
  for (size_t i = 0; i < _countof(hook_names); i++) {
    hook_draft_t &draft = hook_drafts[i];
    if (draft.edited && (draft.read_failed ||
        set_hook(service_name, hook_names[i].event, hook_names[i].action, draft.command, draft.type != REG_SZ))) return 1;
  }
  return 0;
}

static bool check_io(HWND owner, TCHAR *name, TCHAR *buffer, unsigned long len, unsigned long control) {
  LRESULT count = SendMessage(GetDlgItem(tablist[NSSM_TAB_IO], control), WM_GETTEXTLENGTH, 0, 0);
  if (! count) return true;
  if (count > 0 && (unsigned long) count < len && GetDlgItemText(tablist[NSSM_TAB_IO], control, buffer, (int) len) == count) return true;
  popup_message(owner, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, name);
  return false;
}

static bool read_service_name(HWND window, TCHAR (&name)[SERVICE_NAME_LENGTH]) {
  int length = GetWindowTextLength(GetDlgItem(window, IDC_NAME));
  if (!length) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_SERVICE_NAME);
    return false;
  }
  if (length >= SERVICE_NAME_LENGTH || GetDlgItemText(window, IDC_NAME, name, _countof(name)) != length) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(ERROR_INVALID_NAME));
    return false;
  }
  return true;
}

/* Set service parameters. */
int configure(HWND window, nssm_service_t *service, nssm_service_t *orig_service) {
  if (! service) return 1;

  set_nssm_service_defaults(service);

  if (orig_service) {
    /* Preserve options that the editor cannot fully represent. */
    service->preserve_advanced_io = true;
    service->stdin_disposition = orig_service->stdin_disposition;
    service->stdout_disposition = orig_service->stdout_disposition;
    service->stderr_disposition = orig_service->stderr_disposition;
    service->rotate_delay = orig_service->rotate_delay;
    service->rotate_bytes_high = orig_service->rotate_bytes_high;
    service->rotate_bytes_low = orig_service->rotate_bytes_low;
    service->rotate_seconds = orig_service->rotate_seconds;
    service->rotate_stdout_online = orig_service->rotate_stdout_online;
    service->rotate_stderr_online = orig_service->rotate_stderr_online;
    service->stdout_copy_and_truncate = orig_service->stdout_copy_and_truncate;
    service->stderr_copy_and_truncate = orig_service->stderr_copy_and_truncate;
    service->stdin_sharing = orig_service->stdin_sharing;
    service->stdin_flags = orig_service->stdin_flags;
    service->stdin_type = orig_service->stdin_type;
    service->stdout_sharing = orig_service->stdout_sharing;
    service->stdout_flags = orig_service->stdout_flags;
    service->stdout_type = orig_service->stdout_type;
    service->stderr_sharing = orig_service->stderr_sharing;
    service->stderr_flags = orig_service->stderr_flags;
    service->stderr_type = orig_service->stderr_type;
    service->native = orig_service->native;
  }

  /* Get service name. */
  if (!read_service_name(window, service->name)) return 2;

  /* Get executable name */
  if (! service->native) {
    if (!read_edit_text(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->exe, _countof(service->exe))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM_REG_EXE);
      return 3;
    }
    if (!service->exe[0]) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PATH);
      return 3;
    }

    /* Get startup directory. */
    if (!read_edit_text(tablist[NSSM_TAB_APPLICATION], IDC_DIR, service->dir, _countof(service->dir))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM_REG_DIR);
      return 3;
    }
    if (!service->dir[0] && !orig_service && !executable_directory(service->exe, service->dir, _countof(service->dir))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM_REG_DIR);
      return 3;
    }

    for (size_t i = 0; i < _countof(hook_drafts); i++) {
      if (hook_drafts[i].read_failed) {
        popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_HOOK_COMMAND, hook_names[i].event, hook_names[i].action);
        return 4;
      }
    }

    /* Get flags. */
    if (!read_edit_text(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS, service->flags, _countof(service->flags))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_OPTIONS);
      return 4;
    }
  }

  /* Get details. */
  if (!read_edit_text(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, service->displayname, _countof(service->displayname))) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DISPLAYNAME);
    return 5;
  }

  if (!read_edit_text(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, service->description, _countof(service->description))) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DESCRIPTION);
    return 5;
  }

  if (!read_choice(tablist[NSSM_TAB_DETAILS], IDC_STARTUP, NSSM_STARTUP_DISABLED + 1,
      NSSM_NATIVE_STARTUP, &service->startup)) return 5;

  /* Get logon stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_GETCHECK, 0, 0) & BST_CHECKED) {
      service->type |= SERVICE_INTERACTIVE_PROCESS;
    }
    if (service->username) HeapFree(GetProcessHeap(), 0, service->username);
    service->username = 0;
    service->usernamelen = 0;
    if (service->password) {
      SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
      HeapFree(GetProcessHeap(), 0, service->password);
    }
    service->password = 0;
    service->passwordlen = 0;
  }
  else if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_VIRTUAL_SERVICE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    if (service->username) HeapFree(GetProcessHeap(), 0, service->username);
    service->username = virtual_account(service->name);
    if (! service->username) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("account name"), _T("install()"));
      return 6;
    }
    service->usernamelen = _tcslen(service->username) + 1;
    service->password = 0;
    service->passwordlen = 0;
  }
  else {
    /* Username. */
    int username_length = edit_text_length(tablist[NSSM_TAB_LOGON], IDC_USERNAME);
    if (username_length <= 0) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_USERNAME);
      return 6;
    }
    service->usernamelen = static_cast<size_t>(username_length) + 1;

    service->username = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(TCHAR));
    if (! service->username) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("account name"), _T("install()"));
      return 6;
    }
    if (GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username, (int) service->usernamelen) != username_length) {
      HeapFree(GetProcessHeap(), 0, service->username);
      service->username = 0;
      service->usernamelen = 0;
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_USERNAME);
      return 6;
    }

    /*
      Special case for well-known accounts.
      Ignore the password if we're editing and the username hasn't changed.
    */
    const TCHAR *well_known = well_known_username(service->username);
    if (well_known) {
      if (str_equiv(well_known, NSSM_LOCALSYSTEM_ACCOUNT)) {
        HeapFree(GetProcessHeap(), 0, service->username);
        service->username = 0;
        service->usernamelen = 0;
      }
      else {
        HeapFree(GetProcessHeap(), 0, service->username);
        service->username = 0;
        service->usernamelen = _tcslen(well_known) + 1;
        service->username = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(TCHAR));
        if (! service->username) {
          print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("install()"));
          return 6;
        }
        memmove(service->username, well_known, service->usernamelen * sizeof(TCHAR));
      }
    }
    else {
      /* Password. */
      int length = edit_text_length(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1);
      int confirmation_length = edit_text_length(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2);
      if (length < 0 || confirmation_length != length) {
        popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_PASSWORD);
        return 6;
      }

      if (! orig_service || ! orig_service->username || ! str_equiv(service->username, orig_service->username) || length) {
        if (!length) {
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PASSWORD);
          return 6;
        }
        service->passwordlen = static_cast<size_t>(length) + 1;

        /* Temporary buffer for password validation. */
        struct confirmation_t {
          TCHAR *value;
          size_t bytes;
          ~confirmation_t() {
            if (value) { SecureZeroMemory(value, bytes); HeapFree(GetProcessHeap(), 0, value); }
          }
        } confirmation = { 0, service->passwordlen * sizeof(TCHAR) };
        TCHAR *password = confirmation.value = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, confirmation.bytes);
        if (! password) {
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("password confirmation"), _T("install()"));
          return 6;
        }

        /* Actual password buffer. */
        service->password = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->passwordlen * sizeof(TCHAR));
        if (! service->password) {
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("password"), _T("install()"));
          return 6;
        }

        /* Get first password. */
        if (GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, service->password, (int) service->passwordlen) != length) {
          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_PASSWORD);
          return 6;
        }

        /* Get confirmation. */
        if (GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, password, (int) service->passwordlen) != length) {

          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_PASSWORD);
          return 6;
        }

        /* Compare. */
        if (_tcsncmp(password, service->password, service->passwordlen)) {
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PASSWORD);
          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          return 6;
        }
      }
    }
  }

  /* Get dependencies. */
  if (!read_edit_list(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, &service->dependencies, &service->dependencieslen)) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DEPENDENCIES);
    return 6;
  }

  /* Remaining tabs are only for services we manage. */
  if (service->native) return 0;

  /* Get process stuff. */
  DWORD priority_index;
  if (!read_choice(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY, NSSM_IDLE_PRIORITY + 1,
      NSSM_REG_PRIORITY, &priority_index)) return 5;
  service->priority = priority_index_to_constant(priority_index);

  service->affinity = 0LL;
  if (orig_service && !GetWindowLongPtr(tablist[NSSM_TAB_PROCESS], DWLP_USER))
    service->affinity = orig_service->affinity;
  else if (!(SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_GETCHECK, 0, 0) & BST_CHECKED)) {
    HWND list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
    if (affinity_from_list(list, &service->affinity)) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_WARN_AFFINITY_NONE);
      return 5;
    }
  }

  if (SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_GETCHECK, 0, 0) & BST_CHECKED) service->no_console = 0;
  else service->no_console = 1;

  /* Get stop method stuff. */
  check_stop_method(service, NSSM_STOP_METHOD_CONSOLE, IDC_METHOD_CONSOLE);
  check_stop_method(service, NSSM_STOP_METHOD_WINDOW, IDC_METHOD_WINDOW);
  check_stop_method(service, NSSM_STOP_METHOD_THREADS, IDC_METHOD_THREADS);
  check_stop_method(service, NSSM_STOP_METHOD_TERMINATE, IDC_METHOD_TERMINATE);
  HWND shutdown = tablist[NSSM_TAB_SHUTDOWN];
  if (!read_number(shutdown, IDC_KILL_CONSOLE, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, &service->kill_console_delay) ||
      !read_number(shutdown, IDC_KILL_WINDOW, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, &service->kill_window_delay) ||
      !read_number(shutdown, IDC_KILL_THREADS, NSSM_REG_KILL_THREADS_GRACE_PERIOD, &service->kill_threads_delay)) return 5;
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_GETCHECK, 0, 0) & BST_CHECKED) service->kill_process_tree = 1;
  else service->kill_process_tree = 0;

  /* Get exit action stuff. */
  if (!read_number(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, NSSM_REG_THROTTLE, &service->throttle_delay) ||
      !read_number(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, NSSM_REG_RESTART_DELAY, &service->restart_delay)) return 5;
  if (!read_choice(tablist[NSSM_TAB_EXIT], IDC_APPEXIT, NSSM_NUM_EXIT_ACTIONS,
      NSSM_REG_EXIT, &service->default_exit_action)) return 5;

  /* Get I/O stuff. */
  if (! check_io(window, _T("stdin"), service->stdin_path, _countof(service->stdin_path), IDC_STDIN) ||
      ! check_io(window, _T("stdout"), service->stdout_path, _countof(service->stdout_path), IDC_STDOUT) ||
      ! check_io(window, _T("stderr"), service->stderr_path, _countof(service->stderr_path), IDC_STDERR)) return 5;
  if (orig_service) {
    service->preserve_stdin_path = ! _tcscmp(service->stdin_path, orig_service->stdin_path);
    service->preserve_stdout_path = ! _tcscmp(service->stdout_path, orig_service->stdout_path);
    service->preserve_stderr_path = ! _tcscmp(service->stderr_path, orig_service->stderr_path);
  }
  if (SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_GETCHECK, 0, 0) & BST_CHECKED) service->timestamp_log = true;
  else service->timestamp_log = false;

  bool truncate = (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_GETCHECK, 0, 0) & BST_CHECKED) != 0;
  service->preserve_output_dispositions = orig_service && truncate == (orig_service->stdout_disposition == CREATE_ALWAYS);
  if (! service->preserve_output_dispositions) {
    service->stdout_disposition = truncate ? CREATE_ALWAYS : NSSM_STDOUT_DISPOSITION;
    service->stderr_disposition = truncate ? CREATE_ALWAYS : NSSM_STDERR_DISPOSITION;
  }

  /* Get rotation stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    service->rotate_files = true;
    service->rotate_stdout_online = service->rotate_stderr_online = (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_GETCHECK, 0, 0) & BST_CHECKED) != 0;
    if (!read_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, NSSM_REG_ROTATE_SECONDS, &service->rotate_seconds)) return 5;
    HWND size_field = GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW);
    if (! service->rotate_bytes_high || SendMessage(size_field, EM_GETMODIFY, 0, 0)) {
      if (!read_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, NSSM_REG_ROTATE_BYTES_LOW, &service->rotate_bytes_low)) return 5;
      service->rotate_bytes_high = 0;
    }
  }

  if (orig_service && orig_service->rotate_files && ! service->rotate_files) service->rotate_stdout_online = service->rotate_stderr_online = 0;

  /* Get hook stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_GETCHECK, 0, 0) & BST_CHECKED) service->hook_share_output_handles = true;

  /* Get environment. */
  HWND replace_control = GetDlgItem(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE);
  if (!replace_control) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_ENVIRONMENT);
    return 5;
  }
  bool replace = SendMessage(replace_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
  TCHAR **env = replace ? &service->env : &service->env_extra;
  DWORD *count = replace ? &service->envlen : &service->env_extralen;
  if (!read_edit_list(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, env, count) ||
      (*env && !valid_environment(*env, *count))) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_ENVIRONMENT);
    return 5;
  }
  if (replace && !*env) {
    service->envlen = 2;
    service->env = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, service->envlen * sizeof(TCHAR));
    if (!service->env) return 5;
  }

  /* When both blocks exist, the editor shows AppEnvironmentExtra. */
  if (orig_service && orig_service->env && orig_service->env_extra && !replace) {
    service->env = copy_environment_block(orig_service->env);
    if (! service->env) return 5;
    service->envlen = orig_service->envlen;
  }

  return 0;
}

/* Install the service. */
int install(HWND window) {
  if (! window) return 1;

  nssm_service_t *service = alloc_nssm_service();
  if (! service) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("install()"));
    return 1;
  }
  if (service) {
    int ret = configure(window, service, 0);
    if (ret) { cleanup_nssm_service(service); return ret; }
  }

  /* See if it works. */
  switch (install_service(service, update_hooks)) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("install()"));
      cleanup_nssm_service(service);
      return 1;

    case 2:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      cleanup_nssm_service(service);
      return 2;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
      cleanup_nssm_service(service);
      return 3;

    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
      cleanup_nssm_service(service);
      return 4;

    case 5:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INSTALL_SERVICE_FAILED);
      cleanup_nssm_service(service);
      return 5;

    case 7:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INSTALL_ROLLBACK_FAILED);
      cleanup_nssm_service(service);
      PostQuitMessage(7);
      return 7;

    case 6:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_CREATE_PARAMETERS_FAILED);
      cleanup_nssm_service(service);
      return 6;
  }

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_INSTALLED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

/* Remove the service */
int remove(HWND window) {
  if (! window) return 1;

  /* See if it works */
  nssm_service_t *service = alloc_nssm_service();
  if (! service) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("remove()"));
    return 1;
  }
  if (service) {
    /* Get service name */
    if (!read_service_name(window, service->name)) {
      cleanup_nssm_service(service);
      return 2;
    }

    /* Confirm */
    if (popup_message(window, MB_YESNO, NSSM_GUI_ASK_REMOVE_SERVICE, service->name) != IDYES) {
      cleanup_nssm_service(service);
      return 0;
    }
  }

  switch (remove_service(service)) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("remove()"));
      cleanup_nssm_service(service);
      return 1;

    case 2:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      cleanup_nssm_service(service);
      return 2;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SERVICE_NOT_INSTALLED);
      cleanup_nssm_service(service);
      return 3;

    case ERROR_ACCESS_DENIED:
    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_REMOVE_SERVICE_FAILED);
      cleanup_nssm_service(service);
      return 4;
  }

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_REMOVED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

int edit(HWND window, nssm_service_t *orig_service) {
  if (! window || ! orig_service) return 1;

  nssm_service_t *service = alloc_nssm_service();
  if (! service) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("edit()"));
    return 1;
  }
  if (service) {
    int ret = configure(window, service, orig_service);
    if (ret) { cleanup_nssm_service(service); return ret; }
    /* Borrow only during the save; the dialog retains ownership on failure. */
    service->handle = orig_service->handle;
  }
  int result = edit_service(service, true);
  if (service) service->handle = 0;

  switch (result) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("edit()"));
      cleanup_nssm_service(service);
      return 1;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
      cleanup_nssm_service(service);
      return 3;

    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
      cleanup_nssm_service(service);
      return 4;

    case 5:
    case 6:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_EDIT_PARAMETERS_FAILED);
      cleanup_nssm_service(service);
      return 6;
  }

  if (!service->native && update_hooks(service->name)) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_HOOK_SAVE_FAILED);
    cleanup_nssm_service(service);
    return 6;
  }

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_EDITED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

static TCHAR *browse_filter(int message) {
  switch (message) {
    case NSSM_GUI_BROWSE_FILTER_APPLICATIONS: return _T("*.exe;*.bat;*.cmd");
    case NSSM_GUI_BROWSE_FILTER_DIRECTORIES: return _T(".");
    case NSSM_GUI_BROWSE_FILTER_ALL_FILES: /* Fall through. */
    default: return _T("*.*");
  }
}

UINT_PTR CALLBACK browse_hook(HWND dlg, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_INITDIALOG:
      return 1;
  }

  return 0;
}

/* Browse for application */
void browse(HWND window, TCHAR *current, unsigned long flags, ...) {
  if (! window) return;

  OPENFILENAME ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetParent(window);
  TCHAR filter[256] = {};
  size_t used = 0;
  va_list arg;
  va_start(arg, flags);
  for (int id = va_arg(arg, int); id; id = va_arg(arg, int)) {
    gui_message label(id);
    const TCHAR *pattern = browse_filter(id);
    size_t name_size = _tcslen(label.get()) + 1;
    size_t pattern_size = _tcslen(pattern) + 1;
    if (name_size == 1 || name_size + pattern_size >= _countof(filter) - used) {
      used = 0;
      break;
    }
    memcpy(filter + used, label.get(), name_size * sizeof(TCHAR));
    used += name_size;
    memcpy(filter + used, pattern, pattern_size * sizeof(TCHAR));
    used += pattern_size;
  }
  va_end(arg);
  if (used) ofn.lpstrFilter = filter;

  ofn.lpstrFile = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, PATH_LENGTH * sizeof(TCHAR));
  if (!ofn.lpstrFile) return;
  if (flags & OFN_NOVALIDATE) {
    gui_message directory(NSSM_GUI_BROWSE_FILTER_DIRECTORIES);
    _sntprintf_s(ofn.lpstrFile, PATH_LENGTH, _TRUNCATE, _T(":%s:"), directory.get());
    ofn.nMaxFile = DIR_LENGTH;
  }
  else {
    if (current) _sntprintf_s(ofn.lpstrFile, PATH_LENGTH, _TRUNCATE, _T("%s"), current);
    else if (!GetWindowText(window, ofn.lpstrFile, PATH_LENGTH)) ofn.lpstrFile[0] = 0;
    ofn.nMaxFile = PATH_LENGTH;
  }
  gui_message title(NSSM_GUI_BROWSE_TITLE);
  ofn.lpstrTitle = title.get();
  ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | flags;
  if (GetOpenFileName(&ofn)) {
    if (flags & OFN_NOVALIDATE) strip_basename(ofn.lpstrFile);
    SendMessage(window, WM_SETTEXT, 0, (LPARAM) ofn.lpstrFile);
  }
  HeapFree(GetProcessHeap(), 0, ofn.lpstrFile);
}

static void fill_browse_default(HWND tab, int source, int destination, bool directory) {
  if (GetWindowTextLength(GetDlgItem(tab, destination))) return;
  text_buffer<PATH_LENGTH> path;
  if (!path.get() || !read_edit_text(tab, source, path.get(), path.size())) return;
  if (directory) strip_basename(path.get());
  SetDlgItemText(tab, destination, path.get());
}

INT_PTR CALLBACK tab_dlg(HWND tab, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_INITDIALOG:
      return 1;

    /* Button was pressed or control was controlled. */
    case WM_COMMAND:
      HWND dlg;
      unsigned char enabled;

      switch (LOWORD(w)) {
        /* Browse for application. */
        case IDC_BROWSE:
          dlg = GetDlgItem(tab, IDC_PATH);
          browse(dlg, NULL, OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_APPLICATIONS, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          fill_browse_default(tab, IDC_PATH, IDC_DIR, true);
          break;

        /* Browse for startup directory. */
        case IDC_BROWSE_DIR:
          dlg = GetDlgItem(tab, IDC_DIR);
          browse(dlg, NULL, OFN_NOVALIDATE, NSSM_GUI_BROWSE_FILTER_DIRECTORIES, 0);
          break;

        /* Log on. */
        case IDC_LOCALSYSTEM:
          set_logon_enabled(1, 0);
          break;

        case IDC_VIRTUAL_SERVICE:
          set_logon_enabled(0, 0);
          break;

        case IDC_ACCOUNT:
          set_logon_enabled(0, 1);
          break;

        /* Affinity. */
        case IDC_AFFINITY:
          if (HIWORD(w) == LBN_SELCHANGE) SetWindowLongPtr(tab, DWLP_USER, TRUE);
          break;
        case IDC_AFFINITY_ALL:
          if (HIWORD(w) == BN_CLICKED) SetWindowLongPtr(tab, DWLP_USER, TRUE);
          if (SendDlgItemMessage(tab, LOWORD(w), BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 0;
          else enabled = 1;
          set_affinity_enabled(enabled);
          break;

        /* Shutdown methods. */
        case IDC_METHOD_CONSOLE:
          set_timeout_enabled(LOWORD(w), IDC_KILL_CONSOLE);
          break;

        case IDC_METHOD_WINDOW:
          set_timeout_enabled(LOWORD(w), IDC_KILL_WINDOW);
          break;

        case IDC_METHOD_THREADS:
          set_timeout_enabled(LOWORD(w), IDC_KILL_THREADS);
          break;

        /* Browse for stdin. */
        case IDC_BROWSE_STDIN:
          dlg = GetDlgItem(tab, IDC_STDIN);
          browse(dlg, NULL, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Browse for stdout. */
        case IDC_BROWSE_STDOUT:
          dlg = GetDlgItem(tab, IDC_STDOUT);
          browse(dlg, NULL, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          fill_browse_default(tab, IDC_STDOUT, IDC_STDERR, false);
          break;

        /* Browse for stderr. */
        case IDC_BROWSE_STDERR:
          dlg = GetDlgItem(tab, IDC_STDERR);
          browse(dlg, NULL, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Rotation. */
        case IDC_ROTATE:
          if (SendDlgItemMessage(tab, LOWORD(w), BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 1;
          else enabled = 0;
          set_rotation_enabled(enabled);
          break;

        /* Hook event. */
        case IDC_HOOK_EVENT:
          if (HIWORD(w) == CBN_SELCHANGE) set_hook_tab((int) SendMessage(GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), 0);
          break;

        /* Hook action. */
        case IDC_HOOK_ACTION:
          if (HIWORD(w) == CBN_SELCHANGE) set_hook_tab((int) SendMessage(GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), (int) SendMessage(GetDlgItem(tab, IDC_HOOK_ACTION), CB_GETCURSEL, 0, 0));
          break;

        /* Browse for hook. */
        case IDC_BROWSE_HOOK:
          dlg = GetDlgItem(tab, IDC_HOOK);
          browse(dlg, _T(""), OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Hook. */
        case IDC_HOOK:
          if (HIWORD(w) == EN_CHANGE && selected_hook) {
            selected_hook->edited = true;
            selected_hook->read_failed = !read_edit_text(tab, IDC_HOOK, selected_hook->command, _countof(selected_hook->command));
          }
          break;
      }
      return 1;
  }

  return 0;
}

/* Install/remove dialogue callback */
INT_PTR CALLBACK nssm_dlg(HWND window, UINT message, WPARAM w, LPARAM l) {
  nssm_service_t *service;

  switch (message) {
    /* Creating the dialogue */
    case WM_INITDIALOG:
      service = (nssm_service_t *) l;

      SetFocus(GetDlgItem(window, IDC_NAME));

      HWND tabs;
      HWND combo;
      HWND list;
      int n;
      tabs = GetDlgItem(window, IDC_TAB1);
      if (! tabs) return 0;

      /* Set up tabs. */
      selected_tab = 0;

      /* Application tab. */
      insert_tab(tabs, NSSM_TAB_APPLICATION, service->native ? NSSM_GUI_TAB_NATIVE : NSSM_GUI_TAB_APPLICATION);
      if (service->native) {
        tablist[NSSM_TAB_APPLICATION] = dialog(MAKEINTRESOURCE(IDD_NATIVE), window, tab_dlg);
        EnableWindow(tablist[NSSM_TAB_APPLICATION], 0);
        EnableWindow(GetDlgItem(tablist[NSSM_TAB_APPLICATION], IDC_PATH), 0);
      }
      else tablist[NSSM_TAB_APPLICATION] = dialog(MAKEINTRESOURCE(IDD_APPLICATION), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_APPLICATION], SW_SHOW);

      /* Details tab. */
      insert_tab(tabs, NSSM_TAB_DETAILS, NSSM_GUI_TAB_DETAILS);
      tablist[NSSM_TAB_DETAILS] = dialog(MAKEINTRESOURCE(IDD_DETAILS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_DETAILS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_AUTOMATIC, (LPARAM) gui_message(NSSM_GUI_STARTUP_AUTOMATIC).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_DELAYED, (LPARAM) gui_message(NSSM_GUI_STARTUP_DELAYED).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_MANUAL, (LPARAM) gui_message(NSSM_GUI_STARTUP_MANUAL).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_DISABLED, (LPARAM) gui_message(NSSM_GUI_STARTUP_DISABLED).get());
      SendMessage(combo, CB_SETCURSEL, NSSM_STARTUP_AUTOMATIC, 0);

      /* Logon tab. */
      insert_tab(tabs, NSSM_TAB_LOGON, NSSM_GUI_TAB_LOGON);
      tablist[NSSM_TAB_LOGON] = dialog(MAKEINTRESOURCE(IDD_LOGON), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_LOGON], SW_HIDE);

      /* Set defaults. */
      CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_ACCOUNT, IDC_LOCALSYSTEM);
      set_logon_enabled(1, 0);

      /* Dependencies tab. */
      insert_tab(tabs, NSSM_TAB_DEPENDENCIES, NSSM_GUI_TAB_DEPENDENCIES);
      tablist[NSSM_TAB_DEPENDENCIES] = dialog(MAKEINTRESOURCE(IDD_DEPENDENCIES), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_DEPENDENCIES], SW_HIDE);

      /* Remaining tabs are only for services we manage. */
      if (service->native) return 1;

      /* Process tab. */
      insert_tab(tabs, NSSM_TAB_PROCESS, NSSM_GUI_TAB_PROCESS);
      tablist[NSSM_TAB_PROCESS] = dialog(MAKEINTRESOURCE(IDD_PROCESS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_PROCESS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
      SendMessage(combo, CB_INSERTSTRING, NSSM_REALTIME_PRIORITY, (LPARAM) gui_message(NSSM_GUI_REALTIME_PRIORITY_CLASS).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_HIGH_PRIORITY, (LPARAM) gui_message(NSSM_GUI_HIGH_PRIORITY_CLASS).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_ABOVE_NORMAL_PRIORITY, (LPARAM) gui_message(NSSM_GUI_ABOVE_NORMAL_PRIORITY_CLASS).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_NORMAL_PRIORITY, (LPARAM) gui_message(NSSM_GUI_NORMAL_PRIORITY_CLASS).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_BELOW_NORMAL_PRIORITY, (LPARAM) gui_message(NSSM_GUI_BELOW_NORMAL_PRIORITY_CLASS).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_IDLE_PRIORITY, (LPARAM) gui_message(NSSM_GUI_IDLE_PRIORITY_CLASS).get());
      SendMessage(combo, CB_SETCURSEL, NSSM_NORMAL_PRIORITY, 0);

      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, BST_CHECKED, 0);

      list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
      SendMessage(list, LB_SETCOLUMNWIDTH, 16, 0);
      n = populate_affinity_list(list, cpu_affinity_mask());

      /*
        Size to fit.
        The box is high enough for four rows.  It is wide enough for eight
        columns without scrolling.  With scrollbars it shrinks to two rows.
        Note that the above only holds if we set the column width BEFORE
        adding the strings.
      */
      if (n > 0 && n < 32) {
        int columns = (n - 1) / 4;
        RECT rect;
        GetWindowRect(list, &rect);
        int width = rect.right - rect.left;
        width -= (7 - columns) * 16;
        int height = rect.bottom - rect.top;
        if (n < 4) height -= (int) SendMessage(list, LB_GETITEMHEIGHT, 0, 0) * (4 - n);
        SetWindowPos(list, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOOWNERZORDER);
      }
      if (n) SendMessage(list, LB_SELITEMRANGE, 1, MAKELPARAM(0, n - 1));

      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, BST_CHECKED, 0);
      set_affinity_enabled(0);

      /* Shutdown tab. */
      insert_tab(tabs, NSSM_TAB_SHUTDOWN, NSSM_GUI_TAB_SHUTDOWN);
      tablist[NSSM_TAB_SHUTDOWN] = dialog(MAKEINTRESOURCE(IDD_SHUTDOWN), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_SHUTDOWN], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, NSSM_KILL_CONSOLE_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, NSSM_KILL_WINDOW_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, NSSM_KILL_THREADS_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, BST_CHECKED, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, BST_CHECKED, 1);

      /* Restart tab. */
      insert_tab(tabs, NSSM_TAB_EXIT, NSSM_GUI_TAB_EXIT);
      tablist[NSSM_TAB_EXIT] = dialog(MAKEINTRESOURCE(IDD_APPEXIT), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_EXIT], SW_HIDE);

      /* Set defaults. */
      SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, NSSM_RESET_THROTTLE_RESTART, 0);
      combo = GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_RESTART, (LPARAM) gui_message(NSSM_GUI_EXIT_RESTART).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_IGNORE, (LPARAM) gui_message(NSSM_GUI_EXIT_IGNORE).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_REALLY, (LPARAM) gui_message(NSSM_GUI_EXIT_REALLY).get());
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_UNCLEAN, (LPARAM) gui_message(NSSM_GUI_EXIT_UNCLEAN).get());
      SendMessage(combo, CB_SETCURSEL, NSSM_EXIT_RESTART, 0);
      SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, 0, 0);

      /* I/O tab. */
      insert_tab(tabs, NSSM_TAB_IO, NSSM_GUI_TAB_IO);
      tablist[NSSM_TAB_IO] = dialog(MAKEINTRESOURCE(IDD_IO), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_IO], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, BST_UNCHECKED, 0);

      /* Rotation tab. */
      insert_tab(tabs, NSSM_TAB_ROTATION, NSSM_GUI_TAB_ROTATION);
      tablist[NSSM_TAB_ROTATION] = dialog(MAKEINTRESOURCE(IDD_ROTATION), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_ROTATION], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, BST_UNCHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, 0, 0);
      SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, 0, 0);
      set_rotation_enabled(0);

      /* Environment tab. */
      insert_tab(tabs, NSSM_TAB_ENVIRONMENT, NSSM_GUI_TAB_ENVIRONMENT);
      tablist[NSSM_TAB_ENVIRONMENT] = dialog(MAKEINTRESOURCE(IDD_ENVIRONMENT), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_ENVIRONMENT], SW_HIDE);

      /* Hooks tab. */
      insert_tab(tabs, NSSM_TAB_HOOKS, NSSM_GUI_TAB_HOOKS);
      tablist[NSSM_TAB_HOOKS] = dialog(MAKEINTRESOURCE(IDD_HOOKS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_HOOKS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) gui_message(NSSM_GUI_HOOK_EVENT_START).get());
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) gui_message(NSSM_GUI_HOOK_EVENT_STOP).get());
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) gui_message(NSSM_GUI_HOOK_EVENT_EXIT).get());
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) gui_message(NSSM_GUI_HOOK_EVENT_POWER).get());
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) gui_message(NSSM_GUI_HOOK_EVENT_ROTATE).get());
      SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, BST_UNCHECKED, 0);
      selected_hook = NULL;
      SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_HOOK, EM_SETLIMITTEXT, CMD_LENGTH - 1, 0);
      for (size_t i = 0; i < _countof(hook_names); i++) {
        hook_draft_t &draft = hook_drafts[i];
        draft.edited = draft.read_failed = false;
        draft.type = REG_EXPAND_SZ;
        draft.command[0] = 0;
        if (service->name[0] && get_hook(service->name, hook_names[i].event, hook_names[i].action,
            draft.command, sizeof(draft.command), false, &draft.type)) draft.command[0] = 0;
      }
      set_hook_tab(0, 0);

      return 1;

    /* Tab change. */
    case WM_NOTIFY:
      NMHDR *notification;

      notification = (NMHDR *) l;
      switch (notification->code) {
        case TCN_SELCHANGE:
          HWND tabs;
          int selection;

          tabs = GetDlgItem(window, IDC_TAB1);
          if (! tabs) return 0;

          selection = (int) SendMessage(tabs, TCM_GETCURSEL, 0, 0);
          if (selection != selected_tab) {
            ShowWindow(tablist[selected_tab], SW_HIDE);
            ShowWindow(tablist[selection], SW_SHOWDEFAULT);
            SetFocus(GetDlgItem(window, IDOK));
            selected_tab = selection;
          }
          return 1;
      }

      return 0;

    /* Button was pressed or control was controlled */
    case WM_COMMAND:
      switch (LOWORD(w)) {
        /* OK button */
        case IDOK:
          if ((int) GetWindowLongPtr(window, GWLP_USERDATA) == IDD_EDIT) {
            if (! edit(window, (nssm_service_t *) GetWindowLongPtr(window, DWLP_USER))) PostQuitMessage(0);
          }
          else if (! install(window)) PostQuitMessage(0);
          break;

        /* Cancel button */
        case IDCANCEL:
          DestroyWindow(window);
          break;

        /* Remove button */
        case IDC_REMOVE:
          if (! remove(window)) PostQuitMessage(0);
          break;
      }
      return 1;

    /* Window closing */
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
  }
  return 0;
}
