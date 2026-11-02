#include "nssm.h"

extern imports_t imports;

void release_debug_token(debug_token_t *scope) {
  if (! scope) return;
  DWORD error = ERROR_SUCCESS;
  if (scope->token && scope->adjusted) {
    SetLastError(ERROR_SUCCESS);
    BOOL restored = AdjustTokenPrivileges(scope->token, false, &scope->previous, 0, NULL, NULL);
    DWORD result = GetLastError();
    if (! restored || result != ERROR_SUCCESS) error = result ? result : ERROR_ACCESS_DENIED;
  }
  if (scope->impersonated && ! RevertToSelf()) {
    DWORD result = GetLastError();
    error = result ? result : ERROR_ACCESS_DENIED;
  }
  if (scope->token) CloseHandle(scope->token);
  ZeroMemory(scope, sizeof(*scope));
  /* Never continue with an identity/privilege state we failed to restore. */
  if (error) {
    _ftprintf(stderr, _T("Failed to restore debug-token state: %s\n"), error_string(error));
    nssm_exit(111);
  }
}

bool get_debug_token(debug_token_t *scope) {
  if (! scope) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
  ZeroMemory(scope, sizeof(*scope));
  DWORD error, size = sizeof(scope->previous);
  TOKEN_PRIVILEGES requested;
  ZeroMemory(&requested, sizeof(requested));
  if (! OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, false, &scope->token)) {
    error = GetLastError();
    scope->token = NULL;
    if (error != ERROR_NO_TOKEN) return false;
    if (! ImpersonateSelf(SecurityImpersonation)) return false;
    scope->impersonated = true;
    if (! OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, false, &scope->token)) {
      scope->token = NULL;
      goto failed;
    }
  }

  if (! LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &requested.Privileges[0].Luid)) goto failed;
  requested.PrivilegeCount = 1;
  requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  if (! AdjustTokenPrivileges(scope->token, false, &requested, size, &scope->previous, &size)) goto failed;
  scope->adjusted = scope->previous.PrivilegeCount != 0;
  if (GetLastError() != ERROR_SUCCESS) goto failed;
  return true;

failed:
  error = GetLastError();
  release_debug_token(scope);
  SetLastError(error);
  return false;
}

void service_kill_t(nssm_service_t *service, kill_t *k) {
  if (! service) return;
  if (! k) return;

  ZeroMemory(k, sizeof(*k));
  k->name = service->name;
  k->process_handle = service->process_handle;
  k->pid = service->pid;
  k->exitcode = service->exitcode;
  k->stop_method = service->stop_method;
  k->kill_console_delay = service->kill_console_delay;
  k->kill_window_delay = service->kill_window_delay;
  k->kill_threads_delay = service->kill_threads_delay;
  k->status_handle = service->status_handle;
  k->status = &service->status;
  k->creation_time = service->creation_time;
  k->exit_time = service->exit_time;
}

int get_process_creation_time(HANDLE process_handle, FILETIME *ft) {
  FILETIME creation_time, exit_time, kernel_time, user_time;

  if (! GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
    return 1;
  }

  memmove(ft, &creation_time, sizeof(creation_time));

  return 0;
}

int get_process_exit_time(HANDLE process_handle, FILETIME *ft) {
  FILETIME creation_time, exit_time, kernel_time, user_time;

  if (! GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
    return 1;
  }

  if (! (exit_time.dwLowDateTime || exit_time.dwHighDateTime)) return 2;
  memmove(ft, &exit_time, sizeof(exit_time));

  return 0;
}

typedef DWORD (WINAPI *process_id_function_t)(HANDLE);

/* Resolve optional identity APIs without adding a newer Windows import. */
static process_id_function_t process_id_function(const char *name) {
  HMODULE module = GetModuleHandle(_T("kernel32.dll"));
  return module ? (process_id_function_t) GetProcAddress(module, name) : NULL;
}

static bool has_time(const FILETIME *ft) {
  return ft->dwLowDateTime || ft->dwHighDateTime;
}

/* The caller must keep this handle open throughout validation and use. */
static bool process_identity(kill_t *k) {
  if (! k || ! k->pid || ! k->process_handle || ! has_time(&k->creation_time)) return false;
  process_id_function_t id = process_id_function("GetProcessId");
  if (! id || id(k->process_handle) != k->pid) return false;
  FILETIME born;
  return ! get_process_creation_time(k->process_handle, &born) && ! CompareFileTime(&born, &k->creation_time);
}

static bool process_running(kill_t *k) {
  return process_identity(k) && WaitForSingleObject(k->process_handle, 0) == WAIT_TIMEOUT;
}

/* Send some window messages and hope the window respects one or more. */
int CALLBACK kill_window(HWND window, LPARAM arg) {
  kill_t *k = (kill_t *) arg;
  if (! process_running(k)) return 1;

  unsigned long pid;
  DWORD thread = GetWindowThreadProcessId(window, &pid);
  if (! thread) return 1;
  if (pid != k->pid) return 1;

  /* First try sending WM_CLOSE to request that the window close. */
  k->signalled |= PostMessage(window, WM_CLOSE, k->exitcode, 0);

  /* A foreign HWND can be destroyed/recycled between messages. Recheck it. */
  if (! process_running(k) || GetWindowThreadProcessId(window, &pid) != thread || pid != k->pid) return 1;

  /*
    Then tell the window that the user is logging off and it should exit
    without worrying about saving any data.
  */
  k->signalled |= PostMessage(window, WM_ENDSESSION, 1, ENDSESSION_CLOSEAPP | ENDSESSION_CRITICAL | ENDSESSION_LOGOFF);

  return 1;
}

/*
  Try to post a message to the message queues of threads associated with the
  given process ID.  Not all threads have message queues so there's no
  guarantee of success, and we don't want to be left waiting for unsignalled
  processes so this function returns only true if at least one thread was
  successfully prodded.
*/
int kill_threads(nssm_service_t *service, kill_t *k) {
  int ret = 0;
  if (! process_running(k)) return 0;
  process_id_function_t owner = process_id_function("GetProcessIdOfThread");
  if (! owner) return 0;
  FILETIME cutoff;
  GetSystemTimeAsFileTime(&cutoff);

  /* Get a snapshot of all threads in the system. */
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_THREAD_FAILED, k->name, error_string(GetLastError()), 0);
    return 0;
  }

  THREADENTRY32 te;
  ZeroMemory(&te, sizeof(te));
  te.dwSize = sizeof(te);

  if (! Thread32First(snapshot, &te)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
    CloseHandle(snapshot);
    return 0;
  }

  while (true) {
    if (te.th32OwnerProcessID == k->pid && process_running(k)) {
      HANDLE thread = OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, false, te.th32ThreadID);
      if (thread) {
        FILETIME born, exited, kernel, user;
        if (owner(thread) == k->pid && GetThreadTimes(thread, &born, &exited, &kernel, &user) &&
            CompareFileTime(&born, &k->creation_time) >= 0 && CompareFileTime(&born, &cutoff) <= 0 &&
            WaitForSingleObject(thread, 0) == WAIT_TIMEOUT && process_running(k)) {
          ret |= PostThreadMessage(te.th32ThreadID, WM_QUIT, k->exitcode, 0);
        }
        CloseHandle(thread);
      }
    }
    /* Try to get the next thread. */
    if (! Thread32Next(snapshot, &te)) {
      unsigned long error = GetLastError();
      if (error == ERROR_NO_MORE_FILES) break;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, k->name, error_string(GetLastError()), 0);
      CloseHandle(snapshot);
      return ret;
    }
  }

  CloseHandle(snapshot);

  return ret;
}

int kill_threads(kill_t *k) {
  return kill_threads(NULL, k);
}

/* Give the process a chance to die gracefully. */
int kill_process(nssm_service_t *service, kill_t *k) {
  if (! process_identity(k)) { SetLastError(ERROR_INVALID_DATA); return 0; }
  DWORD state = WaitForSingleObject(k->process_handle, 0);
  if (state == WAIT_OBJECT_0) return 1;
  if (state != WAIT_TIMEOUT) return 0;

  /* Try to send a Control-C event to the console. */
  if (k->stop_method & NSSM_STOP_METHOD_CONSOLE) {
    if (! kill_console(k)) return 1;
  }

  /*
    Try to post messages to the windows belonging to the given process ID.
    If the process is a console application it won't have any windows so there's
    no guarantee of success.
  */
  if (k->stop_method & NSSM_STOP_METHOD_WINDOW) {
    EnumWindows((WNDENUMPROC) kill_window, (LPARAM) k);
    if (k->signalled) {
      if (! await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_window_delay)) return 1;
      k->signalled = 0;
    }
  }

  /*
    Try to post messages to any thread message queues associated with the
    process.  Console applications might have them (but probably won't) so
    there's still no guarantee of success.
  */
  if (k->stop_method & NSSM_STOP_METHOD_THREADS) {
    if (kill_threads(k)) {
      if (! await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_threads_delay)) return 1;
    }
  }

  /* We tried being nice.  Time for extreme prejudice. */
  if (k->stop_method & NSSM_STOP_METHOD_TERMINATE) {
    return TerminateProcess(k->process_handle, k->exitcode);
  }

  return 0;
}

int kill_process(kill_t *k) {
  return kill_process(NULL, k);
}

/* Simulate a Control-C event to our console (shared with the app). */
int kill_console(nssm_service_t *service, kill_t *k) {
  unsigned long ret;

  if (! k) return 1;
  if (! process_running(k)) return 2;

  /* Check we loaded AttachConsole(). */
  if (! imports.AttachConsole) return 4;

  /* Try to attach to the process's console. */
  if (! imports.AttachConsole(k->pid)) {
    ret = GetLastError();

    switch (ret) {
      case ERROR_INVALID_HANDLE:
        /* The app doesn't have a console. */
        return 1;

      case ERROR_GEN_FAILURE:
        /* The app already exited. */
        return 2;

      case ERROR_ACCESS_DENIED:
      default:
        /* We already have a console. */
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ATTACHCONSOLE_FAILED, k->name, error_string(ret), 0);
        return 3;
    }
  }

  /* Ignore the event ourselves. */
  ret = 0;
  BOOL ignored = SetConsoleCtrlHandler(0, TRUE);
  if (! ignored) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETCONSOLECTRLHANDLER_FAILED, k->name, error_string(GetLastError()), 0);
    ret = 4;
  }

  /* Send the event. */
  if (! ret && ! process_running(k)) ret = 2;
  if (! ret) {
    if (! GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GENERATECONSOLECTRLEVENT_FAILED, k->name, error_string(GetLastError()), 0);
      ret = 5;
    }
  }

  /* Detach from the console. */
  if (! FreeConsole()) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_FREECONSOLE_FAILED, k->name, error_string(GetLastError()), 0);
  }

  /* Wait for process to exit. */
  if (await_single_handle(k->status_handle, k->status, k->process_handle, k->name, _T(__FUNCTION__), k->kill_console_delay)) ret = 6;

  /* Remove our handler. */
  if (ignored && ! SetConsoleCtrlHandler(0, FALSE)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETCONSOLECTRLHANDLER_FAILED, k->name, error_string(GetLastError()), 0);
  }

  return ret;
}

int kill_console(kill_t *k) {
  return kill_console(NULL, k);
}

static unsigned long tree_failure(kill_t *k, DWORD error) {
  TCHAR pid[16];
  _sntprintf_s(pid, _countof(pid), _TRUNCATE, _T("%lu"), k ? k->pid : 0);
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_TREE_SKIPPED, pid, k && k->name ? k->name : NSSM, error_string(error), 0);
  return 1;
}

/*
  Each frame borrows one pinned process handle and owns its snapshot and child
  handles. No PID is reopened after validation. Keep the parent's handle until
  all descendants have been considered, including after the parent exits.
*/
static unsigned long walk_process_node(nssm_service_t *service, walk_function_t fn, kill_t *k, unsigned long ppid, bool child_node) {
  if (! process_identity(k)) return tree_failure(k, ERROR_INVALID_DATA);
  if (k->depth >= 128) return tree_failure(k, ERROR_STACK_OVERFLOW);

  FILETIME cutoff;
  GetSystemTimeAsFileTime(&cutoff);
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_PROCESS_FAILED, k->name, error_string(GetLastError()), 0);
    return 1;
  }

  PROCESSENTRY32 pe;
  ZeroMemory(&pe, sizeof(pe));
  pe.dwSize = sizeof(pe);
  /* Confirm the parent relation in a fresh snapshot taken AFTER pinning the
     child. This also rejects replacements created within one clock tick. */
  if (child_node) {
    bool found = false;
    if (Process32First(snapshot, &pe)) {
      do {
        if (pe.th32ProcessID == k->pid) {
          found = pe.th32ParentProcessID == ppid;
          break;
        }
      } while (Process32Next(snapshot, &pe));
    }
    if (! found) {
      CloseHandle(snapshot);
      return tree_failure(k, ERROR_INVALID_DATA);
    }
  }

  unsigned long errors = 0;
  if (fn == kill_process) {
    TCHAR pid[16], parent[16], code[16];
    _sntprintf_s(pid, _countof(pid), _TRUNCATE, _T("%lu"), k->pid);
    _sntprintf_s(parent, _countof(parent), _TRUNCATE, _T("%lu"), ppid);
    _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), k->exitcode);
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILLING, k->name, pid, code, 0);
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILL_PROCESS_TREE, pid, parent, k->name, 0);
  }
  if (! fn(service, k)) {
    DWORD error = GetLastError();
    if (WaitForSingleObject(k->process_handle, 0) != WAIT_OBJECT_0) {
      TCHAR pid[16];
      _sntprintf_s(pid, _countof(pid), _TRUNCATE, _T("%lu"), k->pid);
      if (fn == kill_process && (k->stop_method & NSSM_STOP_METHOD_TERMINATE))
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_TERMINATEPROCESS_FAILED, pid, k->name, error_string(error ? error : ERROR_PROCESS_ABORTED), 0);
      else if (fn == kill_process)
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_PROCESS_STILL_ACTIVE, k->name, pid, NSSM, NSSM_REG_STOP_METHOD_SKIP, 0);
      else tree_failure(k, error ? error : ERROR_PROCESS_ABORTED);
      errors++;
    }
  }

  /* Use this immediate parent's actual lifetime, not the root's timestamps. */
  FILETIME kernel, user;
  if (! GetProcessTimes(k->process_handle, &k->creation_time, &k->exit_time, &kernel, &user)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
    CloseHandle(snapshot);
    return errors + 1;
  }

  if (Process32First(snapshot, &pe)) {
    do {
      if (pe.th32ParentProcessID != k->pid || pe.th32ProcessID == k->pid) continue;
      kill_t child = *k;
      child.pid = pe.th32ProcessID;
      child.depth = k->depth + 1;
      child.signalled = 0;
      DWORD access = SYNCHRONIZE | PROCESS_QUERY_INFORMATION;
      access |= fn == kill_process ? PROCESS_TERMINATE : PROCESS_VM_READ;
      child.process_handle = OpenProcess(access, false, child.pid);
      if (! child.process_handle) {
        TCHAR pid[16];
        _sntprintf_s(pid, _countof(pid), _TRUNCATE, _T("%lu"), child.pid);
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid, k->name, error_string(GetLastError()), 0);
        errors++;
        continue;
      }
      if (get_process_creation_time(child.process_handle, &child.creation_time) ||
          CompareFileTime(&child.creation_time, &k->creation_time) < 0 ||
          CompareFileTime(&child.creation_time, &cutoff) > 0 ||
          (has_time(&k->exit_time) && CompareFileTime(&child.creation_time, &k->exit_time) > 0)) {
        errors += tree_failure(&child, ERROR_INVALID_DATA);
      }
      else errors += walk_process_node(service, fn, &child, k->pid, true);
      CloseHandle(child.process_handle);
    } while (Process32Next(snapshot, &pe));
  }
  DWORD error = GetLastError();
  if (error != ERROR_NO_MORE_FILES) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, k->name, error_string(error), 0);
    errors++;
  }
  CloseHandle(snapshot);
  return errors;
}

unsigned long walk_process_tree(nssm_service_t *service, walk_function_t fn, kill_t *k, unsigned long ppid) {
  if (! k || ! fn) return tree_failure(k, ERROR_INVALID_PARAMETER);
  kill_t root = *k;
  return walk_process_node(service, fn, &root, ppid, false);
}

unsigned long kill_process_tree(kill_t *k, unsigned long ppid) {
  return walk_process_tree(NULL, kill_process, k, ppid);
}

int print_process(nssm_service_t *service, kill_t *k) {
  TCHAR exe[EXE_LENGTH];
  TCHAR *buffer = 0;
  if (k->depth) {
    buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (k->depth + 1) * sizeof(TCHAR));
    if (buffer) {
      unsigned long i;
      for (i = 0; i < k->depth; i++) buffer[i] = _T(' ');
      buffer[i] = _T('\0');
    }
  }

  unsigned long size = _countof(exe);
  if (! imports.QueryFullProcessImageName || ! imports.QueryFullProcessImageName(k->process_handle, 0, exe, &size)) {
    /*
      Fall back to GetModuleFileNameEx(), which won't work for WOW64 processes.
    */
    if (! GetModuleFileNameEx(k->process_handle, NULL, exe, _countof(exe))) {
      long error = GetLastError();
      if (error == ERROR_PARTIAL_COPY) _sntprintf_s(exe, _countof(exe), _TRUNCATE, _T("[WOW64]"));
      else _sntprintf_s(exe, _countof(exe), _TRUNCATE, _T("???"));
    }
  }

  _tprintf(_T("% 8lu %s%s\n"), k->pid, buffer ? buffer : _T(""), exe);

  if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
  return 1;
}

int print_process(kill_t *k) {
  return print_process(NULL, k);
}
