#include "nssm.h"

#define COMPLAINED_READ (1 << 0)
#define COMPLAINED_WRITE (1 << 1)
#define COMPLAINED_ROTATE (1 << 2)
#define TIMESTAMP_FORMAT "%04u-%02u-%02u %02u:%02u:%02u.%03u: "
#define TIMESTAMP_LEN 25

/* Serialize service-side registration, hook duplication, rotation and cleanup. */
class logger_lock {
  CRITICAL_SECTION *section;
public:
  explicit logger_lock(nssm_service_t *service) : section(service->hook_section_initialised ? &service->hook_section : NULL) {
    if (section) EnterCriticalSection(section);
  }
  ~logger_lock() { if (section) LeaveCriticalSection(section); }
};

static void release_logger(logger_t *logger) {
  if (logger && ! InterlockedDecrement(&logger->references)) {
    close_handle(&logger->stop_event);
    HeapFree(GetProcessHeap(), 0, logger);
  }
}

static bool logger_stopping(logger_t *logger, unsigned long delay = 0) {
  return WaitForSingleObject(logger->stop_event, delay) != WAIT_TIMEOUT;
}

static int dup_handle(HANDLE source_handle, HANDLE *dest_handle_ptr, TCHAR *source_description, TCHAR *dest_description, unsigned long flags) {
  if (! dest_handle_ptr) return 1;

  if (! DuplicateHandle(GetCurrentProcess(), source_handle, GetCurrentProcess(), dest_handle_ptr, 0, true, flags)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, source_description, dest_description, error_string(GetLastError()));
    return 2;
  }
  return 0;
}

static int dup_handle(HANDLE source_handle, HANDLE *dest_handle_ptr, TCHAR *source_description, TCHAR *dest_description) {
  return dup_handle(source_handle, dest_handle_ptr, source_description, dest_description, DUPLICATE_SAME_ACCESS);
}

/*
  read_handle:  read from application
  pipe_handle:  stdout of application
  write_handle: to file
*/
static HANDLE create_logging_thread(TCHAR *service_name, TCHAR *path, unsigned long sharing, unsigned long disposition, unsigned long flags, HANDLE *pipe_handle_ptr, HANDLE *write_handle_ptr, unsigned long rotate_bytes_low, unsigned long rotate_bytes_high, unsigned long rotate_delay, logger_t **owner, unsigned long rotate_online, bool timestamp_log, bool copy_and_truncate) {
  *owner = NULL;
  *pipe_handle_ptr = NULL;
  logger_t *logger = (logger_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(logger_t));
  if (! logger) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("logger"), _T("create_logging_thread()"));
    return (HANDLE) 0;
  }

  logger->references = 1;
  logger->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (! logger->stop_event) {
    release_logger(logger);
    return NULL;
  }
  /* Only the duplicated STARTUPINFO handles are inheritable. */
  if (! CreatePipe(&logger->read_handle, pipe_handle_ptr, NULL, 0)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service_name, path, error_string(GetLastError()));
    release_logger(logger);
    return NULL;
  }

  ULARGE_INTEGER size;
  size.LowPart = rotate_bytes_low;
  size.HighPart = rotate_bytes_high;

  _sntprintf_s(logger->service_name, _countof(logger->service_name), _TRUNCATE, _T("%s"), service_name);
  _sntprintf_s(logger->path, _countof(logger->path), _TRUNCATE, _T("%s"), path);
  logger->sharing = sharing;
  logger->disposition = disposition;
  logger->flags = flags;
  logger->write_handle = *write_handle_ptr;
  logger->size = size.QuadPart;
  logger->timestamp_log = timestamp_log;
  logger->line_length = 0;
  logger->rotate_online = rotate_online;
  logger->rotate_delay = rotate_delay;
  logger->copy_and_truncate = copy_and_truncate;

  /* The worker may finish before CreateThread returns. Retain the service ref. */
  InterlockedIncrement(&logger->references);
  unsigned long tid;
  HANDLE thread_handle = CreateThread(NULL, 0, log_and_rotate, (void *) logger, 0, &tid);
  if (! thread_handle) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()));
    close_handle(&logger->read_handle);
    close_handle(pipe_handle_ptr);
    /* The caller still owns the file and can fall back to direct output. */
    /* Drop the unused worker reference, then the service reference. */
    release_logger(logger);
    release_logger(logger);
  }
  else {
    *owner = logger;
    *write_handle_ptr = NULL;
  }

  return thread_handle;
}

static inline unsigned long guess_charsize(void *address, unsigned long bufsize) {
  if (IsTextUnicode(address, bufsize, 0)) return (unsigned long) sizeof(wchar_t);
  else return (unsigned long) sizeof(char);
}

void close_handle(HANDLE *handle) {
  if (! handle) return;
  if (*handle && *handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
  *handle = 0;
}

/* Get path, share mode, creation disposition and flags for a stream. */
int get_createfile_parameters(HKEY key, TCHAR *prefix, TCHAR *path, unsigned long *sharing, unsigned long default_sharing, unsigned long *disposition, unsigned long default_disposition, unsigned long *flags, unsigned long default_flags, bool *copy_and_truncate) {
  TCHAR value[NSSM_STDIO_LENGTH];

  /* Path. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s"), prefix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, prefix, _T("get_createfile_parameters()"));
    return 1;
  }
  switch (expand_parameter(key, value, path, PATH_LENGTH * sizeof(*path), true, false)) {
    case 0: if (! path[0]) return 0; break; /* OK. */
    default: return 2; /* Error. */
  }

  /* ShareMode. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_SHARING) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_SHARING, _T("get_createfile_parameters()"));
    return 3;
  }
  switch (get_number(key, value, sharing, false)) {
    case 0: *sharing = default_sharing; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 4; /* Error. */
  }

  /* CreationDisposition. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_DISPOSITION) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_DISPOSITION, _T("get_createfile_parameters()"));
    return 5;
  }
  switch (get_number(key, value, disposition, false)) {
    case 0: *disposition = default_disposition; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 6; /* Error. */
  }

  /* Flags. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_FLAGS) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_FLAGS, _T("get_createfile_parameters()"));
    return 7;
  }
  switch (get_number(key, value, flags, false)) {
    case 0: *flags = default_flags; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 8; /* Error. */
  }

  /* Rotate with CopyFile() and SetEndOfFile(). */
  if (copy_and_truncate) {
    unsigned long data;
    if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_COPY_AND_TRUNCATE) < 0) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_COPY_AND_TRUNCATE, _T("get_createfile_parameters()"));
      return 9;
    }
    switch (get_number(key, value, &data, false)) {
      case 0: *copy_and_truncate = false; break; /* Missing. */
      case 1: /* Found. */
        if (data) *copy_and_truncate = true;
        else *copy_and_truncate = false;
        break;
      case -2: return 9; /* Error. */
    }
  }

  return 0;
}

int set_createfile_parameter(HKEY key, TCHAR *prefix, TCHAR *suffix, unsigned long number) {
  TCHAR value[NSSM_STDIO_LENGTH];

  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, suffix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, _T("set_createfile_parameter()"));
    return 1;
  }

  return set_number(key, value, number);
}

int delete_createfile_parameter(HKEY key, TCHAR *prefix, TCHAR *suffix) {
  TCHAR value[NSSM_STDIO_LENGTH];

  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, suffix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, _T("delete_createfile_parameter()"));
    return 1;
  }

  if (RegDeleteValue(key, value)) return 0;
  return 1;
}

HANDLE write_to_file(TCHAR *path, unsigned long sharing, SECURITY_ATTRIBUTES *attributes, unsigned long disposition, unsigned long flags) {
  static LARGE_INTEGER offset = { 0 };
  HANDLE ret = CreateFile(path, FILE_WRITE_DATA, sharing, attributes, disposition, flags, 0);
  if (ret != INVALID_HANDLE_VALUE) {
    if (SetFilePointerEx(ret, offset, 0, FILE_END)) SetEndOfFile(ret);
    return ret;
  }

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(GetLastError()));
  return ret;
}

static void rotated_filename(TCHAR *path, TCHAR *rotated, unsigned long rotated_len, SYSTEMTIME *st) {
  SYSTEMTIME now;
  if (! st) {
    st = &now;
    GetSystemTime(st);
  }

  TCHAR buffer[PATH_LENGTH];
  _tcsncpy_s(buffer, path, _TRUNCATE);
  buffer[_countof(buffer) - 1] = 0;
  TCHAR *ext = PathFindExtension(buffer);
  TCHAR extension[PATH_LENGTH];
  _sntprintf_s(extension, _countof(extension), _TRUNCATE, _T("-%04u%02u%02uT%02u%02u%02u.%03u%s"), st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond, st->wMilliseconds, ext);
  *ext = _T('\0');
  _sntprintf_s(rotated, rotated_len, _TRUNCATE, _T("%s%s"), buffer, extension);
}

static rotation_options_t offline_rotation_options(const nssm_service_t *service, bool copy_and_truncate) {
  rotation_options_t options;
  options.seconds = service->rotate_seconds;
  options.delay_ms = service->rotate_delay;
  ULARGE_INTEGER bytes;
  bytes.LowPart = service->rotate_bytes_low;
  bytes.HighPart = service->rotate_bytes_high;
  options.bytes = bytes.QuadPart;
  options.copy_and_truncate = copy_and_truncate;
  return options;
}

void rotate_file(TCHAR *service_name, TCHAR *path, const rotation_options_t &options) {
  unsigned long error;
  unsigned long seconds = options.seconds;
  ULONGLONG minimum_bytes = options.bytes;

  /* Now. */
  SYSTEMTIME st;
  GetSystemTime(&st);

  BY_HANDLE_FILE_INFORMATION info;

  /* Try to open the file to check if it exists and to get attributes. */
  HANDLE file = CreateFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (file != INVALID_HANDLE_VALUE) {
    /* Get file attributes. */
    if (! GetFileInformationByHandle(file, &info)) {
      /* Reuse current time for rotation timestamp. */
      seconds = 0;
      minimum_bytes = 0;
      SystemTimeToFileTime(&st, &info.ftLastWriteTime);
    }

    CloseHandle(file);
  }
  else {
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, _T("CreateFile()"), path, error_string(error));
    /* Reuse current time for rotation timestamp. */
    seconds = 0;
    minimum_bytes = 0;
    SystemTimeToFileTime(&st, &info.ftLastWriteTime);
  }

  /* Check file age. */
  if (seconds) {
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    ULARGE_INTEGER s;
    s.LowPart = ft.dwLowDateTime;
    s.HighPart = ft.dwHighDateTime;
    s.QuadPart -= seconds * 10000000LL;
    ft.dwLowDateTime = s.LowPart;
    ft.dwHighDateTime = s.HighPart;
    if (CompareFileTime(&info.ftLastWriteTime, &ft) > 0) return;
  }

  /* Check file size. */
  if (minimum_bytes) {
    ULARGE_INTEGER bytes;
    bytes.LowPart = info.nFileSizeLow;
    bytes.HighPart = info.nFileSizeHigh;
    if (bytes.QuadPart < minimum_bytes) return;
  }

  /* Get new filename. */
  FileTimeToSystemTime(&info.ftLastWriteTime, &st);

  TCHAR rotated[PATH_LENGTH];
  rotated_filename(path, rotated, _countof(rotated), &st);

  /* Rotate. */
  bool ok = true;
  TCHAR *function;
  if (options.copy_and_truncate) {
    function = _T("CopyFile()");
    if (CopyFile(path, rotated, TRUE)) {
      file = write_to_file(path, NSSM_STDOUT_SHARING, 0, NSSM_STDOUT_DISPOSITION, NSSM_STDOUT_FLAGS);
      Sleep(options.delay_ms);
      SetFilePointer(file, 0, 0, FILE_BEGIN);
      SetEndOfFile(file);
      CloseHandle(file);
    }
    else ok = false;
  }
  else {
    function = _T("MoveFile()");
    if (! MoveFile(path, rotated)) ok = false;
  }
  if (ok) {
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ROTATED, service_name, path, rotated);
    return;
  }
  error = GetLastError();

  if (error == ERROR_FILE_NOT_FOUND) return;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, function, rotated, error_string(error));
  return;
}

int get_output_handles(nssm_service_t *service, STARTUPINFO *si) {
  if (! si) return 1;
  logger_lock lock(service);
  bool inherit_handles = false;

  /* Allocate a new console so we get a fresh stdin, stdout and stderr. */
  alloc_console(service);

  /* stdin */
  if (service->stdin_path[0]) {
    /* This launch-scoped handle is closed on both CreateProcess outcomes. */
    SECURITY_ATTRIBUTES security = { sizeof(security), NULL, TRUE };
    si->hStdInput = CreateFile(service->stdin_path, FILE_READ_DATA, service->stdin_sharing, &security, service->stdin_disposition, service->stdin_flags, 0);
    if (si->hStdInput == INVALID_HANDLE_VALUE) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stdin_path, error_string(GetLastError()));
      return 2;
    }

    inherit_handles = true;

  }

  /* stdout */
  if (service->stdout_path[0]) {
    if (service->rotate_files) rotate_file(service->name, service->stdout_path, offline_rotation_options(service, service->stdout_copy_and_truncate));
    HANDLE stdout_handle = write_to_file(service->stdout_path, service->stdout_sharing, 0, service->stdout_disposition, service->stdout_flags);
    if (stdout_handle == INVALID_HANDLE_VALUE) return 4;
    service->stdout_si = 0;

    if (service->use_stdout_pipe) {
      service->stdout_thread = create_logging_thread(service->name, service->stdout_path, service->stdout_sharing, service->stdout_disposition, service->stdout_flags, &service->stdout_si, &stdout_handle, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, &service->stdout_logger, service->rotate_stdout_online, service->timestamp_log, service->stdout_copy_and_truncate);
    }
    else service->stdout_thread = 0;

    if (! service->stdout_thread) {
      if (dup_handle(stdout_handle, &service->stdout_si, NSSM_REG_STDOUT, _T("stdout"), DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) return 4;
      service->rotate_stdout_online = NSSM_ROTATE_OFFLINE;
    }

    if (dup_handle(service->stdout_si, &si->hStdOutput, _T("stdout_si"), _T("stdout"))) return 4;

    inherit_handles = true;

  }

  /* stderr */
  if (service->stderr_path[0]) {
    /* Same as stdout? */
    if (str_equiv(service->stderr_path, service->stdout_path)) {
      service->stderr_sharing = service->stdout_sharing;
      service->stderr_disposition = service->stdout_disposition;
      service->stderr_flags = service->stdout_flags;
      service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;

      /* Two handles to the same file will create a race. */
      /* XXX: Here we assume that either both or neither handle must be a pipe. */
      if (dup_handle(service->stdout_si, &service->stderr_si, _T("stdout"), _T("stderr"))) return 6;
    }
    else {
      if (service->rotate_files) rotate_file(service->name, service->stderr_path, offline_rotation_options(service, service->stderr_copy_and_truncate));
      HANDLE stderr_handle = write_to_file(service->stderr_path, service->stderr_sharing, 0, service->stderr_disposition, service->stderr_flags);
      if (stderr_handle == INVALID_HANDLE_VALUE) return 7;
      service->stderr_si = 0;

      if (service->use_stderr_pipe) {
        service->stderr_thread = create_logging_thread(service->name, service->stderr_path, service->stderr_sharing, service->stderr_disposition, service->stderr_flags, &service->stderr_si, &stderr_handle, service->rotate_bytes_low, service->rotate_bytes_high, service->rotate_delay, &service->stderr_logger, service->rotate_stderr_online, service->timestamp_log, service->stderr_copy_and_truncate);
      }
      else service->stderr_thread = 0;

      if (! service->stderr_thread) {
        if (dup_handle(stderr_handle, &service->stderr_si, NSSM_REG_STDERR, _T("stderr"), DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) return 7;
        service->rotate_stderr_online = NSSM_ROTATE_OFFLINE;
      }
    }

    if (dup_handle(service->stderr_si, &si->hStdError, _T("stderr_si"), _T("stderr"))) return 7;

    inherit_handles = true;

  }

  /*
    We need to set the startup_info flags to make the new handles
    inheritable by the new process.
  */
  if (inherit_handles) si->dwFlags |= STARTF_USESTDHANDLES;

  return 0;
}

/* Reuse output handles for a hook. */
int use_output_handles(nssm_service_t *service, STARTUPINFO *si) {
  logger_lock lock(service);
  si->dwFlags &= ~STARTF_USESTDHANDLES;

  if (service->stdout_si) {
    if (dup_handle(service->stdout_si, &si->hStdOutput, _T("stdout_pipe"), _T("hStdOutput"))) return 1;
    si->dwFlags |= STARTF_USESTDHANDLES;
  }

  if (service->stderr_si) {
    if (dup_handle(service->stderr_si, &si->hStdError, _T("stderr_pipe"), _T("hStdError"))) {
      if (si->hStdOutput) {
        si->dwFlags &= ~STARTF_USESTDHANDLES;
        close_handle(&si->hStdOutput);
      }
      return 2;
    }
    si->dwFlags |= STARTF_USESTDHANDLES;
  }

  return 0;
}

void close_output_handles(STARTUPINFO *si) {
  close_handle(&si->hStdInput);
  close_handle(&si->hStdOutput);
  close_handle(&si->hStdError);
}

/* Optional on pre-Vista Windows; cancellation is a request, not completion. */
static void cancel_logger_io(HANDLE thread) {
  typedef BOOL (WINAPI *cancel_t)(HANDLE);
  HMODULE kernel32 = GetModuleHandle(_T("kernel32.dll"));
  if (! kernel32) return;
  cancel_t cancel = (cancel_t) GetProcAddress(kernel32, "CancelSynchronousIo");
  if (cancel) cancel(thread);
}

static void join_logger(HANDLE *thread, logger_t **owner) {
  logger_t *logger = *owner;
  *owner = NULL;
  if (*thread) {
    DWORD result = WaitForSingleObject(*thread, NSSM_CLEANUP_LOGGERS_DEADLINE);
    if (result != WAIT_OBJECT_0 && logger) {
      SetEvent(logger->stop_event);
      cancel_logger_io(*thread);
      /* Never close the worker's I/O handles, even if this wait also fails. */
      WaitForSingleObject(*thread, 100);
    }
    close_handle(thread);
  }
  release_logger(logger);
}

void set_logger_rotation(nssm_service_t *service, bool requested) {
  logger_lock lock(service);
  logger_t *loggers[] = { service->stdout_logger, service->stderr_logger };
  for (unsigned i = 0; i < _countof(loggers); i++) {
    if (! loggers[i]) continue;
    if (requested) InterlockedCompareExchange(&loggers[i]->rotate_online, NSSM_ROTATE_ONLINE_ASAP, NSSM_ROTATE_ONLINE);
    else InterlockedExchange(&loggers[i]->draining, 1);
  }
}

void cleanup_loggers(nssm_service_t *service) {
  logger_lock lock(service);
  set_logger_rotation(service, false);
  /* Close BOTH writers before joining: stderr may share stdout's pipe. */
  close_handle(&service->stdout_si);
  close_handle(&service->stderr_si);
  join_logger(&service->stdout_thread, &service->stdout_logger);
  join_logger(&service->stderr_thread, &service->stderr_logger);
}

/*
  Try multiple times to read from a file.
  Returns:  0 on success.
            1 on non-fatal error.
           -1 on fatal error.
           -2 on normal pipe EOF.
*/
static int try_read(logger_t *logger, void *address, unsigned long bufsize, unsigned long *in, int *complained) {
  int ret = 1;
  unsigned long error;
  for (int tries = 0; tries < 5; tries++) {
    *in = 0;
    if (logger_stopping(logger)) return -1;
    if (ReadFile(logger->read_handle, address, bufsize, in, 0)) return 0;

    error = GetLastError();
    switch (error) {
      /* Other end closed the pipe. */
      case ERROR_BROKEN_PIPE:
        return -2; // Normal producer EOF, including when rotation is enabled.

      /* Couldn't lock the buffer. */
      case ERROR_NOT_ENOUGH_QUOTA:
        if (logger_stopping(logger, 2000 + tries * 3000)) return -1;
        ret = 1;
        continue;

      /* Write was cancelled by the other end. */
      case ERROR_OPERATION_ABORTED:
        ret = 1;
        goto complain_read;

      default:
        ret = -1;
    }
  }

complain_read:
  /* Ignore the error if we've been requested to exit anyway. */
  if (InterlockedCompareExchange(&logger->draining, 0, 0) || InterlockedCompareExchange(&logger->rotate_online, 0, 0) != NSSM_ROTATE_ONLINE) return ret;
  if (! (*complained & COMPLAINED_READ)) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_READFILE_FAILED, logger->service_name, logger->path, error_string(error));
  *complained |= COMPLAINED_READ;
  return ret;
}

/* Write all bytes synchronously, retaining progress across short writes. */
static int try_write(logger_t *logger, const void *address, unsigned long bufsize, unsigned long *out, int *complained) {
  *out = 0;
  unsigned long error = ERROR_WRITE_FAULT;
  int tries = 0;
  bool failed = false;
  while (*out < bufsize) {
    if (logger_stopping(logger)) return -1;
    unsigned long written = 0;
    BOOL ok = WriteFile(logger->write_handle, (const char *) address + *out, bufsize - *out, &written, 0);
    error = ok ? ERROR_WRITE_FAULT : GetLastError();
    if (written > bufsize - *out) { error = ERROR_INVALID_DATA; failed = true; break; }
    *out += written;
    if (ok && written) { tries = 0; continue; }
    /* A successful zero-byte write must not spin forever. No overlapped I/O. */
    if (ok || (error != ERROR_NOT_ENOUGH_QUOTA && error != ERROR_DISK_FULL) || ++tries == 5) { failed = true; break; }
    if (logger_stopping(logger, 2000 + (tries - 1) * 3000)) return -1;
  }
  if (! failed && *out == bufsize) return 0;
  if (! (*complained & COMPLAINED_WRITE)) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_WRITEFILE_FAILED, logger->service_name, logger->path, error_string(error));
  *complained |= COMPLAINED_WRITE;
  /* Stop after an incomplete write; continuing could corrupt encoding or a timestamp. */
  return -1;
}

static int write_timestamp(logger_t *logger, unsigned long charsize, unsigned long *out, int *complained) {
  *out = 0;
  char timestamp[TIMESTAMP_LEN + 1];
  SYSTEMTIME now;
  GetSystemTime(&now);
  if (_snprintf_s(timestamp, _countof(timestamp), _TRUNCATE, TIMESTAMP_FORMAT, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds) != TIMESTAMP_LEN) return -1;
  if (charsize == 1) return try_write(logger, timestamp, TIMESTAMP_LEN, out, complained);
  /* The format contains ASCII only. No allocation or borrowed conversion buffer. */
  wchar_t utf16[TIMESTAMP_LEN];
  for (unsigned i = 0; i < TIMESTAMP_LEN; i++) utf16[i] = timestamp[i];
  return try_write(logger, utf16, sizeof(utf16), out, complained);
}

static bool log_newline(const char *data, unsigned long count, unsigned long offset, unsigned long charsize) {
  return data[offset] == '\n' && (charsize == 1 || (offset + 1 < count && data[offset + 1] == 0));
}

/* Input starts at a code-unit boundary; an incomplete final unit is copied verbatim. */
static int write_with_timestamp(logger_t *logger, const void *address, unsigned long bufsize, unsigned long *out, int *complained, unsigned long charsize) {
  *out = 0;
  if (! bufsize) return 0;
  if (! logger->timestamp_log) return try_write(logger, address, bufsize, out, complained);
  const char *data = (const char *) address;
  unsigned long offset = 0;
  while (offset < bufsize) {
    unsigned long written = 0;
    if (! logger->line_length) {
      int ret = write_timestamp(logger, charsize, &written, complained);
      *out += written;
      if (ret) return ret;
      logger->line_length = 1; // Only line-start state is needed, not an unbounded length.
    }
    unsigned long end = offset;
    bool newline = false;
    while (end < bufsize) {
      newline = log_newline(data, bufsize, end, charsize);
      end += (bufsize - end < charsize) ? bufsize - end : charsize;
      if (newline) break;
    }
    int ret = try_write(logger, data + offset, end - offset, &written, complained);
    *out += written;
    if (ret) return ret;
    if (newline) logger->line_length = 0;
    offset = end;
  }
  return 0;
}

static unsigned long finish_logger(logger_t *logger, unsigned long result) {
  close_handle(&logger->read_handle);
  close_handle(&logger->write_handle);
  release_logger(logger);
  return result;
}

static bool logger_file_size(HANDLE file, ULONGLONG *size) {
  *size = 0;
  BY_HANDLE_FILE_INFORMATION info;
  if (GetFileInformationByHandle(file, &info)) {
    *size = ((ULONGLONG) info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return true;
  }
  LARGE_INTEGER value;
  if (! GetFileSizeEx(file, &value) || value.QuadPart < 0) return false;
  *size = value.QuadPart;
  return true;
}

/* Return false only if logging cannot continue. A failed rename keeps the old log. */
static bool rotate_logger(logger_t *logger, ULONGLONG *size, bool *size_known, int *complained) {
  if (logger_stopping(logger)) return false;
  InterlockedCompareExchange(&logger->rotate_online, NSSM_ROTATE_ONLINE, NSSM_ROTATE_ONLINE_ASAP);
  TCHAR rotated[PATH_LENGTH];
  rotated_filename(logger->path, rotated, _countof(rotated), NULL);
  bool ok = false;
  const TCHAR *function = _T("MoveFile()");
  unsigned long error = ERROR_SUCCESS;
  if (logger->copy_and_truncate) {
    function = _T("FlushFileBuffers()");
    if (FlushFileBuffers(logger->write_handle)) {
      function = _T("CopyFile()");
      if (CopyFile(logger->path, rotated, TRUE)) {
        if (logger_stopping(logger, logger->rotate_delay)) return false;
        LARGE_INTEGER zero = { 0 };
        function = _T("SetFilePointerEx()");
        if (SetFilePointerEx(logger->write_handle, zero, NULL, FILE_BEGIN)) {
          function = _T("SetEndOfFile()");
          ok = SetEndOfFile(logger->write_handle) != FALSE;
        }
      }
    }
    if (! ok) error = GetLastError();
    /* A failed truncation must never leave the write pointer before existing data. */
    LARGE_INTEGER zero = { 0 };
    if (! SetFilePointerEx(logger->write_handle, zero, NULL, FILE_END)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, logger->service_name, logger->path, _T("SetFilePointerEx()"), rotated, error_string(GetLastError()));
      return false;
    }
  }
  else {
    close_handle(&logger->write_handle);
    ok = MoveFile(logger->path, rotated) != FALSE;
    if (! ok) error = GetLastError();
    /* On failure always append; CREATE_ALWAYS would destroy the original file. */
    logger->write_handle = write_to_file(logger->path, logger->sharing, NULL, ok ? logger->disposition : OPEN_ALWAYS, logger->flags);
    if (logger->write_handle == INVALID_HANDLE_VALUE) return false;
  }
  if (ok) log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ROTATED, logger->service_name, logger->path, rotated);
  else {
    if (! (*complained & COMPLAINED_ROTATE)) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, logger->service_name, logger->path, function, rotated, error_string(error));
    *complained |= COMPLAINED_ROTATE;
  }
  ULONGLONG observed;
  *size_known = logger_file_size(logger->write_handle, &observed);
  if (*size_known) *size = observed;
  else if (ok) { *size = 0; *size_known = true; }
  return true;
}

/* Retain an initial encoding probe, then at most one incomplete UTF-16 byte. */
unsigned long WINAPI log_and_rotate(void *arg) {
  logger_t *logger = (logger_t *) arg;
  if (! logger) return 1;
  ULONGLONG size;
  bool size_known = logger_file_size(logger->write_handle, &size);
  char buffer[1024 + 4];
  unsigned long pending = 0, charsize = 0;
  int complained = 0;
  while (! logger_stopping(logger)) {
    unsigned long in = 0;
    int ret = try_read(logger, buffer + pending, 1024, &in, &complained);
    if (ret > 0) continue;
    if (logger_stopping(logger)) break;
    if (ret == -1) return finish_logger(logger, 2);
    bool eof = ret == -2 || ! in;
    in += pending;
    if (! in && eof) break;
    if (! charsize) {
      bool bom = in >= 2 && (unsigned char) buffer[0] == 0xff && (unsigned char) buffer[1] == 0xfe;
      /* IsTextUnicode cannot reliably classify a single BOM-less code unit. */
      if (! bom && in < 4 && ! eof) { pending = in; continue; }
      charsize = bom ? 2 : (in >= 2 ? guess_charsize(buffer, in) : 1);
      if (bom) {
        in -= 2; memmove(buffer, buffer + 2, in);
        if (size_known && ! size) {
          const wchar_t mark = L'\ufeff';
          unsigned long out = 0;
          ret = try_write(logger, &mark, sizeof(mark), &out, &complained);
          size += out;
          if (ret) return finish_logger(logger, 3);
        }
      }
    }
    unsigned long complete = (charsize == 2 && ! eof) ? in & ~1UL : in;
    unsigned long offset = 0;
    while (offset < complete) {
      unsigned long end = offset;
      bool newline = false;
      while (end < complete) {
        newline = log_newline(buffer, complete, end, charsize);
        end += (complete - end < charsize) ? complete - end : charsize;
        if (newline) break;
      }
      unsigned long out = 0;
      if (charsize == 2 && size_known && ! size) {
        const wchar_t bom = L'\ufeff';
        ret = try_write(logger, &bom, sizeof(bom), &out, &complained);
        size += out;
        if (ret) return finish_logger(logger, 3);
      }
      ret = write_with_timestamp(logger, buffer + offset, end - offset, &out, &complained, charsize);
      size += out;
      if (ret) return finish_logger(logger, 3);
      offset = end;
      LONG rotation = InterlockedCompareExchange(&logger->rotate_online, 0, 0);
      if (newline && ! InterlockedCompareExchange(&logger->draining, 0, 0) &&
          (rotation == NSSM_ROTATE_ONLINE_ASAP || (rotation == NSSM_ROTATE_ONLINE && logger->size && size >= logger->size))) {
        if (! rotate_logger(logger, &size, &size_known, &complained)) return finish_logger(logger, 4);
      }
    }
    pending = in - complete;
    if (pending) memmove(buffer, buffer + complete, pending);
    if (eof) break;
  }
  return finish_logger(logger, 0);
}
