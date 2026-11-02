#include "nssm.h"

extern const TCHAR *exit_action_strings[];

static int service_registry_path(const TCHAR *service_name, bool parameters, const TCHAR *sub, TCHAR *buffer, unsigned long buflen) {
  int ret;

  if (parameters) {
    if (sub) ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY _T("\\") NSSM_REG_PARAMETERS _T("\\%s"), service_name, sub);
    else ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY _T("\\") NSSM_REG_PARAMETERS, service_name);
  }
  else ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY, service_name);

  return ret;
}

static long open_registry_key(const TCHAR *registry, REGSAM sam, HKEY *key, bool must_exist) {
  long error;

  if (sam & KEY_SET_VALUE) {
    error = RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, sam, 0, key, 0);
    if (error != ERROR_SUCCESS) {
      *key = 0;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()));
      return error;
    }
  }
  else {
    error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, sam, key);
    if (error != ERROR_SUCCESS) {
      *key = 0;
      if (error != ERROR_FILE_NOT_FOUND || must_exist) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()));
    }
  }

  return error;
}

static HKEY open_registry_key(const TCHAR *registry, REGSAM sam, bool must_exist) {
  HKEY key;
  long error = open_registry_key(registry, sam, &key, must_exist);
  return key;
}

int create_messages() {
  HKEY key;

  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s"), NSSM) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("eventlog registry"), _T("create_messages()"));
    return 1;
  }

  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()));
    return 2;
  }

  /* Get path of this program */
  const TCHAR *path = nssm_unquoted_imagepath();

  /* Try to register the module but don't worry so much on failure */
  RegSetValueEx(key, _T("EventMessageFile"), 0, REG_SZ, (const unsigned char *) path, (unsigned long) (_tcslen(path) +  1) * sizeof(TCHAR));
  unsigned long types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
  RegSetValueEx(key, _T("TypesSupported"), 0, REG_DWORD, (const unsigned char *) &types, sizeof(types));

  return 0;
}

long enumerate_registry_values(HKEY key, unsigned long *index, TCHAR *name, unsigned long namelen) {
  unsigned long type;
  unsigned long datalen = namelen;
  long error = RegEnumValue(key, *index, name, &datalen, 0, &type, 0, 0);
  if (error == ERROR_SUCCESS) ++*index;
  return error;
}

int create_parameters(nssm_service_t *service, bool editing) {
  /* Try to open the registry */
  HKEY key = open_registry(service->name, KEY_WRITE);
  if (! key) return 1;

  /* Remember parameters in case we need to delete them. */
  TCHAR registry[KEY_LENGTH];
  int ret = service_registry_path(service->name, true, 0, registry, _countof(registry));

  /* Try to create the parameters */
  if (set_expand_string(key, NSSM_REG_EXE, service->exe)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
    RegCloseKey(key);
    return 2;
  }
  if (set_expand_string(key, NSSM_REG_FLAGS, service->flags)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
    RegCloseKey(key);
    return 3;
  }
  if (set_expand_string(key, NSSM_REG_DIR, service->dir)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
    RegCloseKey(key);
    return 4;
  }

  /* Other non-default parameters. May fail. */
  if (service->priority != NORMAL_PRIORITY_CLASS) set_number(key, NSSM_REG_PRIORITY, service->priority);
  else if (editing) RegDeleteValue(key, NSSM_REG_PRIORITY);
  if (service->affinity) {
    TCHAR *string;
    if (! affinity_mask_to_string(service->affinity, &string)) {
      if (RegSetValueEx(key, NSSM_REG_AFFINITY, 0, REG_SZ, (const unsigned char *) string, (unsigned long) (_tcslen(string) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_AFFINITY, error_string(GetLastError()));
        HeapFree(GetProcessHeap(), 0, string);
        return 5;
      }
    }
    if (string) HeapFree(GetProcessHeap(), 0, string);
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_AFFINITY);
  unsigned long stop_method_skip = ~service->stop_method;
  if (stop_method_skip) set_number(key, NSSM_REG_STOP_METHOD_SKIP, stop_method_skip);
  else if (editing) RegDeleteValue(key, NSSM_REG_STOP_METHOD_SKIP);
  if (service->default_exit_action < NSSM_NUM_EXIT_ACTIONS) create_exit_action(service->name, exit_action_strings[service->default_exit_action], editing);
  if (service->restart_delay) set_number(key, NSSM_REG_RESTART_DELAY, service->restart_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_RESTART_DELAY);
  if (service->throttle_delay != NSSM_RESET_THROTTLE_RESTART) set_number(key, NSSM_REG_THROTTLE, service->throttle_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_THROTTLE);
  if (service->kill_console_delay != NSSM_KILL_CONSOLE_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, service->kill_console_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD);
  if (service->kill_window_delay != NSSM_KILL_WINDOW_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, service->kill_window_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD);
  if (service->kill_threads_delay != NSSM_KILL_THREADS_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, service->kill_threads_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD);
  if (! service->kill_process_tree) set_number(key, NSSM_REG_KILL_PROCESS_TREE, 0);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_PROCESS_TREE);
  if (service->stdin_path[0] || editing) {
    if (service->stdin_path[0]) set_expand_string(key, NSSM_REG_STDIN, service->stdin_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDIN);
    if (service->stdin_sharing != NSSM_STDIN_SHARING) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_SHARING, service->stdin_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_SHARING);
    if (service->stdin_disposition != NSSM_STDIN_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_DISPOSITION, service->stdin_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_DISPOSITION);
    if (service->stdin_flags != NSSM_STDIN_FLAGS) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_FLAGS, service->stdin_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_FLAGS);
  }
  if (service->stdout_path[0] || editing) {
    if (service->stdout_path[0]) set_expand_string(key, NSSM_REG_STDOUT, service->stdout_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDOUT);
    if (service->stdout_sharing != NSSM_STDOUT_SHARING) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_SHARING, service->stdout_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_SHARING);
    if (service->stdout_disposition != NSSM_STDOUT_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_DISPOSITION, service->stdout_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_DISPOSITION);
    if (service->stdout_flags != NSSM_STDOUT_FLAGS) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_FLAGS, service->stdout_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_FLAGS);
    if (service->stdout_copy_and_truncate) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_COPY_AND_TRUNCATE, 1);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_COPY_AND_TRUNCATE);
  }
  if (service->stderr_path[0] || editing) {
    if (service->stderr_path[0]) set_expand_string(key, NSSM_REG_STDERR, service->stderr_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDERR);
    if (service->stderr_sharing != NSSM_STDERR_SHARING) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_SHARING, service->stderr_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_SHARING);
    if (service->stderr_disposition != NSSM_STDERR_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_DISPOSITION, service->stderr_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_DISPOSITION);
    if (service->stderr_flags != NSSM_STDERR_FLAGS) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_FLAGS, service->stderr_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_FLAGS);
    if (service->stderr_copy_and_truncate) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_COPY_AND_TRUNCATE, 1);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_COPY_AND_TRUNCATE);
  }
  if (service->timestamp_log) set_number(key, NSSM_REG_TIMESTAMP_LOG, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_TIMESTAMP_LOG);
  if (service->hook_share_output_handles) set_number(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES);
  if (service->rotate_files) set_number(key, NSSM_REG_ROTATE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE);
  if (service->rotate_stdout_online) set_number(key, NSSM_REG_ROTATE_ONLINE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_ONLINE);
  if (service->rotate_seconds) set_number(key, NSSM_REG_ROTATE_SECONDS, service->rotate_seconds);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_SECONDS);
  if (service->rotate_bytes_low) set_number(key, NSSM_REG_ROTATE_BYTES_LOW, service->rotate_bytes_low);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_BYTES_LOW);
  if (service->rotate_bytes_high) set_number(key, NSSM_REG_ROTATE_BYTES_HIGH, service->rotate_bytes_high);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_BYTES_HIGH);
  if (service->rotate_delay != NSSM_ROTATE_DELAY) set_number(key, NSSM_REG_ROTATE_DELAY, service->rotate_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_DELAY);
  if (service->no_console) set_number(key, NSSM_REG_NO_CONSOLE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_NO_CONSOLE);

  /* Environment */
  if (service->env) {
    if (RegSetValueEx(key, NSSM_REG_ENV, 0, REG_MULTI_SZ, (const unsigned char *) service->env, (unsigned long) service->envlen * sizeof(TCHAR)) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_ENV, error_string(GetLastError()));
    }
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_ENV);
  if (service->env_extra) {
    if (RegSetValueEx(key, NSSM_REG_ENV_EXTRA, 0, REG_MULTI_SZ, (const unsigned char *) service->env_extra, (unsigned long) service->env_extralen * sizeof(TCHAR)) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_ENV_EXTRA, error_string(GetLastError()));
    }
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_ENV_EXTRA);

  /* Close registry. */
  RegCloseKey(key);

  return 0;
}

int create_exit_action(TCHAR *service_name, const TCHAR *action_string, bool editing) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (service_registry_path(service_name, true, NSSM_REG_EXIT, registry, _countof(registry)) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("NSSM_REG_EXIT"), _T("create_exit_action()"));
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  unsigned long disposition;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, &disposition) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()));
    return 2;
  }

  /* Do nothing if the key already existed */
  if (disposition == REG_OPENED_EXISTING_KEY && ! editing) {
    RegCloseKey(key);
    return 0;
  }

  /* Create the default value */
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, (unsigned long) (_tcslen(action_string) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXIT, error_string(GetLastError()));
    RegCloseKey(key);
    return 3;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

/* Find the first complete MULTI_SZ terminator without reading past count.
   A null pointer with zero length represents an absent block. */
bool double_null_length(const TCHAR *block, unsigned long count, unsigned long *length) {
  *length = 0;
  if (! block) return ! count;
  if (count > ((size_t) -1) / sizeof(TCHAR)) return false;
  if (count < 2 || (! block[0] && block[1])) return false;
  for (unsigned long i = 0; i < count - 1; i++) {
    if (! block[i] && ! block[i + 1]) {
      *length = i + 2;
      return true;
    }
  }
  return false;
}

/* A registry string must terminate within its returned character count. */
static bool terminated_string(const TCHAR *buffer, unsigned long count) {
  unsigned long i = 0;
  while (i < count && buffer[i]) i++;
  if (i == count) return false;
  /* Only zero padding may follow the first terminator. */
  for (; i < count; i++) if (buffer[i]) return false;
  return true;
}

/* Both the returned character count and its byte allocation must fit. */
static bool add_block_entry(size_t *length, size_t entry_length) {
  size_t limit = ((size_t) -1) / sizeof(TCHAR);
  if (limit > MAXDWORD) limit = MAXDWORD;
  if (entry_length >= limit || *length > limit - entry_length - 1) return false;
  *length += entry_length + 1;
  return true;
}

int get_environment(TCHAR *service_name, HKEY key, TCHAR *value, TCHAR **env, unsigned long *envlen) {
  if (! env || ! envlen) return 1;
  if (*env) HeapFree(GetProcessHeap(), 0, *env);
  *env = 0;
  *envlen = 0;

  /* A concurrent registry writer can grow the value between queries. */
  for (unsigned int attempt = 0; attempt < 3; attempt++) {
    unsigned long type = 0, bytes = 0;
    unsigned long ret = RegQueryValueEx(key, value, 0, &type, NULL, &bytes);
    if (ret == ERROR_FILE_NOT_FOUND) return 0;
    if (ret != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret));
      return 1;
    }
    if (type != REG_MULTI_SZ) {
      log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, value, service_name);
      return 2;
    }
    if (bytes < 2 * sizeof(TCHAR) || bytes % sizeof(TCHAR)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ERROR_INVALID_DATA));
      return 3;
    }

    TCHAR *buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, bytes);
    if (! buffer) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_environment()"));
      return 4;
    }
    unsigned long capacity = bytes;
    ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &bytes);
    if (ret == ERROR_MORE_DATA) {
      HeapFree(GetProcessHeap(), 0, buffer);
      continue;
    }
    if (ret != ERROR_SUCCESS) {
      HeapFree(GetProcessHeap(), 0, buffer);
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret));
      return 5;
    }

    unsigned long length = 0;
    bool valid = type == REG_MULTI_SZ && bytes <= capacity && ! (bytes % sizeof(TCHAR));
    if (valid) valid = double_null_length(buffer, bytes / sizeof(TCHAR), &length);
    /* Permit zero padding, but not hidden entries after the end marker. */
    if (valid) {
      for (unsigned long i = length; i < bytes / sizeof(TCHAR); i++) {
        if (buffer[i]) { valid = false; break; }
      }
    }
    if (! valid) {
      HeapFree(GetProcessHeap(), 0, buffer);
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ERROR_INVALID_DATA));
      return 3;
    }
    *env = buffer;
    *envlen = length;
    return 0;
  }
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ERROR_MORE_DATA));
  return 5;
}

/* datalen is a byte capacity, including space for the terminating TCHAR. */
int get_string(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool expand, bool sanitise, bool must_exist, unsigned long *stored_type) {
  if (! data) return 2;
  ZeroMemory(data, datalen);
  if (datalen < sizeof(TCHAR) || datalen % sizeof(TCHAR)) return 2;
  const unsigned long capacity = datalen / sizeof(TCHAR);
  const size_t buffer_bytes = (size_t) capacity * sizeof(TCHAR);
  TCHAR *buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buffer_bytes);
  if (! buffer) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_string()"));
    return 1;
  }

  unsigned long type = 0, bytes = datalen;
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &bytes);
  if (ret == ERROR_SUCCESS) {
    bool valid = (type == REG_SZ || type == REG_EXPAND_SZ) &&
      bytes >= sizeof(TCHAR) && bytes <= datalen && ! (bytes % sizeof(TCHAR));
    if (valid) valid = terminated_string(buffer, bytes / sizeof(TCHAR));
    if (! valid) ret = ERROR_INVALID_DATA;
  }
  if (ret != ERROR_SUCCESS) {
    HeapFree(GetProcessHeap(), 0, buffer);
    if (ret == ERROR_FILE_NOT_FOUND && ! must_exist) return 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret));
    return 2;
  }

  if (stored_type) *stored_type = type;
  if (sanitise) PathUnquoteSpaces(buffer);
  if (! expand || type == REG_SZ) {
    memmove(data, buffer, (_tcslen(buffer) + 1) * sizeof(TCHAR));
    HeapFree(GetProcessHeap(), 0, buffer);
    return 0;
  }

  unsigned long needed = ExpandEnvironmentStrings(buffer, NULL, 0);
  if (! needed || needed > capacity) {
    unsigned long error = needed ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, buffer, error_string(error));
    HeapFree(GetProcessHeap(), 0, buffer);
    return 3;
  }
  /* Publish output only after success, including when the environment changes. */
  TCHAR *expanded = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buffer_bytes);
  if (! expanded) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_string()"));
    HeapFree(GetProcessHeap(), 0, buffer);
    return 1;
  }
  ret = ExpandEnvironmentStrings(buffer, expanded, capacity);
  if (! ret || ret > capacity) {
    unsigned long error = ret ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, buffer, error_string(error));
    HeapFree(GetProcessHeap(), 0, expanded);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 3;
  }
  memmove(data, expanded, ret * sizeof(TCHAR));
  HeapFree(GetProcessHeap(), 0, expanded);
  HeapFree(GetProcessHeap(), 0, buffer);
  return 0;
}

int get_string(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise) {
  return get_string(key, value, data, datalen, false, sanitise, true);
}

int expand_parameter(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise, bool must_exist) {
  return get_string(key, value, data, datalen, true, sanitise, must_exist);
}

int expand_parameter(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise) {
  return expand_parameter(key, value, data, datalen, sanitise, true);
}

/*
  Sets a string in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_string(HKEY key, TCHAR *value, TCHAR *string, bool expand) {
  unsigned long type = expand ? REG_EXPAND_SZ : REG_SZ;
  if (RegSetValueEx(key, value, 0, type, (const unsigned char *) string, (unsigned long) (_tcslen(string) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS) return 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()));
  return 1;
}

int set_string(HKEY key, TCHAR *value, TCHAR *string) {
  return set_string(key, value, string, false);
}

int set_expand_string(HKEY key, TCHAR *value, TCHAR *string) {
  return set_string(key, value, string, true);
}

/*
  Set an unsigned long in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_number(HKEY key, TCHAR *value, unsigned long number) {
  if (RegSetValueEx(key, value, 0, REG_DWORD, (const unsigned char *) &number, sizeof(number)) == ERROR_SUCCESS) return 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()));
  return 1;
}

/*
  Query an unsigned long from the registry.
  Returns:  1 if a number was retrieved.
            0 if none was found and must_exist is false.
           -1 if none was found and must_exist is true.
           -2 otherwise.
*/
int get_number(HKEY key, TCHAR *value, unsigned long *number, bool must_exist) {
  unsigned long type = REG_DWORD;
  unsigned long number_len = sizeof(unsigned long);

  int ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) number, &number_len);
  if (ret == ERROR_SUCCESS) return 1;

  if (ret == ERROR_FILE_NOT_FOUND) {
    if (! must_exist) return 0;
  }

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret));
  if (ret == ERROR_FILE_NOT_FOUND) return -1;

  return -2;
}

int get_number(HKEY key, TCHAR *value, unsigned long *number) {
  return get_number(key, value, number, true);
}

/* Replace entry separators with CRLF; newlen includes two trailing NULs. */
int format_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **formatted, unsigned long *newlen) {
  *formatted = 0;
  *newlen = 0;
  unsigned long length;
  if (! double_null_length(dn, dnlen, &length)) return 1;
  if (! length) return 0;

  size_t len = 1;
  for (unsigned long i = 0; i < length - 1; i++) {
    /* Add one character, or two for an entry separator. */
    size_t extra = (! dn[i] && dn[i + 1]) ? 1 : 0;
    if (! add_block_entry(&len, extra)) return 1;
  }
  TCHAR *buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! buffer) return 1;
  size_t j = 0;
  for (unsigned long i = 0; i < length - 1; i++) {
    if (j >= len - 1) goto format_failed;
    if (! dn[i] && dn[i + 1]) {
      buffer[j++] = _T('\r');
      if (j >= len - 1) goto format_failed;
      buffer[j++] = _T('\n');
    }
    else buffer[j++] = dn[i];
  }
  *formatted = buffer;
  *newlen = (unsigned long) len;
  return 0;

format_failed:
  HeapFree(GetProcessHeap(), 0, buffer);
  return 1;
}

/* Strip CR and replace LF with NULL.  */
int unformat_double_null(TCHAR *formatted, unsigned long formattedlen, TCHAR **dn, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = 0;

  /* Don't count trailing NULLs. */
  for (i = 0; i < formattedlen; i++) {
    if (! formatted[i]) {
      formattedlen = i;
      break;
    }
  }

  if (! formattedlen) {
    *dn = 0;
    return 0;
  }

  for (i = 0; i < formattedlen; i++) if (formatted[i] != _T('\r')) ++*newlen;

  /* Skip blank lines. */
  for (i = 0; i < formattedlen; i++) {
    if (formatted[i] == _T('\r') && formatted[i + 1] == _T('\n')) {
      /* This is the last CRLF. */
      if (i >= formattedlen - 2) break;

      /*
        Strip at the start of the block or if the next characters are
        CRLF too.
      */
      if (! i || (formatted[i + 2] == _T('\r') && formatted[i + 3] == _T('\n'))) {
        for (j = i + 2; j < formattedlen; j++) formatted[j - 2] = formatted[j];
        formatted[formattedlen--] = _T('\0');
        formatted[formattedlen--] = _T('\0');
        i--;
        --*newlen;
      }
    }
  }

  /* Must end with two NULLs. */
  *newlen += 2;

  *dn = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *dn) return 1;

  for (i = 0, j = 0; i < formattedlen; i++) {
    if (formatted[i] == _T('\r')) continue;
    if (formatted[i] == _T('\n')) (*dn)[j] = _T('\0');
    else (*dn)[j] = formatted[i];
    j++;
  }

  return 0;
}

/* Copy a block. */
int copy_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn) {
  if (! newdn) return 1;

  *newdn = 0;
  if (! dn) return 0;

  *newdn = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, dnlen * sizeof(TCHAR));
  if (! *newdn) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("dn"), _T("copy_double_null()"));
    return 2;
  }

  memmove(*newdn, dn, dnlen * sizeof(TCHAR));
  return 0;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  The new string may be specified as <key> <delimiter> <value> and the keylen
  gives the offset into the string to compare against existing entries.
  If keylen is zero the entire entry is compared.  Matching entries are
  replaced once at the first match; unrelated entry order is preserved.
  A blank append copies the original block.
*/
int append_to_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn, unsigned long *newlen, TCHAR *append, size_t keylen, bool case_sensitive) {
  *newdn = 0;
  *newlen = 0;
  unsigned long length;
  if (! double_null_length(dn, dnlen, &length)) return 1;
  if (! append || ! append[0]) {
    int ret = copy_double_null(dn, length, newdn);
    if (! ret) *newlen = length;
    return ret;
  }
  size_t appendlen = _tcslen(append);
  bool whole_string = ! keylen || keylen > appendlen;
  if (whole_string) keylen = appendlen;
  int (*fn)(const TCHAR *, const TCHAR *, size_t) = case_sensitive ? _tcsncmp : _tcsnicmp;

  /* Reserve the replacement once, regardless of how many keys match. */
  size_t len = 1;
  if (! add_block_entry(&len, appendlen)) return 1;
  for (unsigned long i = 0; i < length && dn[i]; ) {
    size_t entrylen = _tcslen(dn + i); /* The block was bounded above. */
    bool matches = entrylen >= keylen && ! fn(dn + i, append, keylen) &&
      (! whole_string || entrylen == appendlen);
    if (! matches && ! add_block_entry(&len, entrylen)) return 1;
    i += (unsigned long) entrylen + 1;
  }
  TCHAR *buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! buffer) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("newdn"), _T("append_to_double_null()"));
    return 2;
  }

  TCHAR *target = buffer;
  bool replaced = false;
  for (unsigned long i = 0; i < length && dn[i]; ) {
    size_t entrylen = _tcslen(dn + i);
    bool matches = entrylen >= keylen && ! fn(dn + i, append, keylen) &&
      (! whole_string || entrylen == appendlen);
    if (! matches) {
      memmove(target, dn + i, (entrylen + 1) * sizeof(TCHAR));
      target += entrylen + 1;
    }
    else if (! replaced) {
      memmove(target, append, (appendlen + 1) * sizeof(TCHAR));
      target += appendlen + 1;
      replaced = true;
    }
    i += (unsigned long) entrylen + 1;
  }
  if (! replaced) memmove(target, append, (appendlen + 1) * sizeof(TCHAR));
  *newdn = buffer;
  *newlen = (unsigned long) len;
  return 0;
}

/*
  Create a new block with all the string of the first block minus the given
  string.
  The keylen parameter gives the offset into the string to compare against
  existing entries.  If a substring of existing value matches the string to
  the given length it will be removed.
  If the last entry is removed the new block will still be allocated and
  have non-zero length.
*/
int remove_from_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn, unsigned long *newlen, TCHAR *remove, size_t keylen, bool case_sensitive) {
  if (! remove || !remove[0]) return copy_double_null(dn, dnlen, newdn);
  size_t removelen = _tcslen(remove);
  int (*fn)(const TCHAR *, const TCHAR *, size_t) = (case_sensitive) ? _tcsncmp : _tcsnicmp;

  /* Identify the key, if any, or treat the whole string as the key. */
  TCHAR *key = 0;
  if (! keylen || keylen > removelen) keylen = removelen;
  key = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(TCHAR));
  if (! key) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("key"), _T("remove_from_double_null()"));
    return 1;
  }
  memmove(key, remove, keylen * sizeof(TCHAR));
  key[keylen] = _T('\0');

  /* Find the length of the block not including any existing key. */
  size_t len = 0;
  TCHAR *s;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) len += _tcslen(s) + 1;
    for ( ; *s; s++);
  }

  /* Account for trailing NULL. */
  if (++len < 2) len = 2;

  /* Allocate a new block. */
  *newdn = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! *newdn) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("newdn"), _T("remove_from_double_null()"));
    HeapFree(GetProcessHeap(), 0, key);
    return 2;
  }

  /* Copy existing entries.*/
  *newlen = (unsigned long) len;
  TCHAR *t = *newdn;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) {
      len = _tcslen(s) + 1;
      memmove(t, s, len * sizeof(TCHAR));
      t += len;
    }
    for ( ; *s; s++);
  }

  HeapFree(GetProcessHeap(), 0, key);
  return 0;
}

void override_milliseconds(TCHAR *service_name, HKEY key, TCHAR *value, unsigned long *buffer, unsigned long default_value, unsigned long event) {
  unsigned long type = REG_DWORD;
  unsigned long buflen = sizeof(unsigned long);
  bool ok = false;
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        TCHAR milliseconds[16];
        _sntprintf_s(milliseconds, _countof(milliseconds), _TRUNCATE, _T("%lu"), default_value);
        log_event(EVENTLOG_WARNING_TYPE, event, service_name, value, milliseconds);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret));
    }
  }
  else ok = true;

  if (! ok) *buffer = default_value;
}

/* Open the key of the service itself Services\<service_name>. */
HKEY open_service_registry(const TCHAR *service_name, REGSAM sam, bool must_exist) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (service_registry_path(service_name, false, 0, registry, _countof(registry)) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REGISTRY, _T("open_service_registry()"));
    return 0;
  }

  return open_registry_key(registry, sam, must_exist);
}

/* Open a subkey of the service Services\<service_name>\<sub>. */
long open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam, HKEY *key, bool must_exist) {
  *key = 0;
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (service_registry_path(service_name, true, sub, registry, _countof(registry)) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REGISTRY, _T("open_registry()"));
    return ERROR_INSUFFICIENT_BUFFER;
  }

  return open_registry_key(registry, sam, key, must_exist);
}

HKEY open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam, bool must_exist) {
  HKEY key;
  long error = open_registry(service_name, sub, sam, &key, must_exist);
  return key;
}

HKEY open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam) {
  return open_registry(service_name, sub, sam, true);
}

HKEY open_registry(const TCHAR *service_name, REGSAM sam) {
  return open_registry(service_name, 0, sam, true);
}

int get_io_parameters(nssm_service_t *service, HKEY key) {
  /* stdin */
  if (get_createfile_parameters(key, NSSM_REG_STDIN, service->stdin_path, &service->stdin_sharing, NSSM_STDIN_SHARING, &service->stdin_disposition, NSSM_STDIN_DISPOSITION, &service->stdin_flags, NSSM_STDIN_FLAGS, 0)) {
    service->stdin_sharing = service->stdin_disposition = service->stdin_flags = 0;
    ZeroMemory(service->stdin_path, _countof(service->stdin_path) * sizeof(TCHAR));
    return 1;
  }

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS, &service->stdout_copy_and_truncate)) {
    service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
    ZeroMemory(service->stdout_path, _countof(service->stdout_path) * sizeof(TCHAR));
    return 2;
  }

  /* stderr */
  if (get_createfile_parameters(key, NSSM_REG_STDERR, service->stderr_path, &service->stderr_sharing, NSSM_STDERR_SHARING, &service->stderr_disposition, NSSM_STDERR_DISPOSITION, &service->stderr_flags, NSSM_STDERR_FLAGS, &service->stderr_copy_and_truncate)) {
    service->stderr_sharing = service->stderr_disposition = service->stderr_flags = 0;
    ZeroMemory(service->stderr_path, _countof(service->stderr_path) * sizeof(TCHAR));
    return 3;
  }

  return 0;
}

int get_parameters(nssm_service_t *service, STARTUPINFO *si) {
  unsigned long ret;

  /* Try to open the registry */
  HKEY key = open_registry(service->name, KEY_READ);
  if (! key) return 1;

  /* Don't expand parameters when retrieving for the GUI. */
  bool expand = si ? true : false;

  /* Try to get environment variables - may fail */
  get_environment(service->name, key, NSSM_REG_ENV, &service->env, &service->envlen);
  /* Environment variables to add to existing rather than replace - may fail. */
  get_environment(service->name, key, NSSM_REG_ENV_EXTRA, &service->env_extra, &service->env_extralen);

  /* Set environment if we are starting the service. */
  if (si) set_service_environment(service);

  /* Try to get executable file - MUST succeed */
  if (get_string(key, NSSM_REG_EXE, service->exe, sizeof(service->exe), expand, false, true)) {
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail and we don't care */
  if (get_string(key, NSSM_REG_FLAGS, service->flags, sizeof(service->flags), expand, false, true)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_FLAGS, NSSM_REG_FLAGS, service->name, service->exe);
    ZeroMemory(service->flags, sizeof(service->flags));
  }

  /* Try to get startup directory - may fail and we fall back to a default */
  if (get_string(key, NSSM_REG_DIR, service->dir, sizeof(service->dir), expand, true, true) || ! service->dir[0]) {
    _sntprintf_s(service->dir, _countof(service->dir), _TRUNCATE, _T("%s"), service->exe);
    strip_basename(service->dir);
    if (service->dir[0] == _T('\0')) {
      /* Help! */
      ret = GetWindowsDirectory(service->dir, sizeof(service->dir));
      if (! ret || ret > sizeof(service->dir)) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_NO_DIR_AND_NO_FALLBACK, NSSM_REG_DIR, service->name);
        RegCloseKey(key);
        return 4;
      }
    }
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_DIR, NSSM_REG_DIR, service->name, service->dir);
  }

  /* Try to get processor affinity - may fail. */
  TCHAR buffer[512];
  if (get_string(key, NSSM_REG_AFFINITY, buffer, sizeof(buffer), false, false, false) || ! buffer[0]) service->affinity = 0LL;
  else if (affinity_string_to_mask(buffer, &service->affinity)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_AFFINITY_MASK, service->name, buffer);
    service->affinity = 0LL;
  }
  else {
    DWORD_PTR affinity, system_affinity;

    if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) {
      _int64 effective_affinity = service->affinity & system_affinity;
      if (effective_affinity != service->affinity) {
        TCHAR *system = 0;
        if (! affinity_mask_to_string(system_affinity, &system)) {
          TCHAR *effective = 0;
          if (! affinity_mask_to_string(effective_affinity, &effective)) {
            log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_EFFECTIVE_AFFINITY_MASK, service->name, buffer, system, effective);
          }
          HeapFree(GetProcessHeap(), 0, effective);
        }
        HeapFree(GetProcessHeap(), 0, system);
      }
    }
  }

  /* Try to get priority - may fail. */
  unsigned long priority;
  if (get_number(key, NSSM_REG_PRIORITY, &priority, false) == 1) {
    if (priority == (priority & priority_mask())) service->priority = priority;
    else log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_PRIORITY, service->name, NSSM_REG_PRIORITY);
  }

  /* Try to get hook I/O sharing - may fail. */
  unsigned long hook_share_output_handles;
  if (get_number(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES, &hook_share_output_handles, false) == 1) {
    if (hook_share_output_handles) service->hook_share_output_handles = true;
    else service->hook_share_output_handles = false;
  }
  else hook_share_output_handles = false;
  /* Try to get file rotation settings - may fail. */
  unsigned long rotate_files;
  if (get_number(key, NSSM_REG_ROTATE, &rotate_files, false) == 1) {
    if (rotate_files) service->rotate_files = true;
    else service->rotate_files = false;
  }
  else service->rotate_files = false;
  if (get_number(key, NSSM_REG_ROTATE_ONLINE, &rotate_files, false) == 1) {
    if (rotate_files) service->rotate_stdout_online = service->rotate_stderr_online = true;
    else service->rotate_stdout_online = service->rotate_stderr_online = false;
  }
  else service->rotate_stdout_online = service->rotate_stderr_online = false;
  /* Log timestamping requires a logging thread.*/
  unsigned long timestamp_log;
  if (get_number(key, NSSM_REG_TIMESTAMP_LOG, &timestamp_log, false) == 1) {
    if (timestamp_log) service->timestamp_log = true;
    else service->timestamp_log = false;
  }
  else service->timestamp_log = false;

  /* Hook I/O sharing and online rotation need a pipe. */
  service->use_stdout_pipe = service->rotate_stdout_online || service->timestamp_log || hook_share_output_handles;
  service->use_stderr_pipe = service->rotate_stderr_online || service->timestamp_log || hook_share_output_handles;
  if (get_number(key, NSSM_REG_ROTATE_SECONDS, &service->rotate_seconds, false) != 1) service->rotate_seconds = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_LOW, &service->rotate_bytes_low, false) != 1) service->rotate_bytes_low = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_HIGH, &service->rotate_bytes_high, false) != 1) service->rotate_bytes_high = 0;
  override_milliseconds(service->name, key, NSSM_REG_ROTATE_DELAY, &service->rotate_delay, NSSM_ROTATE_DELAY, NSSM_EVENT_BOGUS_THROTTLE);

  /* Try to get force new console setting - may fail. */
  if (get_number(key, NSSM_REG_NO_CONSOLE, &service->no_console, false) != 1) service->no_console = 0;

  /* Change to startup directory in case stdout/stderr are relative paths. */
  TCHAR cwd[PATH_LENGTH];
  GetCurrentDirectory(_countof(cwd), cwd);
  SetCurrentDirectory(service->dir);

  /* Try to get stdout and stderr */
  if (get_io_parameters(service, key)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name);
    RegCloseKey(key);
    SetCurrentDirectory(cwd);
    return 5;
  }

  /* Change back in case the startup directory needs to be deleted. */
  SetCurrentDirectory(cwd);

  /* Try to get mandatory restart delay */
  override_milliseconds(service->name, key, NSSM_REG_RESTART_DELAY, &service->restart_delay, 0, NSSM_EVENT_BOGUS_RESTART_DELAY);

  /* Try to get throttle restart delay */
  override_milliseconds(service->name, key, NSSM_REG_THROTTLE, &service->throttle_delay, NSSM_RESET_THROTTLE_RESTART, NSSM_EVENT_BOGUS_THROTTLE);

  /* Try to get service stop flags. */
  unsigned long type = REG_DWORD;
  unsigned long stop_method_skip;
  unsigned long buflen = sizeof(stop_method_skip);
  bool stop_ok = false;
  ret = RegQueryValueEx(key, NSSM_REG_STOP_METHOD_SKIP, 0, &type, (unsigned char *) &stop_method_skip, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_STOP_METHOD_SKIP, service->name, NSSM_REG_STOP_METHOD_SKIP, NSSM);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_STOP_METHOD_SKIP, error_string(ret));
    }
  }
  else stop_ok = true;

  /* Try all methods except those requested to be skipped. */
  service->stop_method = ~0;
  if (stop_ok) service->stop_method &= ~stop_method_skip;

  /* Try to get kill delays - may fail. */
  override_milliseconds(service->name, key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, &service->kill_console_delay, NSSM_KILL_CONSOLE_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_CONSOLE_GRACE_PERIOD);
  override_milliseconds(service->name, key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, &service->kill_window_delay, NSSM_KILL_WINDOW_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_WINDOW_GRACE_PERIOD);
  override_milliseconds(service->name, key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, &service->kill_threads_delay, NSSM_KILL_THREADS_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_THREADS_GRACE_PERIOD);

  /* Try to get process tree settings - may fail. */
  unsigned long kill_process_tree;
  if (get_number(key, NSSM_REG_KILL_PROCESS_TREE, &kill_process_tree, false) == 1) {
    if (kill_process_tree) service->kill_process_tree = true;
    else service->kill_process_tree = false;
  }
  else service->kill_process_tree = true;

  /* Try to get default exit action. */
  bool default_action;
  service->default_exit_action = NSSM_EXIT_RESTART;
  TCHAR action_string[ACTION_LEN];
  if (! get_exit_action(service->name, 0, action_string, &default_action)) {
    for (int i = 0; exit_action_strings[i]; i++) {
      if (! _tcsnicmp((const TCHAR *) action_string, exit_action_strings[i], ACTION_LEN)) {
        service->default_exit_action = i;
        break;
      }
    }
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

/*
  Sets the string for the exit action corresponding to the exit code.

  ret is a pointer to an unsigned long containing the exit code.
  If ret is NULL, we retrieve the default exit action unconditionally.

  action is a buffer which receives the string.

  default_action is a pointer to a bool which is set to false if there
  was an explicit string for the given exit code, or true if we are
  returning the default action.

  Returns: 0 on success.
           1 on error.
*/
long query_exit_action(HKEY key, const TCHAR *name, TCHAR *action) {
  _tcscpy_s(action, ACTION_LEN, exit_action_strings[NSSM_EXIT_RESTART]);
  TCHAR buffer[ACTION_LEN] = {};
  unsigned long type = 0, bytes = sizeof(buffer);
  long error = RegQueryValueEx(key, name, 0, &type, (unsigned char *) buffer, &bytes);
  if (error != ERROR_SUCCESS) return error;
  if (type != REG_SZ || ! bytes || bytes > sizeof(buffer) || bytes % sizeof(TCHAR) ||
      ! terminated_string(buffer, bytes / sizeof(TCHAR))) return ERROR_INVALID_DATA;
  for (unsigned i = 0; exit_action_strings[i]; i++) {
    if (! str_equiv(buffer, exit_action_strings[i])) continue;
    _tcscpy_s(action, ACTION_LEN, exit_action_strings[i]);
    return ERROR_SUCCESS;
  }
  return ERROR_INVALID_DATA;
}

int get_exit_action(const TCHAR *service_name, unsigned long *ret, TCHAR *action, bool *default_action) {
  _tcscpy_s(action, ACTION_LEN, exit_action_strings[NSSM_EXIT_RESTART]);
  *default_action = true;
  HKEY key = 0;
  long error = open_registry(service_name, NSSM_REG_EXIT, KEY_QUERY_VALUE, &key, false);
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 0;
  if (error != ERROR_SUCCESS) return 1;

  TCHAR code[16] = {};
  if (ret) _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), *ret);
  error = query_exit_action(key, code, action);
  if (error == ERROR_FILE_NOT_FOUND && ret) {
    code[0] = 0;
    error = query_exit_action(key, code, action);
  }
  RegCloseKey(key);
  if (error == ERROR_FILE_NOT_FOUND) return 0; // Absent default means Restart.
  if (error != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, code[0] ? code : NSSM_REG_EXIT, error_string(error));
    return 1;
  }
  *default_action = ! code[0];
  return 0;
}

int set_hook(const TCHAR *service_name, const TCHAR *hook_event, const TCHAR *hook_action, TCHAR *cmd, bool expand) {
  /* Try to open the registry */
  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("%s\\%s"), NSSM_REG_HOOK, hook_event) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook registry"), _T("set_hook()"));
    return 1;
  }

  HKEY key;
  long error;

  /* Don't create keys needlessly. */
  if (! _tcslen(cmd)) {
    key = open_registry(service_name, registry, KEY_READ, false);
    if (! key) return 0;
    error = RegQueryValueEx(key, hook_action, 0, 0, 0, 0);
    RegCloseKey(key);
    if (error == ERROR_FILE_NOT_FOUND) return 0;
  }

  key = open_registry(service_name, registry, KEY_WRITE);
  if (! key) return 1;

  int ret = 1;
  if (_tcslen(cmd)) ret = set_string(key, (TCHAR *) hook_action, cmd, expand);
  else {
    error = RegDeleteValue(key, hook_action);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) ret = 0;
  }

  /* Close registry */
  RegCloseKey(key);

  return ret;
}

int get_hook(const TCHAR *service_name, const TCHAR *hook_event, const TCHAR *hook_action, TCHAR *buffer, unsigned long buflen, bool expand, unsigned long *stored_type) {
  /* Try to open the registry */
  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("%s\\%s"), NSSM_REG_HOOK, hook_event) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook registry"), _T("get_hook()"));
    return 1;
  }
  HKEY key;
  long error = open_registry(service_name, registry, KEY_READ, &key, false);
  if (! key) {
    if (error == ERROR_FILE_NOT_FOUND) {
      ZeroMemory(buffer, buflen);
      return 0;
    }
    return 1;
  }

  /* A hook is a complete command line, not a path to unquote. */
  int ret = get_string(key, (TCHAR *) hook_action, buffer, buflen, expand, false, false, stored_type);

  /* Close registry */
  RegCloseKey(key);

  return ret;
}
