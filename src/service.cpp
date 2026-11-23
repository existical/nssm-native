#include "nssm.h"

bool is_admin;

extern imports_t imports;
extern settings_t settings[];

const TCHAR *exit_action_strings[] = { _T("Restart"), _T("Ignore"), _T("Exit"), _T("Suicide"), 0 };
const TCHAR *startup_strings[] = { _T("SERVICE_AUTO_START"), _T("SERVICE_DELAYED_AUTO_START"), _T("SERVICE_DEMAND_START"), _T("SERVICE_DISABLED"), 0 };
const TCHAR *priority_strings[] = { _T("REALTIME_PRIORITY_CLASS"), _T("HIGH_PRIORITY_CLASS"), _T("ABOVE_NORMAL_PRIORITY_CLASS"), _T("NORMAL_PRIORITY_CLASS"), _T("BELOW_NORMAL_PRIORITY_CLASS"), _T("IDLE_PRIORITY_CLASS"), 0 };

static hook_thread_t hook_threads = {};

/* Serialize launch publication, monitoring and cleanup with the service-side
   hook/logger lock. Workers own snapshots and do not acquire this lock.
   Critical sections are recursive: STOP may call end_service while owning it. */
class service_cleanup_lock {
  CRITICAL_SECTION *section;
  bool owned;
public:
  explicit service_cleanup_lock(nssm_service_t *service) : section(service->hook_section_initialised ? &service->hook_section : NULL), owned(false) { lock(); }
  void lock() { if (section && ! owned) { EnterCriticalSection(section); owned = true; } }
  void unlock() { if (owned) { LeaveCriticalSection(section); owned = false; } }
  ~service_cleanup_lock() { unlock(); }
private:
  service_cleanup_lock(const service_cleanup_lock &);
  service_cleanup_lock &operator=(const service_cleanup_lock &);
};

static bool restart_allowed(nssm_service_t *service) {
  return InterlockedCompareExchange(&service->allow_restart, 0, 0) != 0;
}

static bool status_pending(DWORD state) {
  return state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ||
         state == SERVICE_PAUSE_PENDING || state == SERVICE_CONTINUE_PENDING;
}

static void report_service_status(nssm_service_t *service) {
  SERVICE_STATUS &status = service->status;
  if (!status_pending(status.dwCurrentState)) {
    status.dwCheckPoint = 0;
    status.dwWaitHint = 0;
  }
  if (status.dwCurrentState == SERVICE_STOPPED) status.dwControlsAccepted = 0;
  SetServiceStatus(service->status_handle, &status);
}

/* The caller owns a separate SYNCHRONIZE handle. Never hold the cleanup lock
   across the wait: STOP must be able to terminate the child and wake us. */
enum class startup_result { alive, exited, cancelled, failed };

static startup_result await_startup(nssm_service_t *service, HANDLE process, unsigned long timeout) {
  unsigned long waited = 0;
  do {
    unsigned long interval = timeout - waited;
    if (interval > NSSM_SERVICE_STATUS_DEADLINE) interval = NSSM_SERVICE_STATUS_DEADLINE;
    {
      service_cleanup_lock state(service);
      if (! restart_allowed(service) || service->stopping) return startup_result::cancelled;
      if (interval && status_pending(service->status.dwCurrentState)) {
        service->status.dwWaitHint = interval + NSSM_WAITHINT_MARGIN;
        service->status.dwCheckPoint++;
        report_service_status(service);
      }
    }
    DWORD result = WaitForSingleObject(process, interval);
    if (result == WAIT_OBJECT_0) return startup_result::exited;
    if (result != WAIT_TIMEOUT) return startup_result::failed;
    waited += interval;
  } while (waited < timeout);
  return startup_result::alive;
}

typedef struct {
  int first;
  int last;
} list_t;

/*
  Check the status in response to a control.
  Returns:  1 if the status is expected, eg STOP following CONTROL_STOP.
            0 if the status is desired, eg STOPPED following CONTROL_STOP.
           -1 if the status is undesired, eg STOPPED following CONTROL_START.
*/
static inline int service_control_response(unsigned long control, unsigned long status) {
  switch (control) {
    case NSSM_SERVICE_CONTROL_START:
      switch (status) {
        case SERVICE_START_PENDING:
          return 1;

        case SERVICE_RUNNING:
          return 0;

        default:
          return -1;
      }

    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      switch (status) {
        case SERVICE_RUNNING:
        case SERVICE_STOP_PENDING:
          return 1;

        case SERVICE_STOPPED:
          return 0;

        default:
          return -1;
      }

    case SERVICE_CONTROL_PAUSE:
      switch (status) {
        case SERVICE_PAUSE_PENDING:
          return 1;

        case SERVICE_PAUSED:
          return 0;

        default:
          return -1;
      }

    case SERVICE_CONTROL_CONTINUE:
      switch (status) {
        case SERVICE_CONTINUE_PENDING:
        case SERVICE_START_PENDING:
          return 1;

        case SERVICE_RUNNING:
          return 0;

        default:
          return -1;
      }

    case SERVICE_CONTROL_INTERROGATE:
    case NSSM_SERVICE_CONTROL_ROTATE:
      return 0;
  }

  return 0;
}

enum service_wait_outcome {
  SERVICE_WAIT_COMPLETE, SERVICE_WAIT_FAILED, SERVICE_WAIT_QUERY_ERROR,
  SERVICE_WAIT_STALLED, SERVICE_WAIT_DEADLINE
};

struct service_wait_result {
  service_wait_outcome outcome;
  unsigned long error;
  service_wait_result(service_wait_outcome result, unsigned long code) : outcome(result), error(code) {}
};

/* A changed wait hint alone is not progress. Keep a separate total deadline,
   including when a service continuously advances checkpoints or changes state. */
static service_wait_result await_service_control_response(unsigned long control, SC_HANDLE service_handle,
  SERVICE_STATUS *status, unsigned long initial_status, unsigned long timeout) {
  ULONGLONG started = monotonic_milliseconds();
  ULONGLONG progressed = started;
  unsigned long checkpoint = 0, state = 0, progress_timeout = 0;
  bool first = true;

  while (true) {
    if (! QueryServiceStatus(service_handle, status)) return service_wait_result(SERVICE_WAIT_QUERY_ERROR, GetLastError());
    if (control == SERVICE_CONTROL_CONTINUE && status->dwCurrentState == SERVICE_START_PENDING && status->dwWin32ExitCode)
      return service_wait_result(SERVICE_WAIT_FAILED, status->dwWin32ExitCode);
    int response = service_control_response(control, status->dwCurrentState);
    if (! response) {
      /* A stopped service can still report a failed shutdown. */
      if (status->dwCurrentState == SERVICE_STOPPED && status->dwWin32ExitCode)
        return service_wait_result(SERVICE_WAIT_FAILED, status->dwWin32ExitCode);
      return service_wait_result(SERVICE_WAIT_COMPLETE, ERROR_SUCCESS);
    }
    bool initial = status->dwCurrentState == initial_status &&
      ((control == SERVICE_CONTROL_PAUSE && initial_status == SERVICE_RUNNING) ||
       (control == SERVICE_CONTROL_CONTINUE && initial_status == SERVICE_PAUSED) ||
       ((control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) && initial_status == SERVICE_PAUSED));
    if (response < 0 && ! initial) {
      unsigned long error = status->dwWin32ExitCode;
      if (! error) error = status->dwCurrentState == SERVICE_STOPPED ? ERROR_SERVICE_NOT_ACTIVE : ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
      return service_wait_result(SERVICE_WAIT_FAILED, error);
    }

    ULONGLONG now = monotonic_milliseconds();
    if (first || status->dwCurrentState != state || status->dwCheckPoint > checkpoint) {
      first = false;
      checkpoint = status->dwCheckPoint;
      state = status->dwCurrentState;
      progressed = now;
      progress_timeout = status->dwWaitHint;
      if (! progress_timeout) progress_timeout = 10000;
      if (progress_timeout < 1000) progress_timeout = 1000;
      if (progress_timeout > timeout) progress_timeout = timeout;
    }

    ULONGLONG elapsed = now - started;
    ULONGLONG stalled = now - progressed;
    if (elapsed >= timeout) return service_wait_result(SERVICE_WAIT_DEADLINE, ERROR_TIMEOUT);
    if (stalled >= progress_timeout) return service_wait_result(SERVICE_WAIT_STALLED, ERROR_TIMEOUT);

    unsigned long wait = progress_timeout / 10;
    if (wait < 100) wait = 100;
    if (wait > 1000) wait = 1000;
    if (wait > timeout - elapsed) wait = (DWORD)(timeout - elapsed);
    if (wait > progress_timeout - stalled) wait = (DWORD)(progress_timeout - stalled);
    Sleep(wait);
  }
}

/* START keeps its independent 60-second contract and requires RUNNING. */
static unsigned long await_service_start(SC_HANDLE service_handle, SERVICE_STATUS *status) {
  return await_service_control_response(NSSM_SERVICE_CONTROL_START, service_handle, status,
    SERVICE_START_PENDING, NSSM_STARTUP_TIMEOUT).error;
}

static unsigned long control_wait_setting(HKEY key, const TCHAR *name, unsigned long fallback) {
  unsigned long value, type, bytes = sizeof(value);
  if (RegQueryValueEx(key, name, NULL, &type, (unsigned char *) &value, &bytes) != ERROR_SUCCESS ||
      type != REG_DWORD || bytes != sizeof(value)) return fallback;
  return value;
}

/* Registry access is optional and read-only: a restricted/native service can
   still be controlled. This is a finite CLI policy, not a shutdown guarantee
   for an unbounded number of descendants or outstanding hook workers. */
static unsigned long service_control_timeout(unsigned long control, const TCHAR *service_name) {
  if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_SHUTDOWN) return NSSM_CONTROL_TIMEOUT;
  TCHAR path[NSSM_SERVICE_KEY_PATH_LENGTH];
  HKEY key;
  if (_sntprintf_s(path, _countof(path), _TRUNCATE, NSSM_REGISTRY _T("\\") NSSM_REG_PARAMETERS, service_name) < 0 ||
      RegOpenKeyEx(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return NSSM_STOP_TIMEOUT;
  unsigned long type = 0, bytes = 0;
  long error = RegQueryValueEx(key, NSSM_REG_EXE, NULL, &type, NULL, &bytes);
  if (error != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes <= sizeof(TCHAR)) {
    RegCloseKey(key);
    return NSSM_STOP_TIMEOUT;
  }

  unsigned long skip = control_wait_setting(key, NSSM_REG_STOP_METHOD_SKIP, 0);
  /* Stop/Pre, outstanding hooks, both loggers, and SCM/cleanup margin. */
  unsigned __int64 budget = 30000ULL + NSSM_SERVICE_STATUS_DEADLINE + NSSM_HOOK_THREAD_DEADLINE +
    2ULL * NSSM_CLEANUP_LOGGERS_DEADLINE;
  if (! (skip & NSSM_STOP_METHOD_CONSOLE)) budget += control_wait_setting(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, NSSM_KILL_CONSOLE_GRACE_PERIOD);
  if (! (skip & NSSM_STOP_METHOD_WINDOW)) budget += control_wait_setting(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, NSSM_KILL_WINDOW_GRACE_PERIOD);
  if (! (skip & NSSM_STOP_METHOD_THREADS)) budget += control_wait_setting(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, NSSM_KILL_THREADS_GRACE_PERIOD);
  RegCloseKey(key);
  if (budget < NSSM_STOP_TIMEOUT) budget = NSSM_STOP_TIMEOUT;
  if (budget > NSSM_STOP_TIMEOUT_MAX) budget = NSSM_STOP_TIMEOUT_MAX;
  return (unsigned long) budget;
}

static inline void wait_for_hooks(nssm_service_t *service, bool notify) {
  SERVICE_STATUS_HANDLE status_handle;
  SERVICE_STATUS *status;

  /* On a clean shutdown we need to keep the service's status up-to-date. */
  if (notify) {
    status_handle = service->status_handle;
    status = &service->status;
  }
  else {
    status_handle = NULL;
    status = NULL;
  }

  EnterCriticalSection(&service->hook_section);
  await_hook_threads(&hook_threads, status_handle, status, NSSM_HOOK_THREAD_DEADLINE);
  LeaveCriticalSection(&service->hook_section);
}

int affinity_mask_to_string(__int64 mask, TCHAR **string) {
  if (! string) return 1;
  *string = 0;
  if (! mask) return 0;

  __int64 i, n;

  /* SetProcessAffinityMask() accepts a mask of up to 64 processors. */
  list_t set[64];
  for (n = 0; n < _countof(set); n++) set[n].first = set[n].last = -1;

  for (i = 0, n = 0; i < _countof(set); i++) {
    if (((unsigned __int64) mask) & (1ULL << i)) {
      if (set[n].first == -1) set[n].first = set[n].last = (int) i;
      else if (set[n].last == (int) i - 1) set[n].last = (int) i;
      else {
        n++;
        set[n].first = set[n].last = (int) i;
      }
    }
  }

  /* Worst case is 2x2 characters for first and last CPU plus - and/or , */
  size_t len = (size_t) (n + 1) * 6;
  *string = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! *string) return 2;

  size_t s = 0;
  int ret;
  for (i = 0; i <= n; i++) {
    if (i) (*string)[s++] = _T(',');
    ret = _sntprintf_s(*string + s, 3, _TRUNCATE, _T("%d"), set[i].first);
    if (ret < 0) {
      HeapFree(GetProcessHeap(), 0, *string);
      *string = 0;
      return 3;
    }
    else s += ret;
    if (set[i].last != set[i].first) {
      ret =_sntprintf_s(*string + s, 4, _TRUNCATE, _T("%c%d"), (set[i].last == set[i].first + 1) ? _T(',') : _T('-'), set[i].last);
      if (ret < 0) {
        HeapFree(GetProcessHeap(), 0, *string);
        *string = 0;
        return 4;
      }
      else s += ret;
    }
  }

  return 0;
}

int affinity_string_to_mask(TCHAR *string, __int64 *mask) {
  if (! mask) return 1;
  *mask = 0;
  if (! string) return 0;
  if (! *string) return 3;

  unsigned __int64 parsed = 0;
  TCHAR *s = string;
  while (*s) {
    TCHAR *end;
    unsigned long first, last;
    int ret = str_number(s, &first, &end);
    if (ret != 0 && ret != 2) return 4;
    if (first >= 64) return 2;
    last = first;
    s = end;
    if (*s == _T('-')) {
      ret = str_number(s + 1, &last, &end);
      if (ret != 0 && ret != 2) return 3;
      if (last >= 64) return 2;
      if (last < first) return 3;
      s = end;
    }
    if (*s && *s != _T(',')) return 3;
    for (unsigned long cpu = first; cpu <= last; cpu++) parsed |= 1ULL << cpu;
    if (! *s) break;
    if (! *(++s)) return 3;
  }
  *mask = (__int64) parsed;
  return 0;
}

unsigned long priority_mask() {
 return REALTIME_PRIORITY_CLASS | HIGH_PRIORITY_CLASS | ABOVE_NORMAL_PRIORITY_CLASS | NORMAL_PRIORITY_CLASS | BELOW_NORMAL_PRIORITY_CLASS | IDLE_PRIORITY_CLASS;
}

int priority_constant_to_index(unsigned long constant) {
  switch (constant & priority_mask()) {
    case REALTIME_PRIORITY_CLASS: return NSSM_REALTIME_PRIORITY;
    case HIGH_PRIORITY_CLASS: return NSSM_HIGH_PRIORITY;
    case ABOVE_NORMAL_PRIORITY_CLASS: return NSSM_ABOVE_NORMAL_PRIORITY;
    case BELOW_NORMAL_PRIORITY_CLASS: return NSSM_BELOW_NORMAL_PRIORITY;
    case IDLE_PRIORITY_CLASS: return NSSM_IDLE_PRIORITY;
  }
  return NSSM_NORMAL_PRIORITY;
}

unsigned long priority_index_to_constant(int index) {
  switch (index) {
    case NSSM_REALTIME_PRIORITY: return REALTIME_PRIORITY_CLASS;
    case NSSM_HIGH_PRIORITY: return HIGH_PRIORITY_CLASS;
    case NSSM_ABOVE_NORMAL_PRIORITY: return ABOVE_NORMAL_PRIORITY_CLASS;
    case NSSM_BELOW_NORMAL_PRIORITY: return BELOW_NORMAL_PRIORITY_CLASS;
    case NSSM_IDLE_PRIORITY: return IDLE_PRIORITY_CLASS;
  }
  return NORMAL_PRIORITY_CLASS;
}

static inline unsigned long throttle_milliseconds(unsigned long throttle) {
  if (throttle > 7) throttle = 8;
  /* pow() operates on doubles. */
  unsigned long ret = 1; for (unsigned long i = 1; i < throttle; i++) ret *= 2;
  return ret * 1000;
}

int set_service_environment(nssm_service_t *service) {
  if (!service) return 1;
  if (service->environment_restore_failed) {
    unset_service_environment(service);
    if (service->environment_restore_failed) return 1;
  }
  if (service->env && (clear_environment() || set_environment_block(service->env))) return 1;
  return service->env_extra ? set_environment_block(service->env_extra) : 0;
}

void unset_service_environment(nssm_service_t *service) {
  if (service) service->environment_restore_failed = duplicate_environment_strings(service->initial_env) != 0;
}

/* A single consumer preserves dispatch order. Posting to the completion port
   never takes the service lock or waits for hook setup. */
static DWORD WINAPI dispatch_power_hooks(void *context) {
  nssm_service_t *service = (nssm_service_t *) context;
  DWORD generation;
  ULONG_PTR event;
  OVERLAPPED *unused;
  for (;;) {
    /* Shutdown can still finish if posting its final wakeup fails. */
    if (!GetQueuedCompletionStatus(service->power_port, &generation, &event, &unused, NSSM_SERVICE_STATUS_DEADLINE)) {
      if (GetLastError() == WAIT_TIMEOUT && restart_allowed(service)) continue;
      break;
    }
    if (!event) break;
    service_cleanup_lock state(service);
    if (!restart_allowed(service) || service->stopping || generation != (DWORD) service->start_count) continue;
    unsigned long control = SERVICE_CONTROL_POWEREVENT;
    service->last_control = control;
    TCHAR *action = event == PBT_APMRESUMEAUTOMATIC ? NSSM_HOOK_ACTION_RESUME : NSSM_HOOK_ACTION_CHANGE;
    nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_POWER, action, &control);
  }
  return 0;
}

static DWORD WINAPI rotate_service(void *context) {
  nssm_service_t *service = (nssm_service_t *) context;
  unsigned long control = NSSM_SERVICE_CONTROL_ROTATE;
  unsigned long generation = service->rotate_generation;
  {
    service_cleanup_lock state(service);
    if (generation != (unsigned long) service->start_count || service->stopping) {
      InterlockedExchange(&service->rotate_active, 0);
      return 0;
    }
    service->last_control = control;
  }
  if (restart_allowed(service)) {
    int pre = nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_PRE, &control, NSSM_HOOK_DEADLINE, false);
    if (pre != NSSM_HOOK_STATUS_ERROR && pre != NSSM_HOOK_STATUS_TIMEOUT && restart_allowed(service) && await_logger_rotation(service, generation)) {
      service_cleanup_lock state(service);
      if (restart_allowed(service) && ! service->stopping && generation == (unsigned long) service->start_count)
        nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_POST, &control);
    }
  }
  InterlockedExchange(&service->rotate_active, 0);
  return 0;
}

/*
 Wrapper to be called in a new thread so that we can acknowledge start
 immediately.
*/
static unsigned long WINAPI launch_service(void *arg) {
  return monitor_service((nssm_service_t *) arg);
}

/* Connect to the service manager */
SC_HANDLE open_service_manager(unsigned long access) {
  SC_HANDLE ret = OpenSCManager(0, SERVICES_ACTIVE_DATABASE, access);
  if (! ret) {
    if (is_admin) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED);
    return 0;
  }

  return ret;
}

/* Open a service by name or display name. */
SC_HANDLE open_service(SC_HANDLE services, TCHAR *service_name, unsigned long access, TCHAR *canonical_name, unsigned long canonical_namelen) {
  TCHAR resolved[SERVICE_NAME_LENGTH];
  SC_HANDLE service_handle = OpenService(services, service_name, access);
  DWORD error = ERROR_SUCCESS;
  if (service_handle && canonical_name) {
    TCHAR displayname[SERVICE_NAME_LENGTH];
    DWORD display_length = _countof(displayname), length = _countof(resolved);
    if (!GetServiceDisplayName(services, service_name, displayname, &display_length) ||
        !GetServiceKeyName(services, displayname, resolved, &length)) {
      // The input is already a valid key. An in-place caller can retain its spelling.
      if (canonical_name == service_name) return service_handle;
      error = GetLastError();
    }
  }
  else if (!service_handle) {
    error = GetLastError();
    if (error == ERROR_SERVICE_DOES_NOT_EXIST || error == ERROR_INVALID_NAME) {
      DWORD length = _countof(resolved);
      if (GetServiceKeyName(services, service_name, resolved, &length)) {
        service_handle = OpenService(services, resolved, access);
        error = service_handle ? ERROR_SUCCESS : GetLastError();
      }
      else error = GetLastError();
    }
  }

  if (!error && canonical_name) {
    if (_tcslen(resolved) >= canonical_namelen) error = ERROR_INSUFFICIENT_BUFFER;
    else _tcscpy_s(canonical_name, canonical_namelen, resolved);
  }
  if (!error) return service_handle;
  if (service_handle) CloseServiceHandle(service_handle);
  print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(error));
  SetLastError(error);
  return NULL;
}

QUERY_SERVICE_CONFIG *query_service_config(const TCHAR *service_name, SC_HANDLE service_handle, unsigned long *capacity) {
  if (capacity) *capacity = 0;
  DWORD bytes = 0, required = 0;
  QUERY_SERVICE_CONFIG *config = NULL;
  for (;;) {
    if (QueryServiceConfig(service_handle, config, bytes, &required)) {
      if (config) {
        if (capacity) *capacity = bytes;
        return config;
      }
      SetLastError(ERROR_INVALID_DATA);
    }
    DWORD error = GetLastError();
    if (config) HeapFree(GetProcessHeap(), 0, config);
    if (error == ERROR_INSUFFICIENT_BUFFER && (required < sizeof(*config) || required <= bytes || required > 8192))
      error = ERROR_INVALID_DATA;
    if (error != ERROR_INSUFFICIENT_BUFFER) {
      print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG_FAILED, service_name, error_string(error));
      return NULL;
    }
    bytes = required;
    config = (QUERY_SERVICE_CONFIG *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (!config) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("QUERY_SERVICE_CONFIG"), _T("query_service_config()"));
      return NULL;
    }
  }
}

/* WILL NOT allocate a new string if the identifier is already present. */
int prepend_service_group_identifier(TCHAR *group, TCHAR **canon) {
  if (! group || ! group[0] || group[0] == SC_GROUP_IDENTIFIER) {
    *canon = group;
    return 0;
  }

  size_t len = _tcslen(group) + 1;
  *canon = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(TCHAR));
  if (! *canon) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("prepend_service_group_identifier()"));
    return 1;
  }

  TCHAR *s = *canon;
  *s++ = SC_GROUP_IDENTIFIER;
  memmove(s, group, len * sizeof(TCHAR));
  (*canon)[len] = _T('\0');

  return 0;
}

int append_to_dependencies(TCHAR *dependencies, unsigned long dependencieslen, TCHAR *string, TCHAR **newdependencies, unsigned long *newlen, int type) {
  *newlen = 0;

  TCHAR *canon = 0;
  if (type == DEPENDENCY_GROUPS) {
    if (prepend_service_group_identifier(string, &canon)) return 1;
  }
  else canon = string;
  int ret = append_to_double_null(dependencies, dependencieslen, newdependencies, newlen, canon, 0, false);
  if (canon && canon != string) HeapFree(GetProcessHeap(), 0, canon);

  return ret;
}

int remove_from_dependencies(TCHAR *dependencies, unsigned long dependencieslen, TCHAR *string, TCHAR **newdependencies, unsigned long *newlen, int type) {
  *newlen = 0;

  TCHAR *canon = 0;
  if (type == DEPENDENCY_GROUPS) {
    if (prepend_service_group_identifier(string, &canon)) return 1;
  }
  else canon = string;
  int ret = remove_from_double_null(dependencies, dependencieslen, newdependencies, newlen, canon, 0, false);
  if (canon && canon != string) HeapFree(GetProcessHeap(), 0, canon);

  return ret;
}

struct resolved_dependencies_t {
  TCHAR *canonical = NULL;
  ~resolved_dependencies_t() { if (canonical) HeapFree(GetProcessHeap(), 0, canonical); }
  // SCM interprets NULL as "leave unchanged". An empty list explicitly clears it.
  const TCHAR *data() const { return canonical ? canonical : _T("\0"); }
};

static int resolve_service_dependencies(const TCHAR *service_name, const TCHAR *buffer, resolved_dependencies_t &out) {
  struct resources_t {
    SC_HANDLE manager;
    HKEY key;
    TCHAR *groups;
    resources_t() : manager(0), key(0), groups(0) {}
    ~resources_t() {
      if (groups) HeapFree(GetProcessHeap(), 0, groups);
      if (key) RegCloseKey(key);
      if (manager) CloseServiceHandle(manager);
    }
  } resources;
  if (buffer && buffer[0]) {
    size_t count = 0;
    bool needs_groups = false;
    for (const TCHAR *entry = buffer; *entry; entry += _tcslen(entry) + 1) {
      count++;
      if (*entry == SC_GROUP_IDENTIFIER) needs_groups = true;
      if (_tcslen(entry) >= SERVICE_NAME_LENGTH) return 5;
    }
    if (count > (((size_t) -1) / sizeof(TCHAR) - 2) / SERVICE_NAME_LENGTH) return 3;
    resources.manager = open_service_manager(SC_MANAGER_CONNECT);
    if (! resources.manager) {
      print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      return 1;
    }
    if (needs_groups) {
      LONG error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, NSSM_REGISTRY_GROUPS, 0, KEY_READ, &resources.key);
      if (error != ERROR_SUCCESS) {
        _ftprintf(stderr, _T("%s: %s\n"), NSSM_REGISTRY_GROUPS, error_string(error));
        return 2;
      }
      unsigned long length = 0;
      /* Reuse the bounded REG_MULTI_SZ reader, including missing/type/size checks. */
      if (get_environment((TCHAR *) service_name, resources.key, NSSM_REG_GROUPS, &resources.groups, &length)) return 4;
    }
    size_t capacity = count * SERVICE_NAME_LENGTH + 2;
    out.canonical = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(TCHAR));
    if (! out.canonical) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("dependencies"), _T("set_service_dependencies()"));
      return 3;
    }
    size_t used = 0;
    for (const TCHAR *entry = buffer; *entry; entry += _tcslen(entry) + 1) {
      TCHAR name[SERVICE_NAME_LENGTH] = {};
      if (*entry == SC_GROUP_IDENTIFIER) {
        if (resources.groups && entry[1]) {
          for (const TCHAR *group = resources.groups; *group; group += _tcslen(group) + 1) {
            if (! str_equiv(group, entry + 1)) continue;
            if (_sntprintf_s(name, _countof(name), _TRUNCATE, _T("%c%s"), SC_GROUP_IDENTIFIER, group) < 0) return 5;
            break;
          }
        }
      }
      else {
        SC_HANDLE dependency = open_service(resources.manager, (TCHAR *) entry, SERVICE_QUERY_STATUS, name, _countof(name));
        if (! dependency) return 5;
        CloseServiceHandle(dependency);
      }
      if (! name[0]) {
        _ftprintf(stderr, _T("%s: %s"), entry, error_string(ERROR_SERVICE_DEPENDENCY_DELETED));
        return 5;
      }
      size_t length = _tcslen(name) + 1;
      memmove(out.canonical + used, name, length * sizeof(TCHAR));
      used += length;
    }
  }
  return 0;
}

int set_service_dependencies(const TCHAR *service_name, SC_HANDLE service_handle, TCHAR *buffer) {
  resolved_dependencies_t dependencies;
  int result = resolve_service_dependencies(service_name, buffer, dependencies);
  if (result) return result;
  if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, dependencies.data(), 0, 0, 0)) {
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }
  return 0;
}

int get_service_dependencies(const TCHAR *service_name, SC_HANDLE service_handle, TCHAR **buffer, unsigned long *bufsize, int type) {
  if (buffer) *buffer = 0;
  if (bufsize) *bufsize = 0;
  if (! buffer || ! bufsize || ! (type & DEPENDENCY_ALL) || (type & ~DEPENDENCY_ALL)) return 1;
  unsigned long capacity = 0;
  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle, &capacity);
  if (! qsc) return 3;
  struct config_owner_t {
    QUERY_SERVICE_CONFIG *value;
    ~config_owner_t() { HeapFree(GetProcessHeap(), 0, value); }
  } owner = { qsc };
  if (! qsc->lpDependencies) return 0;
  ULONG_PTR offset = (ULONG_PTR) qsc->lpDependencies - (ULONG_PTR) qsc;
  if (offset < sizeof(*qsc) || offset >= capacity || offset % sizeof(TCHAR) || capacity - offset < sizeof(TCHAR)) return 3;
  if (! qsc->lpDependencies[0]) return 0;
  unsigned long length = 0;
  if (! double_null_length(qsc->lpDependencies, (capacity - (unsigned long) offset) / sizeof(TCHAR), &length)) return 3;
  TCHAR *result = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length * sizeof(TCHAR));
  if (! result) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("lpDependencies"), _T("get_service_dependencies()"));
    return 4;
  }
  unsigned long used = 0;
  for (const TCHAR *entry = qsc->lpDependencies; *entry; entry += _tcslen(entry) + 1) {
    if (! ((*entry == SC_GROUP_IDENTIFIER ? DEPENDENCY_GROUPS : DEPENDENCY_SERVICES) & type)) continue;
    unsigned long count = (unsigned long) _tcslen(entry) + 1;
    memmove(result + used, entry, count * sizeof(TCHAR));
    used += count;
  }
  if (! used) { HeapFree(GetProcessHeap(), 0, result); return 0; }
  *buffer = result;
  *bufsize = used + 1; // Entries include their NUL; add only the final NUL.
  return 0;
}

int get_service_dependencies(const TCHAR *service_name, SC_HANDLE service_handle, TCHAR **buffer, unsigned long *bufsize) {
  return get_service_dependencies(service_name, service_handle, buffer, bufsize, DEPENDENCY_ALL);
}

int set_service_description(const TCHAR *service_name, SC_HANDLE service_handle, TCHAR *buffer) {
  SERVICE_DESCRIPTION description;
  ZeroMemory(&description, sizeof(description));
  /*
    lpDescription must be NULL if we aren't changing, the new description
    or "".
  */
  if (buffer && buffer[0]) description.lpDescription = buffer;
  else description.lpDescription = _T("");

  if (ChangeServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, &description)) return 0;

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_DESCRIPTION_FAILED, service_name, error_string(GetLastError()));
  return 1;
}

int get_service_description(const TCHAR *service_name, SC_HANDLE service_handle, unsigned long len, TCHAR *buffer) {
  if (!buffer || !len) return 1;
  buffer[0] = 0;
  // QueryServiceConfig2 caps the entire returned record at 8 KiB.
  const DWORD capacity = 8192;
  std::unique_ptr<BYTE[]> record(new (std::nothrow) BYTE[capacity]);
  if (!record) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("SERVICE_DESCRIPTION"), _T("get_service_description()"));
    return 2;
  }
  DWORD required;
  if (!QueryServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, record.get(), capacity, &required)) {
    print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, _T("SERVICE_CONFIG_DESCRIPTION"), error_string(GetLastError()));
    return 3;
  }
  const TCHAR *description = reinterpret_cast<SERVICE_DESCRIPTION *>(record.get())->lpDescription;
  if (!description) return 0;
  if (_sntprintf_s(buffer, len, _TRUNCATE, _T("%s"), description) >= 0) return 0;
  buffer[0] = 0;
  print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, _T("SERVICE_CONFIG_DESCRIPTION"), error_string(ERROR_INSUFFICIENT_BUFFER));
  return 3;
}

int change_service_startup(const TCHAR *name, SC_HANDLE handle, unsigned long choice, DWORD type,
                          const TCHAR *dependencies, const TCHAR *account, const TCHAR *password, const TCHAR *display) {
  DWORD start = SERVICE_AUTO_START;
  if (choice == SERVICE_NO_CHANGE) start = SERVICE_NO_CHANGE;
  else if (choice == NSSM_STARTUP_MANUAL) start = SERVICE_DEMAND_START;
  else if (choice == NSSM_STARTUP_DISABLED) start = SERVICE_DISABLED;

  SERVICE_DELAYED_AUTO_START_INFO old = {}, desired = {choice == NSSM_STARTUP_DELAYED};
  DWORD required;
  bool changed = false;
  if (choice != SERVICE_NO_CHANGE) {
    if (!QueryServiceConfig2(handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (BYTE *)&old, sizeof(old), &required)) {
      DWORD error = GetLastError();
      if (error != ERROR_INVALID_LEVEL || desired.fDelayedAutostart) {
        print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, name, _T("SERVICE_CONFIG_DELAYED_AUTO_START_INFO"), error_string(error));
        return 1;
      }
    }
    else if (!!old.fDelayedAutostart != !!desired.fDelayedAutostart) {
      if (!ChangeServiceConfig2(handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &desired)) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_DELAYED_AUTO_START_INFO_FAILED, name, error_string(GetLastError()));
        return 1;
      }
      changed = true;
    }
  }
  if (ChangeServiceConfig(handle, type, start, SERVICE_NO_CHANGE, NULL, NULL, NULL, dependencies, account, password, display)) return 0;
  DWORD error = GetLastError();
  bool restored = true;
  if (changed) {
    SERVICE_DELAYED_AUTO_START_INFO current = {};
    restored = QueryServiceConfig2(handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (BYTE *)&current, sizeof(current), &required) &&
               (!!current.fDelayedAutostart == !!old.fDelayedAutostart ||
                ChangeServiceConfig2(handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &old));
    if (!restored) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_DELAYED_AUTO_START_INFO_FAILED, name, error_string(GetLastError()));
  }
  print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(error));
  return restored ? 1 : 2;
}

int get_service_startup(const TCHAR *service_name, SC_HANDLE service_handle, const QUERY_SERVICE_CONFIG *qsc, unsigned long *startup) {
  if (! qsc) return 1;

  switch (qsc->dwStartType) {
    case SERVICE_DEMAND_START: *startup = NSSM_STARTUP_MANUAL; break;
    case SERVICE_DISABLED: *startup = NSSM_STARTUP_DISABLED; break;
    case SERVICE_AUTO_START: *startup = NSSM_STARTUP_AUTOMATIC; break;
    default: return 1;
  }

  if (*startup != NSSM_STARTUP_AUTOMATIC) return 0;

  SERVICE_DELAYED_AUTO_START_INFO info = {};
  DWORD required;
  if (QueryServiceConfig2(service_handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (LPBYTE)&info, sizeof(info), &required)) {
    if (info.fDelayedAutostart) *startup = NSSM_STARTUP_DELAYED;
    return 0;
  }
  DWORD error = GetLastError();
  if (error == ERROR_INVALID_LEVEL) return 0;
  print_message(stderr, NSSM_MESSAGE_QUERYSERVICECONFIG2_FAILED, service_name, _T("SERVICE_CONFIG_DELAYED_AUTO_START_INFO"), error_string(error));
  return 3;
}

int get_service_username(const TCHAR *service_name, const QUERY_SERVICE_CONFIG *qsc, TCHAR **username, size_t *usernamelen) {
  if (! username) return 1;
  if (! usernamelen) return 1;

  *username = 0;
  *usernamelen = 0;

  if (! qsc) return 1;

  if (qsc->lpServiceStartName && qsc->lpServiceStartName[0]) {
    if (is_localsystem(qsc->lpServiceStartName)) return 0;

    size_t len = _tcslen(qsc->lpServiceStartName);
    *username = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(TCHAR));
    if (! *username) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("username"), _T("get_service_username()"));
      return 2;
    }

    memmove(*username, qsc->lpServiceStartName, (len + 1) * sizeof(TCHAR));
    *usernamelen = len;
  }

  return 0;
}

/* Set default values which aren't zero. */
void set_nssm_service_defaults(nssm_service_t *service) {
  if (! service) return;

  service->type = SERVICE_WIN32_OWN_PROCESS;
  service->priority = NORMAL_PRIORITY_CLASS;
  service->stdin_sharing = NSSM_STDIN_SHARING;
  service->stdin_disposition = NSSM_STDIN_DISPOSITION;
  service->stdin_flags = NSSM_STDIN_FLAGS;
  service->stdout_sharing = NSSM_STDOUT_SHARING;
  service->stdout_disposition = NSSM_STDOUT_DISPOSITION;
  service->stdout_flags = NSSM_STDOUT_FLAGS;
  service->stderr_sharing = NSSM_STDERR_SHARING;
  service->stderr_disposition = NSSM_STDERR_DISPOSITION;
  service->stderr_flags = NSSM_STDERR_FLAGS;
  service->throttle_delay = NSSM_RESET_THROTTLE_RESTART;
  service->stop_method = ~0;
  service->kill_console_delay = NSSM_KILL_CONSOLE_GRACE_PERIOD;
  service->kill_window_delay = NSSM_KILL_WINDOW_GRACE_PERIOD;
  service->kill_threads_delay = NSSM_KILL_THREADS_GRACE_PERIOD;
  service->kill_process_tree = 1;
}

/* Allocate and zero memory for a service. */
nssm_service_t *alloc_nssm_service() {
  nssm_service_t *service = (nssm_service_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(nssm_service_t));
  if (! service) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("alloc_nssm_service()"));
  return service;
}

struct completed_wait_t {
  HANDLE event;
  completed_wait_t *next;
};

/* A callback may register its successor before returning. Keep each completion
   event until Windows releases that registration's last callback. */
static DWORD reap_process_waits(nssm_service_t *service, bool drain) {
  completed_wait_t **link = &service->retiring_waits;
  while (*link) {
    completed_wait_t *wait = *link;
    DWORD result = WaitForSingleObject(wait->event, drain ? INFINITE : 0);
    if (result == WAIT_TIMEOUT && !drain) { link = &wait->next; continue; }
    if (result != WAIT_OBJECT_0) return result == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
    *link = wait->next;
    CloseHandle(wait->event);
    HeapFree(GetProcessHeap(), 0, wait);
  }
  return ERROR_SUCCESS;
}

static DWORD unregister_process_wait(nssm_service_t *service, bool drain) {
  DWORD error = reap_process_waits(service, drain);
  if (!error && service->wait_handle) {
    // Final destruction runs outside the hook lock. Restart and stop can run
    // inside this registration's own callback and must not wait for it.
    completed_wait_t *wait = NULL;
    if (!drain) {
      wait = (completed_wait_t *) HeapAlloc(GetProcessHeap(), 0, sizeof(*wait));
      if (!wait) error = ERROR_NOT_ENOUGH_MEMORY;
      else if (!(wait->event = CreateEvent(NULL, TRUE, FALSE, NULL))) error = GetLastError();
    }
    if (!error && !UnregisterWaitEx(service->wait_handle, wait ? wait->event : INVALID_HANDLE_VALUE)) {
      error = GetLastError();
      if (wait && error == ERROR_IO_PENDING) error = ERROR_SUCCESS;
    }
    if (!error) {
      service->wait_handle = NULL;
      if (wait) {
        wait->next = service->retiring_waits;
        service->retiring_waits = wait;
        error = reap_process_waits(service, false);
      }
    }
    else if (wait) {
      if (wait->event) CloseHandle(wait->event);
      HeapFree(GetProcessHeap(), 0, wait);
    }
  }
  if (error) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_UNREGISTERWAIT_FAILED, service->name, error_string(error));
  return error;
}

/* Free memory for a service. */
void cleanup_nssm_service(nssm_service_t *service) {
  if (! service) return;
  {
    service_cleanup_lock state(service);
    InterlockedExchange(&service->allow_restart, 0);
    service->terminal = true;
  }
  if (unregister_process_wait(service, true)) return; // A callback may still own the context.
  cleanup_loggers(service);
  if (service->username) HeapFree(GetProcessHeap(), 0, service->username);
  if (service->password) {
    SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
    HeapFree(GetProcessHeap(), 0, service->password);
  }
  if (service->dependencies) HeapFree(GetProcessHeap(), 0, service->dependencies);
  if (service->env) HeapFree(GetProcessHeap(), 0, service->env);
  if (service->env_extra) HeapFree(GetProcessHeap(), 0, service->env_extra);
  if (service->handle) CloseServiceHandle(service->handle);
  if (service->process_handle) CloseHandle(service->process_handle);
  close_handle(&service->resume_event);
  close_handle(&service->stop_event);
  close_handle(&service->finished_event);
  close_handle(&service->power_port);
  if (service->hook_section_initialised) DeleteCriticalSection(&service->hook_section);
  if (service->initial_env) HeapFree(GetProcessHeap(), 0, service->initial_env);
  HeapFree(GetProcessHeap(), 0, service);
}

/* About to install the service */
bool valid_service_key_name(const TCHAR *name) {
  return name && *name && _tcslen(name) < SERVICE_NAME_LENGTH && !_tcspbrk(name, _T("/\\"));
}

static bool valid_service_name(const TCHAR *name) {
  if (name && *name && _tcslen(name) < SERVICE_NAME_LENGTH) return true;
  print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(ERROR_INVALID_NAME));
  return false;
}

int pre_install_service(int argc, TCHAR **argv) {
  if (argc && !valid_service_key_name(argv[0])) {
    print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(ERROR_INVALID_NAME));
    return 2;
  }
  if (argc >= 2 && !argv[1][0]) {
    print_message(stderr, NSSM_GUI_MISSING_PATH);
    return 2;
  }
  nssm_service_t *service = alloc_nssm_service();
  if (! service) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("service"), _T("pre_install_service()"));
    return 1;
  }
  set_nssm_service_defaults(service);
  if (argc) _sntprintf_s(service->name, _countof(service->name), _TRUNCATE, _T("%s"), argv[0]);

  /* Show the dialogue box if we didn't give the service name and path */
  if (argc < 2) return nssm_gui(IDD_INSTALL, service);

  if (_sntprintf_s(service->exe, _countof(service->exe), _TRUNCATE, _T("%s"), argv[1]) < 0) {
    print_message(stderr, NSSM_MESSAGE_PATH_TOO_LONG, NSSM_REG_EXE);
    cleanup_nssm_service(service);
    return 2;
  }

  /* Arguments are optional */
  size_t flagslen = 0;
  size_t s = 0;
  int i;
  for (i = 2; i < argc; i++) flagslen += _tcslen(argv[i]) + 1;
  if (! flagslen) flagslen = 1;
  if (flagslen > _countof(service->flags)) {
    print_message(stderr, NSSM_MESSAGE_FLAGS_TOO_LONG);
    cleanup_nssm_service(service);
    return 2;
  }

  for (i = 2; i < argc; i++) {
    size_t len = _tcslen(argv[i]);
    memmove(service->flags + s, argv[i], len * sizeof(TCHAR));
    s += len;
    if (i < argc - 1) service->flags[s++] = _T(' ');
  }

  /* Work out directory name */
  if (!executable_directory(service->exe, service->dir, _countof(service->dir))) {
    print_message(stderr, NSSM_MESSAGE_PATH_TOO_LONG, NSSM_REG_DIR);
    cleanup_nssm_service(service);
    return 2;
  }

  int ret = install_service(service);
  cleanup_nssm_service(service);
  return ret;
}

/* Identify separately installed and renamed wrappers without running them. */
static bool wrapper_image(const TCHAR *command) {
  text_buffer<PATH_LENGTH> image;
  TCHAR *path = image.get();
  if (!path || _sntprintf_s(path, image.size(), _TRUNCATE, _T("%s"), command) < 0) return false;
  PathUnquoteSpaces(path);
  if (same_file(path, nssm_unquoted_imagepath())) return true;
  DWORD ignored, bytes = GetFileVersionInfoSize(path, &ignored);
  if (!bytes && _tcschr(command, _T('%'))) {
    DWORD length = ExpandEnvironmentStrings(command, path, image.size());
    if (!length || length > image.size()) return false;
    PathUnquoteSpaces(path);
    bytes = GetFileVersionInfoSize(path, &ignored);
  }
  if (!bytes) return false;
  void *info = HeapAlloc(GetProcessHeap(), 0, bytes);
  if (!info) return false;
  bool match = false;
  if (GetFileVersionInfo(path, 0, bytes, info)) {
    const TCHAR *tables[] = {_T("040904B0"), _T("040904E4")};
    for (const TCHAR *table : tables) {
      TCHAR key[96], *value;
      UINT length;
      _stprintf_s(key, _T("\\StringFileInfo\\%s\\OriginalFilename"), table);
      if (!VerQueryValue(info, key, (void **) &value, &length) || !length || !str_equiv(value, _T("nssm.exe"))) continue;
      _stprintf_s(key, _T("\\StringFileInfo\\%s\\FileDescription"), table);
      match = VerQueryValue(info, key, (void **) &value, &length) && length && str_equiv(value, _T("The non-sucking service manager"));
      if (match) break;
    }
  }
  HeapFree(GetProcessHeap(), 0, info);
  return match;
}

/* About to edit the service. */
int pre_edit_service(int argc, TCHAR **argv) {
  /* Require service name. */
  if (argc < 2) return usage(1);
  if (!valid_service_name(argv[1])) return 2;

  /* Are we editing on the command line? */
  enum { MODE_EDITING, MODE_GETTING, MODE_SETTING, MODE_RESETTING, MODE_DUMPING } mode = MODE_EDITING;
  const TCHAR *verb = argv[0];
  const TCHAR *service_name = argv[1];
  bool getting = false;
  bool unsetting = false;

  /* Minimum number of arguments. */
  int mandatory = 2;
  /* Index of first value. */
  int remainder = 3;
  int i;
  if (str_equiv(verb, _T("get"))) {
    mandatory = 3;
    mode = MODE_GETTING;
  }
  else if (str_equiv(verb, _T("set"))) {
    mandatory = 4;
    mode = MODE_SETTING;
  }
  else if (str_equiv(verb, _T("reset")) || str_equiv(verb, _T("unset"))) {
    mandatory = 3;
    mode = MODE_RESETTING;
  }
  else if (str_equiv(verb, _T("dump"))) {
    if (argc > 2 && !valid_service_key_name(argv[2])) {
      print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED, error_string(ERROR_INVALID_NAME));
      return 2;
    }
    mandatory = 1;
    remainder = 2;
    mode = MODE_DUMPING;
  }
  if (argc < mandatory) return usage(1);

  const TCHAR *parameter = 0;
  bool literal = false;
  settings_t *setting = 0;
  TCHAR *additional;

  /* Validate the parameter. */
  if (mandatory > 2) {
    bool additional_mandatory = false;

    parameter = argv[2];
    const TCHAR prefix[] = _T("REG_SZ:");
    if (mode == MODE_SETTING && !_tcsnicmp(parameter, prefix, _countof(prefix) - 1)) {
      literal = true;
      parameter += _countof(prefix) - 1;
    }
    for (i = 0; settings[i].name; i++) {
      setting = &settings[i];
      if (! str_equiv(setting->name, parameter)) continue;
      if (literal && (settings[i].native || settings[i].type != REG_EXPAND_SZ)) continue;
      if (((setting->additional & ADDITIONAL_GETTING) && mode == MODE_GETTING) || ((setting->additional & ADDITIONAL_SETTING) && mode == MODE_SETTING) || ((setting->additional & ADDITIONAL_RESETTING) && mode == MODE_RESETTING)) {
        additional_mandatory = true;
        mandatory++;
      }
      break;
    }
    if (! settings[i].name) {
      print_message(stderr, NSSM_MESSAGE_INVALID_PARAMETER, parameter);
      for (i = 0; settings[i].name; i++) _ftprintf(stderr, _T("%s\n"), settings[i].name);
      return 1;
    }

    if (mode == MODE_SETTING && argc > remainder + 1 &&
        (str_equiv(parameter, NSSM_REG_ENV) || str_equiv(parameter, NSSM_REG_ENV_EXTRA) || str_equiv(parameter, NSSM_NATIVE_ENVIRONMENT)) &&
        (*argv[remainder] == _T('+') || *argv[remainder] == _T('-'))) {
      print_message(stderr, NSSM_GUI_INVALID_ENVIRONMENT);
      return 1;
    }

    additional = 0;
    if (additional_mandatory) {
      if (argc < mandatory) {
        print_message(stderr, NSSM_MESSAGE_MISSING_SUBPARAMETER, parameter);
        return 1;
      }
      additional = argv[3];
      remainder = 4;
    }
    else if (str_equiv(setting->name, NSSM_NATIVE_OBJECTNAME) && mode == MODE_SETTING) {
      additional = argv[3];
      remainder = 4;
    }
    else {
      additional = argv[remainder];
      if (!setting->native && setting->type == REG_EXPAND_SZ) additional = literal ? _T("REG_SZ") : NULL;
      if (argc < mandatory) return usage(1);
    }
  }

  nssm_service_t *service = alloc_nssm_service();
  if (! service) return 2;
  struct service_owner_t {
    nssm_service_t *value;
    ~service_owner_t() { cleanup_nssm_service(value); }
  } owner = { service };
  _sntprintf_s(service->name, _countof(service->name), _TRUNCATE, _T("%s"), service_name);

  /* Open service manager */
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 2;
  }

  /* Try to open the service */
  unsigned long access = SERVICE_QUERY_CONFIG;
  if (mode != MODE_GETTING && mode != MODE_DUMPING) access |= SERVICE_CHANGE_CONFIG;
  service->handle = open_service(services, service->name, access, service->name, _countof(service->name));
  if (! service->handle) {
    CloseServiceHandle(services);
    return 3;
  }

  /* Get system details. */
  QUERY_SERVICE_CONFIG *qsc = query_service_config(service->name, service->handle);
  if (! qsc) {
    CloseServiceHandle(services);
    return 4;
  }

  service->type = qsc->dwServiceType;
  if (! (service->type & SERVICE_WIN32_OWN_PROCESS)) {
    if (mode != MODE_GETTING && mode != MODE_DUMPING) {
      HeapFree(GetProcessHeap(), 0, qsc);
      CloseServiceHandle(services);
      print_message(stderr, NSSM_MESSAGE_CANNOT_EDIT, service->name, NSSM_WIN32_OWN_PROCESS, 0);
      return 3;
    }
  }

  if (get_service_startup(service->name, service->handle, qsc, &service->startup)) {
    if (mode != MODE_GETTING && mode != MODE_DUMPING) {
      HeapFree(GetProcessHeap(), 0, qsc);
      CloseServiceHandle(services);
      return 4;
    }
  }

  if (get_service_username(service->name, qsc, &service->username, &service->usernamelen)) {
    if (mode != MODE_GETTING && mode != MODE_DUMPING) {
      HeapFree(GetProcessHeap(), 0, qsc);
      CloseServiceHandle(services);
      return 5;
    }
  }

  _sntprintf_s(service->displayname, _countof(service->displayname), _TRUNCATE, _T("%s"), qsc->lpDisplayName);

  /* Remember the executable in case it isn't NSSM. */
  _sntprintf_s(service->image, _countof(service->image), _TRUNCATE, _T("%s"), qsc->lpBinaryPathName);
  HeapFree(GetProcessHeap(), 0, qsc);

  /* Get extended system details. */
  if (get_service_description(service->name, service->handle, _countof(service->description), service->description)) {
    if (mode != MODE_GETTING && mode != MODE_DUMPING) {
      CloseServiceHandle(services);
      return 6;
    }
  }

  if (get_service_dependencies(service->name, service->handle, &service->dependencies, &service->dependencieslen)) {
    if (mode != MODE_GETTING && mode != MODE_DUMPING) {
      CloseServiceHandle(services);
      return 7;
    }
  }

  service->native = !wrapper_image(service->image);
  int parameters = service->native ? 0 : get_parameters(service, NULL);
  CloseServiceHandle(services);
  bool repair = setting && (mode == MODE_SETTING || mode == MODE_RESETTING);
  if ((service->native && mode == MODE_DUMPING) ||
      (parameters && !repair && (mode == MODE_DUMPING || !setting || !setting->native))) {
    print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE, service->name, NSSM, service->image);
    return 4;
  }

  /* Editing with the GUI. */
  if (mode == MODE_EDITING) {
    owner.value = NULL;
    return nssm_gui(IDD_EDIT, service);
  }

  HKEY key = NULL;
  value_t value = {};
  int ret;

  if (mode == MODE_DUMPING) {
    TCHAR *target_name = argc > remainder ? argv[remainder] : service->name;
    key = open_registry(service->name, KEY_READ);
    if (! key) return 4;

    struct registry_owner_t {
      HKEY value;
      ~registry_owner_t() { if (value) RegCloseKey(value); }
    } registry_owner = { key };
    TCHAR quoted_service_name[SERVICE_NAME_LENGTH * 2];
    text_buffer<EXE_LENGTH * 2> quoted_exe, quoted_nssm;
    if (!quoted_exe.get() || !quoted_nssm.get()) return 6;
    if (quote(target_name, quoted_service_name, _countof(quoted_service_name))) return 5;
    if (quote(service->exe, quoted_exe.get(), quoted_exe.size())) return 6;
    if (quote(nssm_exe(), quoted_nssm.get(), quoted_nssm.size())) return 6;
    if (write_dump_command(_T("%s install %s %s"), quoted_nssm.get(), quoted_service_name, quoted_exe.get())) return 1;

    ret = 0;
    for (i = 0; settings[i].name; i++) {
      setting = &settings[i];
      if (str_equiv(setting->name, NSSM_NATIVE_DISPLAYNAME) && ! str_equiv(target_name, service->name)) continue;
      if (dump_setting(target_name, key, service->handle, setting, service->name)) ret++;
    }
    return stdout_ok() && !ret ? 0 : 1;
  }

  /* Trying to manage App* parameters for a non-NSSM service. */
  if (! setting->native && service->native) {
    print_message(stderr, NSSM_MESSAGE_NATIVE_PARAMETER, setting->name, NSSM);
    return 1;
  }

  if (mode == MODE_GETTING) {
    if (! setting->native) {
      key = open_registry(service->name, KEY_READ);
      if (! key) return 4;
    }

    if (setting->native) ret = get_setting(service->name, service->handle, setting, &value, additional);
    else ret = get_setting(service->name, key, setting, &value, additional);
    if (ret < 0) {
      if (! setting->native) RegCloseKey(key);
      return 5;
    }

    switch (setting->type) {
      case REG_EXPAND_SZ:
      case REG_MULTI_SZ:
      case REG_SZ:
        ret = _tprintf(_T("%s\n"), value.string ? value.string : _T(""));
        if (value.string) HeapFree(GetProcessHeap(), 0, value.string);
        break;

      case REG_DWORD:
        ret = _tprintf(_T("%lu\n"), value.numeric);
        break;
    }

    if (! setting->native) RegCloseKey(key);
    return ret >= 0 && stdout_ok() ? 0 : 5;
  }

  /* Build the value. */
  if (mode == MODE_RESETTING) {
    /* Unset the parameter. */
    value.string = 0;
  }
  else if (remainder == argc) {
    value.string = 0;
  }
  else {
    /* Set the parameter. */
    size_t len = 0;
    size_t delimiterlen = (setting->additional & ADDITIONAL_CRLF) ? 2 : 1;
    for (i = remainder; i < argc; i++) len += _tcslen(argv[i]) + delimiterlen;
    len++;

    value.string = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
    if (! value.string) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("value"), _T("edit_service()"));
      return 2;
    }

    size_t s = 0;
    for (i = remainder; i < argc; i++) {
      size_t argument_length = _tcslen(argv[i]);
      memmove(value.string + s, argv[i], argument_length * sizeof(TCHAR));
      s += argument_length;
      if (i < argc - 1) {
        if (setting->additional & ADDITIONAL_CRLF) {
          value.string[s++] = _T('\r');
          value.string[s++] = _T('\n');
        }
        else value.string[s++] = _T(' ');
      }
    }
    value.string[s] = _T('\0');
  }

  if (! setting->native) {
    key = open_registry(service->name, KEY_READ | KEY_WRITE);
    if (! key) {
      if (value.string) HeapFree(GetProcessHeap(), 0, value.string);
      return 4;
    }
  }

  if (setting->native) ret = set_setting(service->name, service->handle, setting, &value, additional);
  else ret = set_setting(service->name, key, setting, &value, additional);
  if (value.string) HeapFree(GetProcessHeap(), 0, value.string);
  if (ret < 0) {
    if (! setting->native) RegCloseKey(key);
    return 6;
  }

  if (! setting->native) RegCloseKey(key);

  return 0;
}

/* About to remove the service */
int pre_remove_service(int argc, TCHAR **argv) {
  if (argc && !valid_service_name(argv[0])) return 2;
  nssm_service_t *service = alloc_nssm_service();
  if (!service) return 1;
  set_nssm_service_defaults(service);
  if (argc) _tcscpy_s(service->name, argv[0]);

  /* Show dialogue box if we didn't pass service name and "confirm" */
  if (argc < 2) return nssm_gui(IDD_REMOVE, service);
  if (str_equiv(argv[1], _T("confirm"))) {
    int ret = remove_service(service);
    cleanup_nssm_service(service);
    return ret;
  }
  print_message(stderr, NSSM_MESSAGE_PRE_REMOVE_SERVICE);
  cleanup_nssm_service(service);
  return 100;
}

/* SCM stores a command line. Shell metacharacters in file names are literal here. */
static bool service_image_path(const TCHAR *path, TCHAR *image, size_t capacity) {
  if (!*path) return false;
  const TCHAR *format = _tcspbrk(path, _T(" \t")) ? _T("\"%s\"") : _T("%s");
  return _sntprintf_s(image, capacity, _TRUNCATE, format, path) >= 0;
}

/* Install the service */
int install_service(nssm_service_t *service, int (*save_hooks)(TCHAR *)) {
  if (! service) return 1;

  /* Open service manager */
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 2;
  }

  if (!service_image_path(nssm_unquoted_imagepath(), service->image, _countof(service->image))) {
    print_message(stderr, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
    CloseServiceHandle(services);
    return 3;
  }

  /* Create the service - settings will be changed in edit_service() */
  service->handle = CreateService(services, service->name, service->name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, service->image, 0, 0, 0, 0, 0);
  if (! service->handle) {
    print_message(stderr, NSSM_MESSAGE_CREATESERVICE_FAILED, error_string(GetLastError()));
    CloseServiceHandle(services);
    return 5;
  }

  if (edit_service(service, false) || (save_hooks && save_hooks(service->name))) {
    BOOL removed = DeleteService(service->handle);
    if (! removed && GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) removed = TRUE;
    if (! removed) print_message(stderr, NSSM_GUI_INSTALL_ROLLBACK_FAILED);
    CloseServiceHandle(services);
    return removed ? 6 : 7;
  }

  print_message(stdout, NSSM_MESSAGE_SERVICE_INSTALLED, service->name);

  /* Cleanup */
  CloseServiceHandle(services);

  return 0;
}

static const TCHAR *dependency_list(const nssm_service_t *service) {
  if (!service || !service->dependencieslen || !service->dependencies || !*service->dependencies) return NULL;
  return service->dependencies;
}

/* Edit the service. */
int edit_service(nssm_service_t *service, bool editing, int (*stage_hooks)(TCHAR *, registry_edit *)) {
  if (! service) return 1;

  service->edit_error[0] = service->rollback_errors[0] = 0;
  const nssm_service_t *original = editing ? service->original : NULL;
  const TCHAR *requested_dependencies = dependency_list(service);
  const bool update_dependencies = !original || !same_double_null(requested_dependencies, service->dependencieslen,
                                                                  dependency_list(original), original->dependencieslen);
  resolved_dependencies_t dependencies;
  if (update_dependencies && resolve_service_dependencies(service->name, requested_dependencies, dependencies)) return 5;

  /*
    The only two valid flags for service type are SERVICE_WIN32_OWN_PROCESS
    and SERVICE_INTERACTIVE_PROCESS.
  */
  service->type &= SERVICE_INTERACTIVE_PROCESS;
  service->type |= SERVICE_WIN32_OWN_PROCESS;

  /* Display name. */
  if (! service->displayname[0]) _sntprintf_s(service->displayname, _countof(service->displayname), _TRUNCATE, _T("%s"), service->name);

  /*
    Username must be NULL if we aren't changing or an account name.
    We must explicitly use LOCALSYSTEM to change it when we are editing.
    Password must be NULL if we aren't changing, a password or "".
    Empty passwords are valid but we won't allow them in the GUI.
  */
  TCHAR *username = 0;
  TCHAR *canon = 0;
  TCHAR *password = 0;
  boolean virtual_account = false;
  bool same_account = editing && service->original && !service->passwordlen &&
                      str_equiv(service->username ? service->username : NSSM_LOCALSYSTEM_ACCOUNT,
                                service->original->username ? service->original->username : NSSM_LOCALSYSTEM_ACCOUNT);
  if (!same_account && service->usernamelen) {
    username = service->username;
    if (is_virtual_account(service->name, username)) {
      virtual_account = true;
      canon = username;
    }
    else {
      if (canonicalise_username(username, &canon)) return 5;
      if (service->passwordlen) password = service->password;
    }
  }
  else if (!same_account && editing) username = canon = NSSM_LOCALSYSTEM_ACCOUNT;

  // The account and password go in the final SCM write. An old password cannot be recovered.
  const bool needs_right = !virtual_account && canon && !well_known_username(canon);
  if (canon && !virtual_account && !needs_right) password = _T("");
  registry_edit changes(service);
  text_buffer<VALUE_LENGTH> previous_description;
  bool description_changed = false, granted = false;
  int result = 0;
  bool update_description = service->original ? _tcscmp(service->original->description, service->description) != 0 :
                            (editing || service->description[0]);
  if (editing && update_description) {
    if (!previous_description.get() || get_service_description(service->name, service->handle,
        previous_description.size(), previous_description.get())) {
      _tcscpy_s(service->edit_error, _T("Description"));
      result = 5;
    }
    else update_description = _tcscmp(previous_description.get(), service->description) != 0;
  }
  if (!result && !service->native && create_parameters(service, editing, &changes)) {
    print_message(stderr, NSSM_MESSAGE_CREATE_PARAMETERS_FAILED);
    result = 6;
  }
  if (!result && !service->native && stage_hooks && stage_hooks(service->name, &changes)) result = 6;
  if (!result && changes.apply()) result = 6;
  if (!result && update_description) {
    if (set_service_description(service->name, service->handle, service->description)) {
      _tcscpy_s(service->edit_error, _T("Description"));
      result = 5;
    }
    else description_changed = true;
  }
  if (!result && needs_right && grant_logon_as_service(canon, &granted)) {
    print_message(stderr, NSSM_MESSAGE_GRANT_LOGON_AS_SERVICE_FAILED, username);
    _tcscpy_s(service->edit_error, NSSM_LOGON_AS_SERVICE_RIGHT);
    result = 5;
  }
  DWORD startup = original && service->startup == original->startup ? SERVICE_NO_CHANGE : service->startup;
  DWORD type = original && !((service->type ^ original->type) & SERVICE_INTERACTIVE_PROCESS) ? SERVICE_NO_CHANGE : service->type;
  const TCHAR *display = original && !_tcscmp(service->displayname, original->displayname) ? NULL : service->displayname;
  if (!result && (startup != SERVICE_NO_CHANGE || type != SERVICE_NO_CHANGE || update_dependencies || canon || display)) {
    int error = change_service_startup(service->name, service->handle, startup, type,
                                       update_dependencies ? dependencies.data() : NULL, canon, password, display);
    if (error) {
      _tcscpy_s(service->edit_error, _T("Service Control Manager"));
      if (error == 2) _tcscpy_s(service->rollback_errors, _T("DelayedAutoStart "));
      if (granted) _tcsncat_s(service->rollback_errors, NSSM_LOGON_AS_SERVICE_RIGHT _T(" "), _TRUNCATE);
      result = 5;
    }
  }
  if (canon != username) HeapFree(GetProcessHeap(), 0, canon);
  if (result) {
    if (editing && description_changed) {
      text_buffer<VALUE_LENGTH> current;
      if (!current.get() || get_service_description(service->name, service->handle, current.size(), current.get()) ||
          (_tcscmp(current.get(), previous_description.get()) &&
           (_tcscmp(current.get(), service->description) ||
            set_service_description(service->name, service->handle, previous_description.get()))))
        _tcsncat_s(service->rollback_errors, _T("Description "), _TRUNCATE);
    }
    changes.rollback();
  }
  else if (!service->native) set_service_recovery(service);
  return result;
}

/* Control a service. */
int control_service(unsigned long control, int argc, TCHAR **argv, bool return_status) {
  if (argc < 1) return usage(1);
  TCHAR *service_name = argv[0];
  TCHAR canonical_name[SERVICE_NAME_LENGTH] = {0};

  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    if (return_status) return 0;
    return 2;
  }

  unsigned long access = SERVICE_QUERY_STATUS;
  switch (control) {
    case NSSM_SERVICE_CONTROL_START:
      access |= SERVICE_START;
    break;

    case SERVICE_CONTROL_CONTINUE:
    case SERVICE_CONTROL_PAUSE:
      access |= SERVICE_PAUSE_CONTINUE;
      break;

    case SERVICE_CONTROL_STOP:
      access |= SERVICE_STOP;
      break;

    case NSSM_SERVICE_CONTROL_ROTATE:
      access |= SERVICE_USER_DEFINED_CONTROL;
      break;
  }

  SC_HANDLE service_handle = open_service(services, service_name, access, canonical_name, _countof(canonical_name));
  if (! service_handle) {
    CloseServiceHandle(services);
    if (return_status) return 0;
    return 3;
  }

  int ret;
  unsigned long error;
  SERVICE_STATUS service_status;
  ZeroMemory(&service_status, sizeof(service_status));
  if (control == NSSM_SERVICE_CONTROL_START) {
    // ServiceMain receives the key name from the SCM ahead of these arguments.
    ret = StartService(service_handle, argc - 1, argc > 1 ? (const TCHAR **) (argv + 1) : NULL);
    error = ret ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(services);

    if (error == ERROR_IO_PENDING) {
      /*
        Older versions of Windows return immediately with ERROR_IO_PENDING
        indicate that the operation is still in progress.  Newer versions
        will return it if there really is a delay.
      */
      ret = 1;
      error = ERROR_SUCCESS;
    }

    if (ret) {
      error = await_service_start(service_handle, &service_status);
      CloseServiceHandle(service_handle);

      if (error != ERROR_SUCCESS) {
        if (service_status.dwCurrentState) print_message(stderr, NSSM_MESSAGE_BAD_CONTROL_RESPONSE, canonical_name, service_status_text(service_status.dwCurrentState), service_control_text(control));
        _ftprintf(stderr, _T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
        if (error == ERROR_SERVICE_SPECIFIC_ERROR) _ftprintf(stderr, _T("%s: service-specific exit code %lu\n"), canonical_name, service_status.dwServiceSpecificExitCode);
        if (return_status) return 0;
        return 1;
      }
      else _tprintf(_T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
      return 0;
    }
    else {
      CloseServiceHandle(service_handle);
      _ftprintf(stderr, _T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
      if (return_status) return 0;
      return 1;
    }
  }
  else if (control == SERVICE_CONTROL_INTERROGATE) {
    /*
      We could actually send an INTERROGATE control but that won't return
      any information if the service is stopped and we don't care about
      the extra details it might give us in any case.  So we'll fake it.
    */
    ret = QueryServiceStatus(service_handle, &service_status);
    error = ret ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(service_handle);
    CloseServiceHandle(services);

    if (ret) {
      int written = _tprintf(_T("%s\n"), service_status_text(service_status.dwCurrentState));
      if (return_status) return service_status.dwCurrentState;
      return written >= 0 && stdout_ok() ? 0 : 1;
    }
    else {
      _ftprintf(stderr, _T("%s: %s\n"), canonical_name, error_string(error));
      if (return_status) return 0;
      return 1;
    }
  }
  else {
    unsigned long timeout = service_control_timeout(control, canonical_name);
    ret = ControlService(service_handle, control, &service_status);
    unsigned long initial_status = service_status.dwCurrentState;
    error = ret ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(services);

    if (error == ERROR_IO_PENDING) {
      ret = 1;
      error = ERROR_SUCCESS;
    }

    if (ret) {
      service_wait_result response = await_service_control_response(control, service_handle, &service_status, initial_status, timeout);
      error = response.error;
      CloseServiceHandle(service_handle);

      if (response.outcome != SERVICE_WAIT_COMPLETE) {
        if (service_status.dwCurrentState) print_message(stderr, NSSM_MESSAGE_BAD_CONTROL_RESPONSE, canonical_name, service_status_text(service_status.dwCurrentState), service_control_text(control));
        _ftprintf(stderr, _T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
        if (response.outcome == SERVICE_WAIT_STALLED) _ftprintf(stderr, _T("%s: service made no progress before its wait hint expired.\n"), canonical_name);
        if (response.outcome == SERVICE_WAIT_DEADLINE) _ftprintf(stderr, _T("%s: total control wait limit (%lu ms) reached.\n"), canonical_name, timeout);
        if (error == ERROR_SERVICE_SPECIFIC_ERROR) _ftprintf(stderr, _T("%s: service-specific exit code %lu\n"), canonical_name, service_status.dwServiceSpecificExitCode);
        if (return_status) return 0;
        return 1;
      }
      else _tprintf(_T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
      if (return_status) return service_status.dwCurrentState;
      return 0;
    }
    else {
      CloseServiceHandle(service_handle);
      _ftprintf(stderr, _T("%s: %s: %s"), canonical_name, service_control_text(control), error_string(error));
      if (error == ERROR_SERVICE_NOT_ACTIVE) {
        if (control == SERVICE_CONTROL_SHUTDOWN || control == SERVICE_CONTROL_STOP) {
          if (return_status) return SERVICE_STOPPED;
          return 0;
        }
      }
      if (return_status) return 0;
      return 1;
    }
  }
}

int control_service(unsigned long control, int argc, TCHAR **argv) {
  return control_service(control, argc, argv, false);
}

/* Remove the service */
int remove_service(nssm_service_t *service) {
  if (! service) return 1;

  /* Open service manager */
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (! services) {
    DWORD error = GetLastError();
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return error == ERROR_ACCESS_DENIED ? ERROR_ACCESS_DENIED : 2;
  }

  /* Try to open the service */
  service->handle = open_service(services, service->name, DELETE, service->name, _countof(service->name));
  if (! service->handle) {
    DWORD error = GetLastError();
    CloseServiceHandle(services);
    return error == ERROR_ACCESS_DENIED ? ERROR_ACCESS_DENIED : 3;
  }

  /* Try to delete the service */
  if (! DeleteService(service->handle)) {
    print_message(stderr, NSSM_MESSAGE_DELETESERVICE_FAILED);
    CloseServiceHandle(services);
    return 4;
  }

  /* Cleanup */
  CloseServiceHandle(services);

  print_message(stdout, NSSM_MESSAGE_SERVICE_REMOVED, service->name);
  return 0;
}

/* The SCM can already be reporting START_PENDING with checkpoint zero and
   its own short wait hint before the wrapper registers its handler. */
static void report_start_pending(nssm_service_t *service) {
  service->status.dwCurrentState = SERVICE_START_PENDING;
  service->status.dwWaitHint = NSSM_SERVICE_STATUS_DEADLINE + NSSM_WAITHINT_MARGIN;
  ++service->status.dwCheckPoint;
  report_service_status(service);
}

/* Service initialisation */
void WINAPI service_main(unsigned long, TCHAR **argv) {
  nssm_service_t *service = alloc_nssm_service();
  if (! service) return;

  if (_sntprintf_s(service->name, _countof(service->name), _TRUNCATE, _T("%s"), argv[0]) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("service->name"), _T("service_main()"));
    return;
  }

  /* Initialise status */
  ZeroMemory(&service->status, sizeof(service->status));
  service->status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
  service->status.dwControlsAccepted = 0;
  service->status.dwWin32ExitCode = NO_ERROR;
  service->status.dwServiceSpecificExitCode = 0;
  service->status.dwCheckPoint = 0;
  service->status.dwWaitHint = NSSM_WAITHINT_MARGIN;

  /* Signal we AREN'T running the server */
  service->process_handle = 0;
  service->pid = 0;

  service->hook_section_initialised = InitializeCriticalSectionAndSpinCount(&service->hook_section, 0) != FALSE;

  /* Register control handler */
  service->status_handle = RegisterServiceCtrlHandlerEx(NSSM, service_control_handler, (void *) service);
  if (! service->status_handle) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_REGISTERSERVICECTRLHANDER_FAILED, error_string(GetLastError()));
    cleanup_nssm_service(service);
    return;
  }

  if (!service->hook_section_initialised) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook critical section"), _T("service_main()"));
    service->status.dwCurrentState = SERVICE_STOPPED;
    service->status.dwWin32ExitCode = ERROR_NOT_ENOUGH_MEMORY;
    service->status.dwWaitHint = 0;
    report_service_status(service);
    return;
  }
  report_start_pending(service);
  log_service_control(service->name, 0, true);

  if (is_admin) create_exit_action(service->name, exit_action_strings[0], false);

  _tcscpy_s(service->displayname, service->name);
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (services) {
    unsigned long length = _countof(service->displayname);
    if (!GetServiceDisplayName(services, service->name, service->displayname, &length))
      _tcscpy_s(service->displayname, service->name);
    if (is_admin) {
      service->handle = open_service(services, service->name, SERVICE_CHANGE_CONFIG, 0, 0);
      set_service_recovery(service);
    }
    CloseServiceHandle(services);
  }

  service->resume_event = CreateEvent(NULL, FALSE, FALSE, NULL);
  service->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  service->finished_event = CreateEvent(NULL, TRUE, FALSE, NULL);

  /* Remember our initial environment. */
  service->initial_env = copy_environment();
  if (! service->initial_env || ! service->resume_event || ! service->stop_event || ! service->finished_event) {
    stop_service(service, ERROR_OUTOFMEMORY, true, true);
    return;
  }

  /* Remember our creation time. */
  if (get_process_creation_time(GetCurrentProcess(), &service->nssm_creation_time)) ZeroMemory(&service->nssm_creation_time, sizeof(service->nssm_creation_time));

  InterlockedExchange(&service->allow_restart, 1);
  service->power_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  HANDLE power_worker = service->power_port ? CreateThread(NULL, 0, dispatch_power_hooks, service, 0, NULL) : NULL;
  if (!power_worker) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_POWER_NOTIFICATIONS_UNAVAILABLE, service->name, error_string(GetLastError()));
    close_handle(&service->power_port);
  }
  /* The handler context and port live until the service process exits. */
  struct power_worker_owner {
    HANDLE port, worker;
    ~power_worker_owner() {
      if (!worker) return;
      PostQueuedCompletionStatus(port, 0, 0, NULL);
      WaitForSingleObject(worker, INFINITE);
      CloseHandle(worker);
    }
  } power = {service->power_port, power_worker};
  HANDLE launcher = CreateThread(NULL, 0, launch_service, service, 0, NULL);
  if (! launcher) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()));
    stop_service(service, ERROR_NOT_ENOUGH_MEMORY, true, true);
    return;
  }
  CloseHandle(launcher);
  HANDLE events[] = {service->finished_event, service->stop_event};
  if (WaitForMultipleObjects(_countof(events), events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
    {
      service_cleanup_lock state(service);
      if (service->terminal) return;
      service->last_control = service->stop_control;
      service->status.dwCurrentState = SERVICE_STOP_PENDING;
      service->status.dwControlsAccepted = 0;
      service->status.dwWaitHint = NSSM_SERVICE_STATUS_DEADLINE + NSSM_WAITHINT_MARGIN;
      report_service_status(service);
    }
    nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_STOP, NSSM_HOOK_ACTION_PRE, &service->stop_control, NSSM_SERVICE_STATUS_DEADLINE, false);
    stop_service(service, 0, true, true);
  }
}

/* Make sure service recovery actions are taken where necessary */
void set_service_recovery(nssm_service_t *service) {
  SERVICE_FAILURE_ACTIONS_FLAG flag;
  ZeroMemory(&flag, sizeof(flag));
  flag.fFailureActionsOnNonCrashFailures = true;

  /* This functionality was added in Vista so the call may fail */
  if (! ChangeServiceConfig2(service->handle, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag)) {
    unsigned long error = GetLastError();
    /* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
    if (error != ERROR_INVALID_LEVEL) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SERVICE_CONFIG_FAILURE_ACTIONS_FAILED, service->name, error_string(error));
    }
  }
}

int monitor_service(nssm_service_t *service) {
  /* Set service status to started */
  int ret = start_service(service);
  if (ret) {
    TCHAR code[16];
    _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%d"), ret);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, service->exe, service->name, code);
    return ret;
  }
  return 0;
}

TCHAR *service_control_text(unsigned long control) {
  switch (control) {
    /* HACK: there is no SERVICE_CONTROL_START constant */
    case NSSM_SERVICE_CONTROL_START: return _T("START");
    case SERVICE_CONTROL_STOP: return _T("STOP");
    case SERVICE_CONTROL_SHUTDOWN: return _T("SHUTDOWN");
    case SERVICE_CONTROL_PAUSE: return _T("PAUSE");
    case SERVICE_CONTROL_CONTINUE: return _T("CONTINUE");
    case SERVICE_CONTROL_INTERROGATE: return _T("INTERROGATE");
    case NSSM_SERVICE_CONTROL_ROTATE: return _T("ROTATE");
    case SERVICE_CONTROL_POWEREVENT: return _T("POWEREVENT");
    default: return 0;
  }
}

TCHAR *service_status_text(unsigned long status) {
  switch (status) {
    case SERVICE_STOPPED: return _T("SERVICE_STOPPED");
    case SERVICE_START_PENDING: return _T("SERVICE_START_PENDING");
    case SERVICE_STOP_PENDING: return _T("SERVICE_STOP_PENDING");
    case SERVICE_RUNNING: return _T("SERVICE_RUNNING");
    case SERVICE_CONTINUE_PENDING: return _T("SERVICE_CONTINUE_PENDING");
    case SERVICE_PAUSE_PENDING: return _T("SERVICE_PAUSE_PENDING");
    case SERVICE_PAUSED: return _T("SERVICE_PAUSED");
    default: return 0;
  }
}

void log_service_control(TCHAR *service_name, unsigned long control, bool handled) {
  TCHAR *text = service_control_text(control);
  unsigned long event;

  if (! text) {
    /* "0x" + 8 x hex + NULL */
    text = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, 11 * sizeof(TCHAR));
    if (! text) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("control code"), _T("log_service_control()"));
      return;
    }
    if (_sntprintf_s(text, 11, _TRUNCATE, _T("0x%08x"), control) < 0) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("control code"), _T("log_service_control()"));
      HeapFree(GetProcessHeap(), 0, text);
      return;
    }

    event = NSSM_EVENT_SERVICE_CONTROL_UNKNOWN;
  }
  else if (handled) event = NSSM_EVENT_SERVICE_CONTROL_HANDLED;
  else event = NSSM_EVENT_SERVICE_CONTROL_NOT_HANDLED;

  log_event(EVENTLOG_INFORMATION_TYPE, event, service_name, text);

  if (event == NSSM_EVENT_SERVICE_CONTROL_UNKNOWN) {
    HeapFree(GetProcessHeap(), 0, text);
  }
}

/* Service control handler */
unsigned long WINAPI service_control_handler(unsigned long control, unsigned long event, void *data, void *context) {
  nssm_service_t *service = (nssm_service_t *) context;

  switch (control) {
    case SERVICE_CONTROL_INTERROGATE:
      /* We always keep the service status up-to-date so this is a no-op. */
      return NO_ERROR;

    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
      log_service_control(service->name, control, true);

      if (InterlockedExchange(&service->allow_restart, 0)) {
        service->stop_control = control;
        SetEvent(service->stop_event);
      }
      return NO_ERROR;

    case SERVICE_CONTROL_CONTINUE: {
      service_cleanup_lock state(service);
      if (! restart_allowed(service) || service->status.dwCurrentState != SERVICE_PAUSED) return NO_ERROR;
      service->last_control = control;
      service->throttle = 0;
      service->status.dwCurrentState = SERVICE_CONTINUE_PENDING;
      service->status.dwWaitHint = NSSM_WAITHINT_MARGIN;
      report_service_status(service);
      SetEvent(service->resume_event);
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_RESET_THROTTLE, service->name);
      return NO_ERROR;
    }

    case SERVICE_CONTROL_PAUSE:
      /*
        We don't accept pause messages but it isn't possible to register
        only for continue messages so we have to handle this case.
      */
      log_service_control(service->name, control, false);
      return ERROR_CALL_NOT_IMPLEMENTED;

    case NSSM_SERVICE_CONTROL_ROTATE:
      log_service_control(service->name, control, true);
      if (! restart_allowed(service) || InterlockedCompareExchange(&service->rotate_active, 1, 0)) return NO_ERROR;
      {
        service->rotate_generation = (unsigned long) InterlockedCompareExchange(&service->start_count, 0, 0);
        HANDLE worker = CreateThread(NULL, 0, rotate_service, service, 0, NULL);
        if (! worker) {
          DWORD error = GetLastError();
          InterlockedExchange(&service->rotate_active, 0);
          return error;
        }
        CloseHandle(worker);
      }
      return NO_ERROR;

    case SERVICE_CONTROL_POWEREVENT:
      if (event == PBT_APMRESUMEAUTOMATIC || event == PBT_APMPOWERSTATUSCHANGE) {
        if (!restart_allowed(service)) return NO_ERROR;
        if (!service->power_port) return ERROR_CALL_NOT_IMPLEMENTED;
        DWORD generation = InterlockedCompareExchange(&service->start_count, 0, 0);
        if (!PostQueuedCompletionStatus(service->power_port, generation, event, NULL)) return GetLastError();
        return NO_ERROR;
      }
      log_service_control(service->name, control, false);
      return NO_ERROR;
  }

  /* Unknown control */
  log_service_control(service->name, control, false);
  return ERROR_CALL_NOT_IMPLEMENTED;
}

/* A failed relaunch with no published child still belongs to the retry loop. */
static int launch_failure(nssm_service_t *service, DWORD code) {
  if (! restart_allowed(service)) return 0;
  if (! service->exit_count || service->process_handle) return stop_service(service, code, true, true);
  cleanup_loggers(service);
  service->status.dwCurrentState = SERVICE_PAUSED;
  service->status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  report_service_status(service);
  return code;
}

/* A Start/Pre hook that redirects output inherits its own set of handles.
   Keep the application's pending startup handles private until its launch. */
static bool set_launch_handle_inheritance(STARTUPINFO *si, bool inherit) {
  if (!(si->dwFlags & STARTF_USESTDHANDLES)) return true;
  DWORD flags = inherit ? HANDLE_FLAG_INHERIT : 0;
  return SetHandleInformation(si->hStdInput, HANDLE_FLAG_INHERIT, flags) &&
         SetHandleInformation(si->hStdOutput, HANDLE_FLAG_INHERIT, flags) &&
         SetHandleInformation(si->hStdError, HANDLE_FLAG_INHERIT, flags);
}

/* Start the service */
int start_service(nssm_service_t *service) {
  service_cleanup_lock state(service);
  if (! restart_allowed(service)) return 0;
  if (service->process_handle) return 0;
  DWORD previous_wait = unregister_process_wait(service, false);
  if (previous_wait) return stop_service(service, previous_wait, true, true);
  service->start_requested_count++;
  HANDLE startup_process = NULL;

  /* Allocate a STARTUPINFO structure for a new process */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  /* Allocate a PROCESSINFO structure for the process */
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  /* Get startup parameters */
  int ret = get_parameters(service, &si);
  if (ret) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_PARAMETERS_FAILED, service->name);
    unset_service_environment(service);
    return launch_failure(service, 2);
  }

  /* Launch executable with arguments */
  text_buffer<CMD_LENGTH> command;
  TCHAR *cmd = command.get();
  if (!cmd || _sntprintf_s(cmd, command.size(), _TRUNCATE, _T("\"%s\" %s"), service->exe, service->flags) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("command line"), _T("start_service"));
    unset_service_environment(service);
    return launch_failure(service, 2);
  }

  /* Registry expansion temporarily changes the process environment. Restore
     it before releasing state ownership for a potentially long throttle. */
  unset_service_environment(service);
  state.unlock();
  throttle_restart(service);
  state.lock();
  if (! restart_allowed(service)) return 0;
  service->stopping = false;

  service->status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
  if (service->power_port) service->status.dwControlsAccepted |= SERVICE_ACCEPT_POWEREVENT;
  report_start_pending(service);

  unsigned long control = NSSM_SERVICE_CONTROL_START;

  /* Did another thread receive a stop control? */
  if (restart_allowed(service)) {
    /* Set up I/O redirection. */
    if (get_output_handles(service, &si) || !set_launch_handle_inheritance(&si, false)) {
      if (restart_allowed(service)) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name);
      FreeConsole();
      close_output_handles(&si);
      unset_service_environment(service);
      return launch_failure(service, 4);
    }
    FreeConsole();

    /* Pre-start hook. May need I/O to have been redirected already. */
    if (nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, &control, NSSM_SERVICE_STATUS_DEADLINE, false) == NSSM_HOOK_STATUS_ABORT) {
      TCHAR code[16];
      _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), NSSM_HOOK_STATUS_ABORT);
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PRESTART_HOOK_ABORT, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE, service->name, code);
      close_output_handles(&si);
      unset_service_environment(service);
      return stop_service(service, 5, true, true);
    }

    if (! restart_allowed(service)) {
      close_output_handles(&si);
      unset_service_environment(service);
      return 0;
    }

    if (set_service_environment(service)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_PARAMETERS_FAILED, service->name);
      close_output_handles(&si);
      unset_service_environment(service);
      return launch_failure(service, 2);
    }

    if (!set_launch_handle_inheritance(&si, true)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name);
      close_output_handles(&si);
      unset_service_environment(service);
      return launch_failure(service, 4);
    }
    bool inherit_handles = (si.dwFlags & STARTF_USESTDHANDLES) != 0;
    unsigned long flags = service->priority & priority_mask();
    if (service->affinity) flags |= CREATE_SUSPENDED;
    flags |= service->no_console ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE;

    report_start_pending(service);
    if (! CreateProcess(0, cmd, 0, 0, inherit_handles, flags, 0, service->dir, &si, &pi)) {
      unsigned long exitcode = 3;
      unsigned long error = GetLastError();
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPROCESS_FAILED, service->name, service->exe, error_string(error));
      close_output_handles(&si);
      unset_service_environment(service);
      return launch_failure(service, exitcode);
    }
    InterlockedIncrement(&service->start_count);
    service->process_handle = pi.hProcess;
    service->pid = pi.dwProcessId;

    if (get_process_creation_time(service->process_handle, &service->creation_time)) ZeroMemory(&service->creation_time, sizeof(service->creation_time));

    close_output_handles(&si);

    if (service->affinity) {
      /*
        We are explicitly storing service->affinity as a 64-bit unsigned integer
        so that we can parse it regardless of whether we're running in 32-bit
        or 64-bit mode.  The arguments to SetProcessAffinityMask(), however, are
        defined as type DWORD_PTR and hence limited to 32 bits on a 32-bit system
        (or when running the 32-bit NSSM).

        The result is a lot of seemingly-unnecessary casting throughout the code
        and potentially confusion when we actually try to start the service.
        Having said that, however, it's unlikely that we're actually going to
        run in 32-bit mode on a system which has more than 32 CPUs so the
        likelihood of seeing a confusing situation is somewhat diminished.
      */
      DWORD_PTR affinity, system_affinity;

      if (GetProcessAffinityMask(service->process_handle, &affinity, &system_affinity)) affinity = service->affinity & system_affinity;
      else {
        affinity = (DWORD_PTR) service->affinity;
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSAFFINITYMASK_FAILED, service->name, error_string(GetLastError()));
      }

      if (! SetProcessAffinityMask(service->process_handle, affinity)) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_SETPROCESSAFFINITYMASK_FAILED, service->name, error_string(GetLastError()));
      }

      if (ResumeThread(pi.hThread) == (DWORD) -1) {
        unsigned long error = GetLastError();
        CloseHandle(pi.hThread);
        TCHAR code[16];
        _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), error);
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, service->exe, service->name, code);
        unset_service_environment(service);
        return stop_service(service, 3, true, true);
      }
    }
    CloseHandle(pi.hThread);
    if (! DuplicateHandle(GetCurrentProcess(), service->process_handle, GetCurrentProcess(), &startup_process, SYNCHRONIZE, false, 0)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, _T("application process"), _T("startup wait"), error_string(GetLastError()));
      unset_service_environment(service);
      return stop_service(service, 3, true, true);
    }
  }

  /* Restore our environment. */
  unset_service_environment(service);

  /*
    Wait for a clean startup before changing the service status to RUNNING
    but be mindful of the fact that we are blocking the service control manager
    so abandon the wait before too much time has elapsed.
  */
  unsigned long startup_delay = service->throttle_delay;
  state.unlock();
  startup_result waited = startup_process ? await_startup(service, startup_process, startup_delay) : startup_result::cancelled;
  DWORD wait_error = waited == startup_result::failed ? GetLastError() : ERROR_SUCCESS;
  if (startup_process) CloseHandle(startup_process);
  state.lock();

  /* Did another thread receive a stop control? */
  if (! restart_allowed(service) || service->stopping) return 0;
  if (!service->process_handle || waited == startup_result::cancelled) return 0;
  if (waited == startup_result::failed) return stop_service(service, wait_error ? wait_error : ERROR_GEN_FAILURE, true, true);
  if (waited == startup_result::alive) service->throttle = 0;

  /* Monitor service */
  HANDLE registration;
  if (! RegisterWaitForSingleObject(&registration, service->process_handle, end_service, (void *) service, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION)) {
    DWORD error = GetLastError();
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_REGISTERWAITFORSINGLEOBJECT_FAILED, service->name, service->exe, error_string(error));
    return stop_service(service, error ? error : ERROR_GEN_FAILURE, true, true);
  }

  service->wait_handle = registration;

  /* A signalled process already has its one-shot exit callback queued. */
  if (waited == startup_result::exited) return 0;

  /* Signal successful start */
  service->status.dwCurrentState = SERVICE_RUNNING;
  service->status.dwControlsAccepted &= ~SERVICE_ACCEPT_PAUSE_CONTINUE;
  report_service_status(service);

  /* Post-start hook. */
  if (! service->throttle) {
    (void) nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_POST, &control);
  }

  if (restart_allowed(service)) log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_STARTED_SERVICE, service->exe, service->flags, service->name, service->dir);
  return 0;
}

/* Stop the service */
int stop_service(nssm_service_t *service, unsigned long exitcode, bool graceful, bool default_action) {
  InterlockedExchange(&service->allow_restart, 0);
  service_cleanup_lock cleanup(service);
  if (service->terminal) return service->status.dwServiceSpecificExitCode;
  service->terminal = true;
  if (service->resume_event) SetEvent(service->resume_event);
  DWORD wait_error = unregister_process_wait(service, false);
  if (wait_error && !exitcode) exitcode = wait_error;

  service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;
  set_logger_rotation(service, false);

  if (default_action && ! exitcode && ! graceful) {
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_GRACEFUL_SUICIDE, service->name, service->exe, exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_REALLY]);
    graceful = true;
  }

  /* Signal we are stopping */
  if (graceful) {
    service->status.dwCurrentState = SERVICE_STOP_PENDING;
    service->status.dwWaitHint = NSSM_WAITHINT_MARGIN;
    report_service_status(service);
  }

  /* Nothing to do if service isn't running */
  if (service->pid) {
    /* Shut down service */
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_TERMINATEPROCESS, service->name, service->exe);
    kill_t k;
    service_kill_t(service, &k);
    k.exitcode = 0;
    if (! kill_process(&k) && (k.stop_method & NSSM_STOP_METHOD_TERMINATE) && ! exitcode) exitcode = ERROR_PROCESS_ABORTED;
  }
  else log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_PROCESS_ALREADY_STOPPED, service->name, service->exe);

  end_service((void *) service, true);

  /* Signal we stopped */
  if (graceful) {
    service->status.dwCurrentState = SERVICE_STOP_PENDING;
    wait_for_hooks(service, true);
    service->status.dwCurrentState = SERVICE_STOPPED;
    if (exitcode) {
      service->status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
      service->status.dwServiceSpecificExitCode = exitcode;
    }
    else {
      service->status.dwWin32ExitCode = NO_ERROR;
      service->status.dwServiceSpecificExitCode = 0;
    }
    report_service_status(service);
  }

  if (graceful && service->finished_event) SetEvent(service->finished_event);
  return exitcode;
}

/* Callback function triggered when the server exits */
void CALLBACK end_service(void *arg, unsigned char why) {
  nssm_service_t *service = (nssm_service_t *) arg;

  unsigned long exitcode = 0;
  TCHAR code[16];
  {
    service_cleanup_lock cleanup(service);
    if (service->stopping) return;

    service->stopping = true;

    service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;
    set_logger_rotation(service, false);

    /* Use now as a dummy exit time. */
    GetSystemTimeAsFileTime(&service->exit_time);

    /* Check exit code */
    if (service->process_handle) {
      GetExitCodeProcess(service->process_handle, &exitcode);
      if (why && exitcode == STILL_ACTIVE && WaitForSingleObject(service->process_handle, 0) != WAIT_OBJECT_0) exitcode = 0;
      else get_process_exit_time(service->process_handle, &service->exit_time);
      service->exitcode = exitcode;
    }

    /*
      Log that the service ended BEFORE logging about killing the process
      tree.  See below for the possible values of the why argument.
    */
    if (! why) {
      _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), exitcode);
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ENDED_SERVICE, service->exe, service->name, code);
    }

    /* Clean up. */
    if (service->pid && service->kill_process_tree) {
      kill_t k;
      service_kill_t(service, &k);
      kill_process_tree(&k, service->pid);
    }
    /* Keep the root identity pinned until descendant cleanup has finished. */
    // Keep the watched handle valid if terminal cancellation failed.
    if (service->process_handle && (!why || !service->wait_handle)) {
      CloseHandle(service->process_handle);
      service->process_handle = NULL;
    }
    service->pid = 0;

    /* Exit hook. */
    service->exit_count++;
    (void) nssm_hook(&hook_threads, service, NSSM_HOOK_EVENT_EXIT, NSSM_HOOK_ACTION_POST, NULL, NSSM_HOOK_DEADLINE, true);

    /* Exit logging threads. */
    cleanup_loggers(service);

  }
  /* Restart and exit actions run after releasing cleanup ownership. */

  /*
    The why argument is true if our wait timed out or false otherwise.
    Our wait is infinite so why will never be true when called by the system.
    If it is indeed true, assume we were called from stop_service() because
    this is a controlled shutdown, and don't take any restart action.
  */
  if (why) return;
  if (! restart_allowed(service)) return;

  /* What action should we take? */
  int action = NSSM_EXIT_RESTART;
  TCHAR action_string[ACTION_LEN];
  bool default_action;
  if (! get_exit_action(service->name, &exitcode, action_string, &default_action)) {
    for (int i = 0; exit_action_strings[i]; i++) {
      if (! _tcsnicmp((const TCHAR *) action_string, exit_action_strings[i], ACTION_LEN)) {
        action = i;
        break;
      }
    }
  }

  switch (action) {
    /* Try to restart the service or return failure code to service manager */
    case NSSM_EXIT_RESTART:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_RESTART, service->name, code, exit_action_strings[action], service->exe);
      while (restart_allowed(service) && monitor_service(service) && restart_allowed(service)) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_RESTART_SERVICE_FAILED, service->exe, service->name);
        if (WaitForSingleObject(service->stop_event, 30000) != WAIT_TIMEOUT) break;
      }
    break;

    /* Do nothing, just like srvany would */
    case NSSM_EXIT_IGNORE: {
      /* Ignore explicitly keeps the wrapper running after its child exits. */
      service_cleanup_lock state(service);
      if (!restart_allowed(service)) return;
      if (service->status.dwCurrentState == SERVICE_START_PENDING) {
        service->status.dwCurrentState = SERVICE_RUNNING;
        service->status.dwControlsAccepted &= ~SERVICE_ACCEPT_PAUSE_CONTINUE;
        report_service_status(service);
      }
    }
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_IGNORE, service->name, code, exit_action_strings[action], service->exe);
      wait_for_hooks(service, false);
      return;
    break;

    /* Tell the service manager we are finished */
    case NSSM_EXIT_REALLY:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_REALLY, service->name, code, exit_action_strings[action]);
      if (InterlockedExchange(&service->allow_restart, 0)) stop_service(service, exitcode, true, default_action);
    break;

    /* Fake a crash so pre-Vista service managers will run recovery actions. */
    case NSSM_EXIT_UNCLEAN:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_UNCLEAN, service->name, code, exit_action_strings[action]);
      if (! InterlockedExchange(&service->allow_restart, 0)) return;
      stop_service(service, exitcode, false, default_action);
      wait_for_hooks(service, false);
      nssm_exit(exitcode);
  }
}

void throttle_restart(nssm_service_t *service) {
  service_cleanup_lock state(service);
  if (! restart_allowed(service)) return;
  unsigned long previous = service->throttle;
  if (service->throttle < 8) service->throttle++;
  if (! service->start_count) return;
  unsigned long backoff = previous ? throttle_milliseconds(service->throttle) : 0;
  unsigned long ms = service->restart_delay > backoff ? service->restart_delay : backoff;
  if (! ms) return;
  TCHAR milliseconds[16], threshold[16];
  _ultot_s(ms, milliseconds, _countof(milliseconds), 10);
  if (service->restart_delay > backoff) log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_RESTART_DELAY, service->name, milliseconds);
  else {
    _ultot_s(service->throttle_delay, threshold, _countof(threshold), 10);
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_THROTTLED, service->name, threshold, milliseconds);
  }
  ResetEvent(service->resume_event);
  service->status.dwCurrentState = SERVICE_PAUSED;
  service->status.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
  report_service_status(service);
  state.unlock();
  HANDLE events[] = {service->stop_event, service->resume_event};
  /* MAXDWORD is a valid configured delay, but means infinite to the API. */
  WaitForMultipleObjects(_countof(events), events, FALSE, ms == INFINITE ? INFINITE - 1 : ms);
}

/*
  When responding to a stop (or any other) request we need to set dwWaitHint to
  the number of milliseconds we expect the operation to take, and optionally
  increase dwCheckPoint.  If dwWaitHint milliseconds elapses without the
  operation completing or dwCheckPoint increasing, the system will consider the
  service to be hung.

  However the system will consider the service to be hung after 30000
  milliseconds regardless of the value of dwWaitHint if dwCheckPoint has not
  changed.  Therefore if we want to wait longer than that we must periodically
  increase dwCheckPoint.

  Report each bounded wait interval plus a scheduling margin and increment
  the checkpoint when entering the next interval.

  If the status_handle and service_status arguments are omitted, this function
  will not try to update the service manager but it will still log to the
  event log that it is waiting for a handle.

  Returns: 1 if the wait timed out.
           0 if the wait completed.
          -1 on error.
*/
int await_single_handle(SERVICE_STATUS_HANDLE status_handle, SERVICE_STATUS *status, HANDLE handle, TCHAR *name, TCHAR *function_name, unsigned long timeout) {
  /* A zero delay still checks the handle once, without publishing progress. */
  if (! timeout) {
    switch (WaitForSingleObject(handle, 0)) {
      case WAIT_OBJECT_0: return 0;
      case WAIT_TIMEOUT: return 1;
      default: return -1;
    }
  }
  unsigned long interval;
  unsigned long ret;
  unsigned long waited;
  TCHAR interval_milliseconds[16];
  TCHAR timeout_milliseconds[16];
  TCHAR waited_milliseconds[16];
  TCHAR *function = function_name;

  /* Add brackets to function name. */
  size_t funclen = _tcslen(function_name) + 3;
  TCHAR *func = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, funclen * sizeof(TCHAR));
  if (func) {
    if (_sntprintf_s(func, funclen, _TRUNCATE, _T("%s()"), function_name) > -1) function = func;
  }

  _sntprintf_s(timeout_milliseconds, _countof(timeout_milliseconds), _TRUNCATE, _T("%lu"), timeout);

  waited = 0;
  while (waited < timeout) {
    interval = timeout - waited;
    if (interval > NSSM_SERVICE_STATUS_DEADLINE) interval = NSSM_SERVICE_STATUS_DEADLINE;

    if (status && status_pending(status->dwCurrentState)) {
      status->dwWaitHint = interval + NSSM_WAITHINT_MARGIN;
      status->dwCheckPoint++;
      SetServiceStatus(status_handle, status);
    }

    if (waited) {
      _sntprintf_s(waited_milliseconds, _countof(waited_milliseconds), _TRUNCATE, _T("%lu"), waited);
      _sntprintf_s(interval_milliseconds, _countof(interval_milliseconds), _TRUNCATE, _T("%lu"), interval);
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_AWAITING_SINGLE_HANDLE, function, name, waited_milliseconds, interval_milliseconds, timeout_milliseconds);
    }

    switch (WaitForSingleObject(handle, interval)) {
      case WAIT_OBJECT_0:
        ret = 0;
        goto awaited;

      case WAIT_TIMEOUT:
        ret = 1;
        break;

      default:
        ret = -1;
        goto awaited;
    }

    waited += interval;
  }

awaited:
  if (func) HeapFree(GetProcessHeap(), 0, func);

  return ret;
}

int list_nssm_services(int argc, TCHAR **argv) {
  bool including_native = (argc > 0 && str_equiv(argv[0], _T("all")));

  /* Open service manager. */
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 1;
  }

  const DWORD page_bytes = 64 * 1024;
  ENUM_SERVICE_STATUS_PROCESS *status = (ENUM_SERVICE_STATUS_PROCESS *) HeapAlloc(GetProcessHeap(), 0, page_bytes);
  if (! status) {
    CloseServiceHandle(services);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("ENUM_SERVICE_STATUS_PROCESS"), _T("list_nssm_services()"));
    return 3;
  }

  DWORD resume = 0;
  int result = 0;
  while (true) {
    DWORD previous = resume, required = 0, count = 0;
    BOOL complete = EnumServicesStatusEx(services, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
        (LPBYTE) status, page_bytes, &required, &count, &resume, NULL);
    if (!complete) {
      DWORD error = GetLastError();
      if (error != ERROR_MORE_DATA || resume == previous) {
        print_message(stderr, NSSM_MESSAGE_ENUMSERVICESSTATUS_FAILED, error_string(error));
        result = !previous && error != ERROR_MORE_DATA ? 2 : 4;
        break;
      }
    }

    for (DWORD i = 0; i < count; i++) {
      const TCHAR *name = status[i].lpServiceName;
      if (!including_native) {
        HKEY key;
        if (open_registry(name, NULL, KEY_QUERY_VALUE, &key, false) != ERROR_SUCCESS) continue;
        DWORD type, length = 0;
        LONG error = RegQueryValueEx(key, NSSM_REG_EXE, NULL, &type, NULL, &length);
        RegCloseKey(key);
        if (error || (type != REG_SZ && type != REG_EXPAND_SZ) || length <= sizeof(TCHAR)) continue;
        SC_HANDLE handle = OpenService(services, name, SERVICE_QUERY_CONFIG);
        if (!handle) continue;
        QUERY_SERVICE_CONFIG *config = query_service_config(name, handle);
        CloseServiceHandle(handle);
        if (!config) continue;
        bool managed = wrapper_image(config->lpBinaryPathName);
        HeapFree(GetProcessHeap(), 0, config);
        if (!managed) continue;
      }
      if (_tprintf(_T("%s\n"), name) < 0) { result = 5; break; }
    }

    if (complete || result) break;
  }

  HeapFree(GetProcessHeap(), 0, status);
  CloseServiceHandle(services);
  if (!stdout_ok()) return 5;
  return result;
}

static DWORD query_service_pid(SC_HANDLE handle, DWORD *pid) {
  SERVICE_STATUS_PROCESS status;
  DWORD needed;
  *pid = 0;
  if (!QueryServiceStatusEx(handle, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) return GetLastError();
  switch (status.dwCurrentState) {
    case SERVICE_STOPPED:
      return ERROR_SUCCESS;
    case SERVICE_RUNNING:
    case SERVICE_PAUSED:
    case SERVICE_PAUSE_PENDING:
    case SERVICE_CONTINUE_PENDING:
      *pid = status.dwProcessId;
      return *pid ? ERROR_SUCCESS : ERROR_RETRY;
    default:
      return ERROR_RETRY;
  }
}

/* Keep the process object alive across the second SCM query so that its PID
   cannot be recycled between identity verification and tree enumeration. */
static DWORD open_service_process(SC_HANDLE handle, kill_t *root) {
  DWORD error = query_service_pid(handle, &root->pid);
  if (error || !root->pid) return error;
  HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, root->pid);
  if (!process) return GetLastError();

  DWORD current_pid;
  error = query_service_pid(handle, &current_pid);
  if (error || current_pid != root->pid) {
    CloseHandle(process);
    return error ? error : ERROR_RETRY;
  }
  root->process_handle = process;
  return ERROR_SUCCESS;
}

int service_process_tree(int argc, TCHAR **argv) {
  int errors = 0;
  if (argc < 1) return usage(1);

  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 1;
  }

  /*
    We need SeDebugPrivilege to read the process tree.
    We ignore failure here so that an error will be printed later when we
    try to open a process handle.
  */
  debug_token_t token;
  (void) get_debug_token(&token);

  TCHAR canonical_name[SERVICE_NAME_LENGTH];
  nssm_service_t *service;
  kill_t k;

  int i;
  for (i = 0; i < argc; i++) {
    TCHAR *service_name = argv[i];
    SC_HANDLE service_handle = open_service(services, service_name, SERVICE_QUERY_STATUS, canonical_name, _countof(canonical_name));
    if (! service_handle) {
      errors++;
      continue;
    }

    ZeroMemory(&k, sizeof(k));
    DWORD error = open_service_process(service_handle, &k);
    CloseServiceHandle(service_handle);
    if (error) {
      _ftprintf(stderr, _T("%s: %s\n"), canonical_name, error_string(error));
      errors++;
      continue;
    }
    if (!k.process_handle) continue;

    if (get_process_creation_time(k.process_handle, &k.creation_time)) {
      CloseHandle(k.process_handle);
      errors++;
      continue;
    }
    /* Dummy exit time so we can check processes' parents. */
    GetSystemTimeAsFileTime(&k.exit_time);

    service = alloc_nssm_service();
    if (! service) {
      CloseHandle(k.process_handle);
      errors++;
      continue;
    }

    _sntprintf_s(service->name, _countof(service->name), _TRUNCATE, _T("%s"), canonical_name);
    k.name = service->name;
    if (walk_process_tree(service, print_process, &k, k.pid)) errors++;
    CloseHandle(k.process_handle);

    cleanup_nssm_service(service);
  }

  CloseServiceHandle(services);
  release_debug_token(&token);
  return stdout_ok() ? errors : 1;
}
