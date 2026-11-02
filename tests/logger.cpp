#include "test.h"

namespace logger_test {
static bool checking;
static HANDLE first_writer, second_writer;
static bool first_closed, second_closed;
static volatile LONG allocations, invalid_closes, cancellations;
static bool active, fail_alloc, fail_event, fail_pipe, no_cancel, missing_cancel, quick_wait;
static int thread_mode, duplicate_fail, duplicate_calls, file_fail, file_calls;
static DWORD forced_wait;
static HANDLE deferred_thread, blocked_output, write_entered;
static LPTHREAD_START_ROUTINE deferred_routine;
static void *deferred_argument;
static LPVOID WINAPI allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (fail_alloc) { fail_alloc = false; return NULL; }
  void *p = HeapAlloc(heap, flags, bytes);
  if (active && p) InterlockedIncrement(&allocations);
  return p;
}
static BOOL WINAPI release(HANDLE heap, DWORD flags, LPVOID p) {
  if (active && p) InterlockedDecrement(&allocations);
  return HeapFree(heap, flags, p);
}
static HANDLE WINAPI event(LPSECURITY_ATTRIBUTES a, BOOL manual, BOOL initial, LPCTSTR name) {
  if (fail_event) { fail_event = false; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
  return CreateEvent(a, manual, initial, name);
}
static BOOL WINAPI pipe(PHANDLE read, PHANDLE write, LPSECURITY_ATTRIBUTES a, DWORD size) {
  if (fail_pipe) { fail_pipe = false; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
  return CreatePipe(read, write, a, size);
}
static HANDLE WINAPI thread(LPSECURITY_ATTRIBUTES a, SIZE_T stack, LPTHREAD_START_ROUTINE routine, LPVOID argument, DWORD flags, LPDWORD id) {
  if (thread_mode == 1) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
  if (thread_mode == 2) {
    deferred_routine = routine; deferred_argument = argument;
    return deferred_thread = CreateEvent(NULL, TRUE, FALSE, NULL);
  }
  if (thread_mode == 3) {
    SetEvent(static_cast<logger_t *>(argument)->stop_event);
    routine(argument); // Completion before CreateThread returns.
    return CreateEvent(NULL, TRUE, TRUE, NULL);
  }
  return CreateThread(a, stack, routine, argument, flags, id);
}
static BOOL WINAPI duplicate(HANDLE sp, HANDLE source, HANDLE dp, LPHANDLE dest, DWORD access, BOOL inherit, DWORD options) {
  if (active && ++duplicate_calls == duplicate_fail) {
    if (options & DUPLICATE_CLOSE_SOURCE) CloseHandle(source);
    *dest = NULL;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  return DuplicateHandle(sp, source, dp, dest, access, inherit, options);
}
static HANDLE WINAPI file(LPCTSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES a, DWORD disposition, DWORD flags, HANDLE template_file) {
  if (active && ++file_calls == file_fail) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
  return CreateFile(path, access, sharing, a, disposition, flags, template_file);
}
static BOOL WINAPI write(HANDLE h, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED overlap) {
  if (h == blocked_output && write_entered) SetEvent(write_entered);
  return WriteFile(h, data, size, written, overlap);
}
static BOOL WINAPI cancel(HANDLE h) {
  InterlockedIncrement(&cancellations);
  if (no_cancel) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
  typedef BOOL (WINAPI *cancel_t)(HANDLE);
  cancel_t fn = reinterpret_cast<cancel_t>(GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "CancelSynchronousIo"));
  return fn ? fn(h) : FALSE;
}
static FARPROC WINAPI lookup(HMODULE module, LPCSTR name) {
  if (! strcmp(name, "CancelSynchronousIo")) return missing_cancel ? NULL : reinterpret_cast<FARPROC>(cancel);
  return GetProcAddress(module, name);
}
static BOOL WINAPI close(HANDLE handle) {
  if (checking) {
    if (handle == first_writer) first_closed = true;
    if (handle == second_writer) second_closed = true;
  }
  BOOL result = CloseHandle(handle);
  if (active && ! result) InterlockedIncrement(&invalid_closes);
  return result;
}
static DWORD WINAPI wait(HANDLE handle, DWORD timeout) {
  if (checking) CHECK(first_closed && second_closed);
  if (handle == deferred_thread && forced_wait) return forced_wait;
  if (quick_wait && timeout == NSSM_CLEANUP_LOGGERS_DEADLINE) timeout = 20;
  DWORD result = WaitForSingleObject(handle, timeout);
  if (checking) CHECK(result != WAIT_FAILED);
  return result;
}
static void log(unsigned short, unsigned long, ...) {}
static void console(nssm_service_t *) {}
}

#define HeapAlloc logger_test::allocate
#define HeapFree logger_test::release
#undef CreateEvent
#define CreateEvent logger_test::event
#define CreatePipe logger_test::pipe
#define CreateThread logger_test::thread
#define DuplicateHandle logger_test::duplicate
#undef CreateFile
#define CreateFile logger_test::file
#define WriteFile logger_test::write
#define GetProcAddress logger_test::lookup
#define alloc_console logger_test::console
#define CloseHandle logger_test::close
#define WaitForSingleObject logger_test::wait
#define log_event logger_test::log
#include "../src/io.cpp"
#undef CloseHandle
#undef WaitForSingleObject
#undef log_event
#undef HeapAlloc
#undef HeapFree
#undef CreateEvent
#undef CreatePipe
#undef CreateThread
#undef DuplicateHandle
#undef CreateFile
#undef WriteFile
#undef GetProcAddress
#undef alloc_console
// Restore the generic names used by the test fixtures below.
#define CreateEvent CreateEventW
#define CreateFile CreateFileW

namespace logger_test {
struct fixture {
  nssm_service_t *service;
  STARTUPINFO si;
  TCHAR path[MAX_PATH], other[MAX_PATH];
  fixture(bool shared = true) : service(alloc_nssm_service()), si() {
    REQUIRE(service);
    REQUIRE(! allocations);
    active = true;
    invalid_closes = cancellations = 0;
    fail_alloc = fail_event = fail_pipe = no_cancel = missing_cancel = quick_wait = false;
    thread_mode = duplicate_fail = duplicate_calls = file_fail = file_calls = 0;
    forced_wait = 0; deferred_thread = blocked_output = write_entered = NULL;
    deferred_routine = NULL; deferred_argument = NULL;
    TCHAR temp[MAX_PATH];
    REQUIRE(GetTempPath(_countof(temp), temp));
    REQUIRE(GetTempFileName(temp, _T("nsl"), 0, path));
    REQUIRE(GetTempFileName(temp, _T("nsl"), 0, other));
    _tcscpy_s(service->name, _T("Logger ownership test"));
    _tcscpy_s(service->stdout_path, path);
    _tcscpy_s(service->stderr_path, shared ? path : other);
    service->stdout_sharing = service->stderr_sharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
    service->stdout_disposition = service->stderr_disposition = OPEN_ALWAYS;
    service->stdout_flags = service->stderr_flags = FILE_ATTRIBUTE_NORMAL;
    service->use_stdout_pipe = service->use_stderr_pipe = true;
    InitializeCriticalSection(&service->hook_section);
    service->hook_section_initialised = true;
  }
  void stop() { close_output_handles(&si); cleanup_loggers(service); }
  void dispose_service() {
    if (! service) return;
    cleanup_nssm_service(service); service = NULL;
  }
  ~fixture() {
    if (service) stop();
    dispose_service();
    CHECK(! allocations);
    CHECK(! invalid_closes);
    active = false;
    CHECK(DeleteFile(path)); CHECK(DeleteFile(other));
  }
  std::string contents(bool stderr_file = false) {
    HANDLE h = CreateFile(stderr_file ? other : path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    std::string result; char buffer[4096]; DWORD count;
    while (ReadFile(h, buffer, sizeof(buffer), &count, NULL) && count) result.append(buffer, count);
    CloseHandle(h); return result;
  }
};
static void output(HANDLE h, const std::string &text) {
  DWORD written;
  REQUIRE(WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, NULL));
  REQUIRE(written == text.size());
}
static HANDLE copy_handle(HANDLE h) {
  HANDLE copy;
  REQUIRE(DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS));
  return copy;
}
static void finish_deferred() {
  REQUIRE(deferred_routine);
  deferred_routine(deferred_argument);
  deferred_routine = NULL; deferred_argument = NULL; deferred_thread = NULL;
}
static void normal_streams(bool shared) {
  fixture f(shared);
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  DWORD flags;
  REQUIRE(GetHandleInformation(f.service->stdout_si, &flags));
  CHECK(!(flags & HANDLE_FLAG_INHERIT));
  REQUIRE(GetHandleInformation(f.si.hStdOutput, &flags));
  CHECK(flags & HANDLE_FLAG_INHERIT);
  HANDLE observer = copy_handle(f.service->stdout_thread);
  output(f.si.hStdOutput, "stdout tail\n");
  output(f.si.hStdError, "stderr tail\n");
  f.stop();
  CHECK(WaitForSingleObject(observer, 0) == WAIT_OBJECT_0); CloseHandle(observer);
  CHECK(f.contents() == (shared ? "stdout tail\nstderr tail\n" : "stdout tail\n"));
  if (! shared) CHECK(f.contents(true) == "stderr tail\n");
}
static void failure_cases() {
  for (int failure = 0; failure < 4; failure++) {
    fixture f;
    fail_alloc = failure == 0; fail_event = failure == 1;
    fail_pipe = failure == 2; thread_mode = failure == 3 ? 1 : 0;
    REQUIRE(get_output_handles(f.service, &f.si) == 0); // Direct file fallback.
    CHECK(! f.service->stdout_thread && ! f.service->stdout_logger);
    output(f.si.hStdOutput, "fallback\n"); f.stop();
    CHECK(f.contents() == "fallback\n");
  }
  for (int failure = 1; failure <= 3; failure++) {
    fixture f;
    duplicate_fail = failure;
    CHECK(get_output_handles(f.service, &f.si) != 0);
    f.stop(); // Includes failure after the logger is already running.
  }
  {
    fixture f(false); file_fail = 2;
    CHECK(get_output_handles(f.service, &f.si) != 0); f.stop();
  }
  {
    fixture f; thread_mode = 3;
    REQUIRE(get_output_handles(f.service, &f.si) == 0);
    CHECK(f.service->stdout_logger->references == 1);
    f.stop();
  }
  {
    fixture f;
    REQUIRE(get_output_handles(f.service, &f.si) == 0);
    STARTUPINFO hook = {};
    duplicate_fail = duplicate_calls + 2;
    CHECK(use_output_handles(f.service, &hook) != 0);
    CHECK(! hook.hStdOutput && ! hook.hStdError);
    close_output_handles(&hook); f.stop();
  }
}
static void detached_worker(DWORD result) {
  fixture f;
  thread_mode = 2; forced_wait = result; no_cancel = true;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  logger_t *old = f.service->stdout_logger;
  set_logger_rotation(f.service, true);
  CHECK(old->rotate_online == NSSM_ROTATE_OFFLINE);
  f.stop();
  CHECK(cancellations == 1 && old->references == 1);
  CHECK(! f.service->stdout_logger && ! f.service->stdout_thread);
  CHECK(WaitForSingleObject(old->stop_event, 0) == WAIT_OBJECT_0);
  CHECK(!_tcscmp(old->path, f.path));
  // Restart into the same service storage before the old worker gets CPU time.
  thread_mode = 0; forced_wait = 0;
  _tcscpy_s(f.service->name, _T("New generation"));
  f.service->rotate_stdout_online = NSSM_ROTATE_ONLINE;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  logger_t *current = f.service->stdout_logger;
  set_logger_rotation(f.service, true);
  CHECK(current->rotate_online == NSSM_ROTATE_ONLINE_ASAP);
  CHECK(old->draining == 1 && !_tcscmp(old->service_name, _T("Logger ownership test")));
  set_logger_rotation(f.service, false);
  finish_deferred(); // Must not close handles or modify state belonging to current.
  CHECK(current->rotate_online == NSSM_ROTATE_ONLINE_ASAP && current->references >= 1);
  output(f.si.hStdOutput, "new generation\n");
  f.stop(); f.dispose_service();
  CHECK(f.contents() == "new generation\n");
}
static void freed_service() {
  fixture f; thread_mode = 2; forced_wait = WAIT_TIMEOUT; missing_cancel = true;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  f.stop(); f.dispose_service();
  finish_deferred(); // ASan sees a real freed service, not quarantined storage.
}
static void child_writer(int mode) {
  fixture f;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  HANDLE observer = copy_handle(f.service->stdout_thread);
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  HANDLE gate = CreateEvent(&security, TRUE, FALSE, NULL);
  REQUIRE(gate);
  TCHAR exe[PATH_LENGTH], command[CMD_LENGTH];
  REQUIRE(GetModuleFileName(NULL, exe, _countof(exe)));
  _sntprintf_s(command, _countof(command), _TRUNCATE, _T("\"%s\" --logger-child %I64u"), exe, (unsigned __int64) (ULONG_PTR) gate);
  STARTUPINFO child_si = f.si;
  child_si.cb = sizeof(child_si);
  child_si.hStdInput = child_si.hStdOutput; // Unused but valid inherited handle.
  PROCESS_INFORMATION child = {};
  REQUIRE(CreateProcess(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &child_si, &child));
  CloseHandle(child.hThread);
  if (mode == 0) SetEvent(gate); // Trailing bytes arrive as parent closes writers.
  if (mode == 2) { no_cancel = true; quick_wait = true; }
  f.stop();
  if (mode == 2) {
    CHECK(WaitForSingleObject(observer, 0) == WAIT_TIMEOUT);
    f.dispose_service(); // Child still owns a writer; worker still blocks in read.
  }
  if (mode != 0) SetEvent(gate);
  REQUIRE(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(observer, 5000) == WAIT_OBJECT_0);
  if (mode == 0) CHECK(f.contents() == "child trailing output\n");
  else CHECK(cancellations == 1);
  CloseHandle(child.hProcess); CloseHandle(observer); CloseHandle(gate);
}
struct writer_context { HANDLE pipe; unsigned count; };
static DWORD WINAPI writer(void *argument) {
  writer_context *c = static_cast<writer_context *>(argument);
  std::string data(1024, 'x'); data.back() = '\n'; DWORD written;
  for (unsigned i = 0; i < c->count; i++) {
    if (! WriteFile(c->pipe, data.data(), static_cast<DWORD>(data.size()), &written, NULL)) break;
  }
  CloseHandle(c->pipe); return 0;
}
static void active_output() {
  fixture f;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  writer_context context = {copy_handle(f.si.hStdOutput), 128};
  HANDLE producer = CreateThread(NULL, 0, writer, &context, 0, NULL);
  REQUIRE(producer);
  f.stop();
  REQUIRE(WaitForSingleObject(producer, 5000) == WAIT_OBJECT_0); CloseHandle(producer);
  CHECK(f.contents().size() == 128 * 1024);
}
static void blocked_write() {
  fixture f;
  HANDLE unread;
  REQUIRE(CreatePipe(&unread, &blocked_output, NULL, 4096));
  write_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
  REQUIRE(write_entered);
  HANDLE file_handle = blocked_output;
  f.service->stdout_thread = create_logging_thread(f.service->name, f.path, 0, OPEN_ALWAYS, 0,
    &f.service->stdout_si, &file_handle, 0, 0, 0, &f.service->stdout_logger, NSSM_ROTATE_OFFLINE, false, false);
  REQUIRE(f.service->stdout_thread);
  HANDLE observer = copy_handle(f.service->stdout_thread);
  writer_context context = {copy_handle(f.service->stdout_si), 256};
  HANDLE producer = CreateThread(NULL, 0, writer, &context, 0, NULL);
  REQUIRE(producer && WaitForSingleObject(write_entered, 5000) == WAIT_OBJECT_0);
  cleanup_loggers(f.service); // Output pipe is deliberately never read.
  REQUIRE(WaitForSingleObject(observer, 5000) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(producer, 5000) == WAIT_OBJECT_0);
  CHECK(cancellations == 1);
  CloseHandle(observer); CloseHandle(producer); CloseHandle(unread); CloseHandle(write_entered);
  write_entered = blocked_output = NULL;
}
}

int logger_child(int argc, TCHAR **argv) {
  if (argc != 3) return 2;
  HANDLE gate = (HANDLE) (ULONG_PTR) _tcstoui64(argv[2], NULL, 10);
  if (WaitForSingleObject(gate, 5000) != WAIT_OBJECT_0) return 3;
  const char text[] = "child trailing output\n";
  DWORD written;
  return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, sizeof(text) - 1, &written, NULL) ? 0 : 1;
}

void test_logger_ownership() {
  nssm_service_t *service = new nssm_service_t();
  InitializeCriticalSection(&service->hook_section);
  service->stdout_thread = CreateEvent(NULL, TRUE, TRUE, NULL);
  service->stderr_thread = CreateEvent(NULL, TRUE, TRUE, NULL);
  service->stdout_si = logger_test::first_writer = CreateEvent(NULL, TRUE, TRUE, NULL);
  service->stderr_si = logger_test::second_writer = CreateEvent(NULL, TRUE, TRUE, NULL);
  REQUIRE(service->stdout_thread && service->stderr_thread && service->stdout_si && service->stderr_si);
  logger_test::checking = true;
  cleanup_loggers(service);
  logger_test::checking = false;
  CHECK(! service->stdout_thread && ! service->stderr_thread && ! service->stdout_si && ! service->stderr_si);
  DeleteCriticalSection(&service->hook_section);
  delete service;
  logger_test::normal_streams(true);
  logger_test::normal_streams(false);
  logger_test::failure_cases();
  logger_test::detached_worker(WAIT_TIMEOUT);
  logger_test::detached_worker(WAIT_FAILED);
  logger_test::freed_service();
  for (int mode = 0; mode < 3; mode++) logger_test::child_writer(mode);
  {
    logger_test::fixture f;
    REQUIRE(get_output_handles(f.service, &f.si) == 0);
    logger_test::output(f.si.hStdOutput, "service disposal\n");
    close_output_handles(&f.si); f.dispose_service();
    CHECK(f.contents() == "service disposal\n");
  }
  logger_test::active_output();
  logger_test::blocked_write();
  DWORD before = test_handle_count();
  for (unsigned i = 0; i < 100; i++) logger_test::normal_streams(i % 2 != 0);
  DWORD after = test_handle_count();
  CHECK(before == after);
  printf("Logger handles after 100 restart cycles: before=%lu after=%lu\n", before, after);
}
