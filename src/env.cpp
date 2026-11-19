#include "nssm.h"

/*
  Environment block is of the form:

    KEY1=VALUE1 NULL
    KEY2=VALUE2 NULL
    NULL

  A single variable KEY=VALUE has length 15:

    KEY=VALUE (13) NULL (1)
    NULL (1)

  Environment variable names are case-insensitive!
*/

/* Find the length in characters of an environment block. */
size_t environment_length(TCHAR *env) {
  size_t len = 0;

  TCHAR *s;
  for (s = env; ; s++) {
    len++;
    if (*s == _T('\0')) {
      if (*(s + 1) == _T('\0')) {
        len++;
        break;
      }
    }
  }

  return len;
}

/* Copy an environment block. */
TCHAR *copy_environment_block(TCHAR *env) {
  if (! env) return NULL;
  TCHAR *newenv;
  if (copy_double_null(env, (unsigned long) environment_length(env), &newenv)) return 0;
  return newenv;
}

/*
  The environment block starts with variables of the form
  =C:=C:\Windows\System32 which we ignore.
*/
TCHAR *useful_environment(TCHAR *rawenv) {
  TCHAR *env = rawenv;

  if (env) {
    while (*env == _T('=')) {
      for ( ; *env; env++);
      env++;
    }
  }

  return env;
}

/* Expand an environment variable.  Must call HeapFree() on the result. */
TCHAR *expand_environment_string(TCHAR *string) {
  unsigned long len;

  len = ExpandEnvironmentStrings(string, 0, 0);
  if (! len) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()));
    return 0;
  }

  TCHAR *ret = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
  if (! ret) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("ExpandEnvironmentStrings()"), _T("expand_environment_string"));
    return 0;
  }

  unsigned long written = ExpandEnvironmentStrings(string, ret, len);
  if (! written || written > len) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()));
    HeapFree(GetProcessHeap(), 0, ret);
    return 0;
  }

  return ret;
}

/*
  Set all the environment variables from an environment block in the current
  environment or remove all the variables in the block from the current
  environment.
*/
static int set_environment_block(TCHAR *env, bool set, bool expand = true) {
  if (! env) return 1;
  int ret = 0;

  TCHAR *s, *t, *next;
  for (s = env; *s; s = next) {
    /* Find the next entry before splitting this one, even if its value is empty. */
    next = s + _tcslen(s) + 1;
    for (t = s; *t && *t != _T('='); t++);
    if (*t == _T('=')) {
      *t++ = _T('\0');
      if (set) {
        TCHAR *expanded = expand ? expand_environment_string(t) : NULL;
        if ((expand && !expanded) || !SetEnvironmentVariable(s, expand ? expanded : t)) ret++;
        if (expanded) HeapFree(GetProcessHeap(), 0, expanded);
      }
      else {
        if (! SetEnvironmentVariable(s, NULL)) ret++;
      }
      t[-1] = _T('=');
    }
  }

  return ret;
}

int set_environment_block(TCHAR *env) {
  return set_environment_block(env, true);
}

static int unset_environment_block(TCHAR *env) {
  return set_environment_block(env, false);
}

/* Remove all variables from the process environment. */
int clear_environment() {
  TCHAR *copy = copy_environment();
  if (! copy) return 1;
  int result = unset_environment_block(useful_environment(copy));
  HeapFree(GetProcessHeap(), 0, copy);
  return result;
}

int duplicate_environment(TCHAR *rawenv) {
  if (! rawenv || clear_environment()) return 1;
  return set_environment_block(useful_environment(rawenv), true, false);
}

/*
  Verify an environment block.
  Returns:  1 if environment is invalid.
            0 if environment is OK.
           -1 on error.
*/
int test_environment(TCHAR *env) {
  TCHAR *path = (TCHAR *) nssm_imagepath();
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  unsigned long flags = CREATE_SUSPENDED;
#ifdef UNICODE
  flags |= CREATE_UNICODE_ENVIRONMENT;
#endif

  /*
    Try to relaunch ourselves but with the candidate environment set.
    Assuming no solar flare activity, the only reason this would fail is if
    the environment were invalid.
  */
  if (CreateProcess(0, path, 0, 0, 0, flags, env, 0, &si, &pi)) {
    TerminateProcess(pi.hProcess, 0);
    /* Termination is asynchronous; never report success without observing exit. */
    DWORD waited = WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (waited != WAIT_OBJECT_0) return -1;
  }
  else {
    unsigned long error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) return 1;
    else return -1;
  }

  return 0;
}

/*
  Duplicate an environment block returned by GetEnvironmentStrings().
  Since such a block is by definition readonly, and duplicate_environment()
  modifies its inputs, this function takes a copy of the input and operates
  on that.
*/
int duplicate_environment_strings(TCHAR *env) {
  TCHAR *newenv = copy_environment_block(env);
  if (!newenv) return 1;
  int result = duplicate_environment(newenv);
  HeapFree(GetProcessHeap(), 0, newenv);
  return result;
}

/* Safely get a copy of the current environment. */
TCHAR *copy_environment() {
  TCHAR *rawenv = GetEnvironmentStrings();
  if (! rawenv) return NULL;
  TCHAR *env = copy_environment_block(rawenv);
  FreeEnvironmentStrings(rawenv);
  return env;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  If the key is already present its value will be overwritten in place.
  If the key is blank or empty the new block will still be allocated and have
  non-zero length.
*/
int append_to_environment_block(TCHAR *env, unsigned long envlen, TCHAR *string, TCHAR **newenv, unsigned long *newlen) {
  size_t keylen = 0;
  if (string && string[0]) {
    for (; string[keylen]; keylen++) {
      if (string[keylen] == _T('=')) {
        keylen++;
        break;
      }
    }
  }
  return append_to_double_null(env, envlen, newenv, newlen, string, keylen, false);
}

/*
  Create a new block with all the strings of the first block minus the given
  string.
  If the key is not present the new block will be a copy of the original.
  If the string is KEY=VALUE the key will only be removed if its value is
  VALUE.
  If the string is just KEY the key will unconditionally be removed.
  If removing the string results in an empty list the new block will still be
  allocated and have non-zero length.
*/
int remove_from_environment_block(TCHAR *env, unsigned long envlen, const TCHAR *string, TCHAR **newenv, unsigned long *newlen) {
  if (! string || ! string[0] || string[0] == _T('=')) return 1;

  unsigned long length;
  if (! double_null_length(env, envlen, &length) || !env) return 1;
  if (copy_double_null(env, length, newenv)) return 2;
  const TCHAR *equals = _tcschr(string, _T('='));
  size_t keylen = equals ? equals - string : _tcslen(string);
  TCHAR *out = *newenv;
  for (const TCHAR *entry = env; *entry; entry += _tcslen(entry) + 1) {
    if (! _tcsnicmp(entry, string, keylen) && entry[keylen] == _T('=') &&
        (! equals || ! _tcscmp(entry + keylen + 1, equals + 1))) continue;
    size_t count = _tcslen(entry) + 1;
    memcpy(out, entry, count * sizeof(*out));
    out += count;
  }
  if (out == *newenv) *out++ = 0;
  *out = 0;
  *newlen = (unsigned long) (out - *newenv + 1);
  return 0;
}
