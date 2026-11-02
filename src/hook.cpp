#include "nssm.h"
#include <limits.h>

typedef struct {
  TCHAR name[HOOK_NAME_LENGTH];
  HANDLE process_handle;
  unsigned long pid;
  unsigned long deadline;
  FILETIME creation_time;
  kill_t k;
} hook_t;

const TCHAR *hook_event_strings[] = { NSSM_HOOK_EVENT_START, NSSM_HOOK_EVENT_STOP, NSSM_HOOK_EVENT_EXIT, NSSM_HOOK_EVENT_POWER, NSSM_HOOK_EVENT_ROTATE, NULL };
const TCHAR *hook_action_strings[] = { NSSM_HOOK_ACTION_PRE, NSSM_HOOK_ACTION_POST, NSSM_HOOK_ACTION_CHANGE, NSSM_HOOK_ACTION_RESUME, NULL };

/* The worker exclusively owns this snapshot and its process handle. */
static unsigned long WINAPI await_hook(void *arg) {
  hook_t *hook = (hook_t *) arg;
  if (! hook) return NSSM_HOOK_STATUS_ERROR;

  DWORD waited = WaitForSingleObject(hook->process_handle, hook->deadline);
  int ret = NSSM_HOOK_STATUS_SUCCESS;
  if (waited == WAIT_TIMEOUT) ret = NSSM_HOOK_STATUS_TIMEOUT;
  else if (waited != WAIT_OBJECT_0) ret = NSSM_HOOK_STATUS_ERROR;

  /* Tidy up hook process tree using only worker-owned state. */
  hook->k.name = hook->name;
  hook->k.process_handle = hook->process_handle;
  hook->k.pid = hook->pid;
  hook->k.stop_method = ~0;
  hook->k.kill_console_delay = NSSM_KILL_CONSOLE_GRACE_PERIOD;
  hook->k.kill_window_delay = NSSM_KILL_WINDOW_GRACE_PERIOD;
  hook->k.kill_threads_delay = NSSM_KILL_THREADS_GRACE_PERIOD;
  hook->k.creation_time = hook->creation_time;
  GetSystemTimeAsFileTime(&hook->k.exit_time);
  kill_process_tree(&hook->k, hook->pid);

  if (! ret) {
    unsigned long exitcode;
    if (! GetExitCodeProcess(hook->process_handle, &exitcode)) ret = NSSM_HOOK_STATUS_ERROR;
    else if (exitcode == NSSM_HOOK_STATUS_ABORT) ret = NSSM_HOOK_STATUS_ABORT;
    else if (exitcode) ret = NSSM_HOOK_STATUS_FAILED;
  }
  CloseHandle(hook->process_handle);
  HeapFree(GetProcessHeap(), 0, hook);
  return ret;
}

static void set_hook_runtime(TCHAR *v, FILETIME *start, FILETIME *now) {
  if (start && now) {
    ULARGE_INTEGER s;
    s.LowPart = start->dwLowDateTime;
    s.HighPart = start->dwHighDateTime;
    if (s.QuadPart) {
      ULARGE_INTEGER t;
      t.LowPart = now->dwLowDateTime;
      t.HighPart = now->dwHighDateTime;
      if (t.QuadPart && t.QuadPart >= s.QuadPart) {
        t.QuadPart -= s.QuadPart;
        t.QuadPart /= 10000LL;
        TCHAR number[16];
        _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%llu"), t.QuadPart);
        SetEnvironmentVariable(v, number);
        return;
      }
    }
  }
  SetEnvironmentVariable(v, _T(""));
}

/* Caller holds service->hook_section. Reserve before publishing a worker. */
static bool reserve_thread_handle(hook_thread_t *hook_threads) {
  if (! hook_threads) return false;
  if (hook_threads->num_threads < 0 || hook_threads->num_threads == INT_MAX) return false;
  size_t count = (size_t) hook_threads->num_threads + 1;
  if (count > ((size_t) -1) / sizeof(hook_thread_data_t)) return false;
  hook_thread_data_t *data = (hook_thread_data_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*data));
  if (! data) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook_thread_t"), _T("reserve_thread_handle()"));
    return false;
  }
  if (hook_threads->num_threads) memmove(data, hook_threads->data, hook_threads->num_threads * sizeof(*data));
  if (hook_threads->data) HeapFree(GetProcessHeap(), 0, hook_threads->data);
  hook_threads->data = data;
  return true;
}

static void free_empty_thread_list(hook_thread_t *hook_threads) {
  if (! hook_threads || hook_threads->num_threads) return;
  if (hook_threads->data) HeapFree(GetProcessHeap(), 0, hook_threads->data);
  hook_threads->data = NULL;
}

bool valid_hook_name(const TCHAR *hook_event, const TCHAR *hook_action, bool quiet) {
  bool valid_event = false;
  bool valid_action = false;

  /* Exit/Post */
  if (str_equiv(hook_event, NSSM_HOOK_EVENT_EXIT)) {
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_POST);
    return false;
  }

  /* Power/{Change,Resume} */
  if (str_equiv(hook_event, NSSM_HOOK_EVENT_POWER)) {
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_CHANGE)) return true;
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_RESUME)) return true;
    if (quiet) return false;
    print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_CHANGE);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_RESUME);
    return false;
  }

  /* Rotate/{Pre,Post} */
  if (str_equiv(hook_event, NSSM_HOOK_EVENT_ROTATE)) {
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_PRE)) return true;
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_PRE);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_POST);
    return false;
  }

  /* Start/{Pre,Post} */
  if (str_equiv(hook_event, NSSM_HOOK_EVENT_START)) {
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_PRE)) return true;
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_PRE);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_POST);
    return false;
  }

  /* Stop/Pre */
  if (str_equiv(hook_event, NSSM_HOOK_EVENT_STOP)) {
    if (str_equiv(hook_action, NSSM_HOOK_ACTION_PRE)) return true;
    if (quiet) return false;
    print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_ACTION_PRE);
    return false;
  }

  if (quiet) return false;
  print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_EVENT);
  _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_EVENT_EXIT);
  _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_EVENT_POWER);
  _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_EVENT_ROTATE);
  _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_EVENT_START);
  _ftprintf(stderr, _T("%s\n"), NSSM_HOOK_EVENT_STOP);
  return false;
}

/* The service hook critical section serializes append, wait and compaction. */
void await_hook_threads(hook_thread_t *hook_threads, SERVICE_STATUS_HANDLE status_handle, SERVICE_STATUS *status, unsigned long deadline) {
  if (! hook_threads) return;
  int retained = 0;
  for (int i = 0; i < hook_threads->num_threads; i++) {
    hook_thread_data_t entry = hook_threads->data[i];
    bool complete;
    if (deadline) complete = await_single_handle(status_handle, status, entry.thread_handle, entry.name, _T(__FUNCTION__), deadline) == 0;
    else complete = WaitForSingleObject(entry.thread_handle, 0) == WAIT_OBJECT_0;
    if (complete) CloseHandle(entry.thread_handle);
    else {
      /* Retain live workers on timeout or wait failure. No allocation needed. */
      if (retained != i) hook_threads->data[retained] = entry;
      retained++;
    }
  }
  hook_threads->num_threads = retained;
  free_empty_thread_list(hook_threads);
}

/*
   Returns:
   NSSM_HOOK_STATUS_SUCCESS  if the hook ran successfully.
   NSSM_HOOK_STATUS_NOTFOUND if no hook was found.
   NSSM_HOOK_STATUS_ABORT    if the hook failed and we should cancel service start.
   NSSM_HOOK_STATUS_ERROR    on error.
   NSSM_HOOK_STATUS_NOTRUN   if the hook didn't run.
   NSSM_HOOK_STATUS_TIMEOUT  if the hook timed out.
   NSSM_HOOK_STATUS_FAILED   if the hook failed.
*/
int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control, unsigned long deadline, bool async) {
  int ret = 0;

  hook_t *hook = (hook_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(hook_t));
  if (! hook) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook"), _T("nssm_hook()"));
    return NSSM_HOOK_STATUS_ERROR;
  }

  FILETIME now;
  GetSystemTimeAsFileTime(&now);

  EnterCriticalSection(&service->hook_section);

  /* Set the environment. */
  set_service_environment(service);

  /* ABI version. */
  TCHAR number[16];
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), NSSM_HOOK_VERSION);
  SetEnvironmentVariable(NSSM_HOOK_ENV_VERSION, number);

  /* Event triggering this action. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_EVENT, hook_event);

  /* Hook action. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_ACTION, hook_action);

  /* Control triggering this action.  May be empty. */
  if (hook_control) SetEnvironmentVariable(NSSM_HOOK_ENV_TRIGGER, service_control_text(*hook_control));
  else SetEnvironmentVariable(NSSM_HOOK_ENV_TRIGGER, _T(""));

  /* Last control handled. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_LAST_CONTROL, service_control_text(service->last_control));

  /* Path to NSSM, unquoted for the environment. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_IMAGE_PATH, nssm_unquoted_imagepath());

  /* NSSM version. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_NSSM_CONFIGURATION, NSSM_CONFIGURATION);
  SetEnvironmentVariable(NSSM_HOOK_ENV_NSSM_VERSION, NSSM_VERSION);
  SetEnvironmentVariable(NSSM_HOOK_ENV_BUILD_DATE, NSSM_DATE);

  /* NSSM PID. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), GetCurrentProcessId());
  SetEnvironmentVariable(NSSM_HOOK_ENV_PID, number);

  /* NSSM runtime. */
  set_hook_runtime(NSSM_HOOK_ENV_RUNTIME, &service->nssm_creation_time, &now);

  /* Application PID. */
  if (service->pid) {
    _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->pid);
    SetEnvironmentVariable(NSSM_HOOK_ENV_APPLICATION_PID, number);
    /* Application runtime. */
    set_hook_runtime(NSSM_HOOK_ENV_APPLICATION_RUNTIME, &service->creation_time, &now);
    /* Exit code. */
    SetEnvironmentVariable(NSSM_HOOK_ENV_EXITCODE, _T(""));
  }
  else {
    SetEnvironmentVariable(NSSM_HOOK_ENV_APPLICATION_PID, _T(""));
    if (str_equiv(hook_event, NSSM_HOOK_EVENT_START) && str_equiv(hook_action, NSSM_HOOK_ACTION_PRE)) {
      SetEnvironmentVariable(NSSM_HOOK_ENV_APPLICATION_RUNTIME, _T(""));
      SetEnvironmentVariable(NSSM_HOOK_ENV_EXITCODE, _T(""));
    }
    else {
      set_hook_runtime(NSSM_HOOK_ENV_APPLICATION_RUNTIME, &service->creation_time, &service->exit_time);
      /* Exit code. */
      _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->exitcode);
      SetEnvironmentVariable(NSSM_HOOK_ENV_EXITCODE, number);
    }
  }

  /* Deadline for this script. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), deadline);
  SetEnvironmentVariable(NSSM_HOOK_ENV_DEADLINE, number);

  /* Service name. */
  SetEnvironmentVariable(NSSM_HOOK_ENV_SERVICE_NAME, service->name);
  SetEnvironmentVariable(NSSM_HOOK_ENV_SERVICE_DISPLAYNAME, service->displayname);

  /* Times the service was asked to start. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->start_requested_count);
  SetEnvironmentVariable(NSSM_HOOK_ENV_START_REQUESTED_COUNT, number);

  /* Times the service actually did start. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->start_count);
  SetEnvironmentVariable(NSSM_HOOK_ENV_START_COUNT, number);

  /* Times the service exited. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->exit_count);
  SetEnvironmentVariable(NSSM_HOOK_ENV_EXIT_COUNT, number);

  /* Throttled count. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->throttle);
  SetEnvironmentVariable(NSSM_HOOK_ENV_THROTTLE_COUNT, number);

  /* Command line. */
  TCHAR app[CMD_LENGTH];
  _sntprintf_s(app, _countof(app), _TRUNCATE, _T("\"%s\" %s"), service->exe, service->flags);
  SetEnvironmentVariable(NSSM_HOOK_ENV_COMMAND_LINE, app);

  TCHAR cmd[CMD_LENGTH];
  if (get_hook(service->name, hook_event, hook_action, cmd, sizeof(cmd))) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_HOOK_FAILED, hook_event, hook_action, service->name);
    unset_service_environment(service);
    LeaveCriticalSection(&service->hook_section);
    HeapFree(GetProcessHeap(), 0, hook);
    return NSSM_HOOK_STATUS_ERROR;
  }

  /* No hook. */
  if (! _tcslen(cmd)) {
    unset_service_environment(service);
    LeaveCriticalSection(&service->hook_section);
    HeapFree(GetProcessHeap(), 0, hook);
    return NSSM_HOOK_STATUS_NOTFOUND;
  }

  /* Run the command. */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  if (service->hook_share_output_handles) (void) use_output_handles(service, &si);
  bool inherit_handles = false;
  if (si.dwFlags & STARTF_USESTDHANDLES) inherit_handles = true;
  unsigned long flags = 0;
#ifdef UNICODE
  flags |= CREATE_UNICODE_ENVIRONMENT;
#endif
  ret = NSSM_HOOK_STATUS_NOTRUN;
  if (CreateProcess(0, cmd, 0, 0, inherit_handles, flags, 0, service->dir, &si, &pi)) {
    close_output_handles(&si);
    CloseHandle(pi.hThread);
    _sntprintf_s(hook->name, _countof(hook->name), _TRUNCATE, _T("%s (%s/%s)"), service->name, hook_event, hook_action);
    hook->process_handle = pi.hProcess;
    hook->pid = pi.dwProcessId;
    hook->deadline = deadline;
    if (get_process_creation_time(hook->process_handle, &hook->creation_time)) GetSystemTimeAsFileTime(&hook->creation_time);

    /* Neither registration nor synchronous waits may read hook after CreateThread. */
    hook_thread_data_t entry = {0};
    memmove(entry.name, hook->name, sizeof(entry.name));
    await_hook_threads(hook_threads, service->status_handle, &service->status, 0);
    if (! reserve_thread_handle(hook_threads)) ret = (int) await_hook(hook);
    else {
      unsigned long tid;
      entry.thread_handle = CreateThread(NULL, 0, await_hook, (void *) hook, 0, &tid);
      if (! entry.thread_handle) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()));
        ret = (int) await_hook(hook);
      }
      else {
        hook_threads->data[hook_threads->num_threads++] = entry;
        ret = NSSM_HOOK_STATUS_SUCCESS;
        if (! async) {
          /* Avoid wrapping a large deadline to zero or the INFINITE sentinel. */
          unsigned long timeout = deadline;
          if (timeout >= MAXDWORD - NSSM_SERVICE_STATUS_DEADLINE) timeout = MAXDWORD - 1;
          else timeout += NSSM_SERVICE_STATUS_DEADLINE;
          int waited = await_single_handle(service->status_handle, &service->status, entry.thread_handle, entry.name, _T(__FUNCTION__), timeout);
          if (! waited) {
            unsigned long exitcode;
            ret = GetExitCodeThread(entry.thread_handle, &exitcode) ? (int) exitcode : NSSM_HOOK_STATUS_ERROR;
            CloseHandle(entry.thread_handle);
            hook_threads->num_threads--;
          }
          else ret = waited == 1 ? NSSM_HOOK_STATUS_TIMEOUT : NSSM_HOOK_STATUS_ERROR;
          /* A still-running worker remains tracked for the next cleanup. */
        }
      }
      free_empty_thread_list(hook_threads);
    }
  }
  else {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_HOOK_CREATEPROCESS_FAILED, hook_event, hook_action, service->name, cmd, error_string(GetLastError()));
    HeapFree(GetProcessHeap(), 0, hook);
    close_output_handles(&si);
  }

  /* Restore our environment. */
  unset_service_environment(service);

  LeaveCriticalSection(&service->hook_section);

  return ret;
}

int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control, unsigned long deadline) {
  return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, deadline, true);
}

int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control) {
  return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, NSSM_HOOK_DEADLINE);
}
