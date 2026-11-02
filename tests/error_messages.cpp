#include "test.h"
#include <map>
#include <mutex>
#include "event_capture.h"

namespace event_test {
bool active;
std::vector<record> records;
static bool fail_registration;
static unsigned opened, closed, reported;
void capture(unsigned short type, unsigned long id, unsigned short count, const TCHAR * const *strings) {
  if (! active) return;
  record entry; entry.type = type; entry.id = id;
  for (unsigned short i = 0; i < count; i++) entry.strings.push_back(strings[i] ? strings[i] : _T(""));
  records.push_back(entry);
}
static HANDLE WINAPI open(LPCTSTR, LPCTSTR) { opened++; return fail_registration ? NULL : reinterpret_cast<HANDLE>(1); }
static BOOL WINAPI report(HANDLE, WORD type, WORD, DWORD id, PSID, WORD count, DWORD, LPCTSTR *strings, LPVOID) {
  reported++; capture(type, id, count, strings); return TRUE;
}
static BOOL WINAPI close(HANDLE) { closed++; return TRUE; }
}

namespace message_test {
struct allocation { SIZE_T bytes; bool local; };
static std::map<void *, allocation> allocations;
static std::mutex lock;
static bool active, fail_allocate, fail_fallback;
static unsigned mode;
static HLOCAL WINAPI allocate(UINT flags, SIZE_T bytes) {
  if (! active) return LocalAlloc(flags, bytes);
  if (fail_allocate) return NULL;
  void *p = LocalAlloc(flags, bytes);
  if (p) { std::lock_guard<std::mutex> guard(lock); allocations[p] = {bytes, true}; }
  return p;
}
static LPVOID WINAPI heap_allocate(HANDLE heap, DWORD flags, SIZE_T bytes) {
  if (! active) return HeapAlloc(heap, flags, bytes);
  if (fail_allocate) return NULL;
  void *p = HeapAlloc(heap, flags, bytes);
  if (p) { std::lock_guard<std::mutex> guard(lock); allocations[p] = {bytes, false}; }
  return p;
}
static HLOCAL WINAPI release(HLOCAL p) {
  if (! active || ! p) return LocalFree(p);
  std::lock_guard<std::mutex> guard(lock);
  REQUIRE(allocations.count(p)); bool local = allocations[p].local;
  CHECK(local); allocations.erase(p);
  if (local) return LocalFree(p);
  HeapFree(GetProcessHeap(), 0, p); return NULL;
}
static bool capacity(TCHAR *p, size_t count) {
  std::lock_guard<std::mutex> guard(lock);
  bool valid = p && allocations.count(p) && count <= allocations[p].bytes / sizeof(TCHAR);
  CHECK(valid); return valid;
}
static DWORD WINAPI format(DWORD flags, LPCVOID source, DWORD code, DWORD language, LPTSTR buffer, DWORD count, va_list *args) {
  if (! active) return FormatMessage(flags, source, code, language, buffer, count, args);
  if (mode == 2 || (mode == 1 && language)) { SetLastError(ERROR_RESOURCE_LANG_NOT_FOUND); return 0; }
  const DWORD length = 40000;
  TCHAR *output = buffer;
  if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    output = static_cast<TCHAR *>(allocate(LPTR, (length + 1) * sizeof(TCHAR)));
    *reinterpret_cast<TCHAR **>(buffer) = output;
    if (! output) return 0;
  }
  else if (! capacity(buffer, count) || count <= length) return 0;
  for (DWORD i = 0; i < length; i++) output[i] = _T('x');
  output[length] = 0; return length;
}
static int fallback(TCHAR *buffer, size_t count, size_t truncate, const TCHAR *format, ...) {
  if (active && ! capacity(buffer, count)) return -1;
  if (active && fail_fallback) return -1;
  va_list args; va_start(args, format);
  int result = _vsntprintf_s(buffer, count, truncate, format, args);
  va_end(args); return result;
}
}
#define LocalAlloc message_test::allocate
#define LocalFree message_test::release
#define HeapAlloc message_test::heap_allocate
#undef FormatMessage
#define FormatMessage message_test::format
#undef _sntprintf_s
#define _sntprintf_s message_test::fallback
#undef RegisterEventSource
#undef ReportEvent
#define RegisterEventSource event_test::open
#define ReportEvent event_test::report
#define DeregisterEventSource event_test::close
#include "../src/event.cpp"
#undef RegisterEventSource
#undef ReportEvent
#undef DeregisterEventSource
#undef LocalAlloc
#undef LocalFree
#undef HeapAlloc
#undef FormatMessage
#undef _sntprintf_s

namespace message_test {
struct worker { HANDLE ready, gate; TCHAR *buffer; };
static DWORD WINAPI run(void *arg) {
  worker *w = static_cast<worker *>(arg);
  w->buffer = error_string(ERROR_ACCESS_DENIED);
  SetEvent(w->ready);
  return WaitForSingleObject(w->gate, 5000) == WAIT_OBJECT_0 ? 0 : 1;
}
struct workers_fixture {
  worker workers[2]; HANDLE threads[2];
  workers_fixture() { ZeroMemory(workers, sizeof(workers)); ZeroMemory(threads, sizeof(threads)); }
  ~workers_fixture() {
    for (unsigned i = 0; i < 2; i++) if (workers[i].gate) SetEvent(workers[i].gate);
    for (unsigned i = 0; i < 2; i++) {
      if (threads[i]) { CHECK(WaitForSingleObject(threads[i], 6000) == WAIT_OBJECT_0); CloseHandle(threads[i]); }
      if (workers[i].ready) CloseHandle(workers[i].ready);
      if (workers[i].gate) CloseHandle(workers[i].gate);
    }
  }
  void start() {
    for (unsigned i = 0; i < 2; i++) {
      workers[i].ready = CreateEvent(NULL, TRUE, FALSE, NULL);
      workers[i].gate = CreateEvent(NULL, TRUE, FALSE, NULL);
      REQUIRE(workers[i].ready && workers[i].gate);
      threads[i] = CreateThread(NULL, 0, run, &workers[i], 0, NULL); REQUIRE(threads[i]);
    }
    for (unsigned i = 0; i < 2; i++) REQUIRE(WaitForSingleObject(workers[i].ready, 5000) == WAIT_OBJECT_0);
  }
};
}

void test_event_insertions() {
  using namespace event_test;
  active = true; records.clear(); opened = closed = reported = 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED);
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, _T("app.exe"), _T("Test"), _T("3"));
  log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, _T("Test"), _T("test.log"), static_cast<const TCHAR *>(NULL));
  REQUIRE(records.size() == 3);
  CHECK(records[0].strings.empty());
  CHECK(records[1].strings == std::vector<std::basic_string<TCHAR> >({_T("app.exe"), _T("Test"), _T("3")}));
  CHECK(records[2].strings.size() == 3 && records[2].strings[2].empty());
  CHECK(opened == 3 && closed == 3 && reported == 3);
  fail_registration = true;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED);
  CHECK(opened == 4 && closed == 3 && reported == 3);
  fail_registration = false; active = false;
  test_launch_events(); test_pipe_events();
}

void test_error_messages() {
  using namespace message_test;
  error_buffer.clear();
  active = true;
  TCHAR *message = error_string(ERROR_ACCESS_DENIED);
  CHECK(message && _tcslen(message) == 40000);
  for (unsigned i = 0; i < 100; i++) CHECK(error_string(i) == message);
  CHECK(allocations.size() == 1);
  mode = 1;
  CHECK(error_string(1) == message && _tcslen(message) == 40000);
  TCHAR *owned = message_string(1);
  CHECK(owned && _tcslen(owned) == 40000);
  release(owned);
  mode = 2;
  CHECK(! _tcscmp(error_string(MAXDWORD), _T("system error 4294967295")));
  owned = message_string(MAXDWORD);
  CHECK(owned && ! _tcscmp(owned, _T("system error 4294967295")));
  if (owned) release(owned);
  fail_fallback = true;
  CHECK(message_string(MAXDWORD) == NULL);
  fail_fallback = false;
  error_buffer.clear();
  CHECK(allocations.empty());
  fail_allocate = true;
  CHECK(error_string(1) && ! error_buffer.value);
  CHECK(message_string(1) == NULL);
  CHECK(allocations.empty());
  fail_allocate = false; mode = 0;
  message = error_string(1);
  for (unsigned iteration = 0; iteration < 20; iteration++) {
    {
      workers_fixture f; f.start();
      CHECK(f.workers[0].buffer != f.workers[1].buffer && f.workers[0].buffer != message && f.workers[1].buffer != message);
      { std::lock_guard<std::mutex> guard(lock); CHECK(allocations.size() == 3); }
    }
    CHECK(allocations.size() == 1); // C++ TLS destructor frees each returning worker's buffer.
  }
  error_buffer.clear(); CHECK(allocations.empty()); active = false;
  CHECK(error_string(ERROR_ACCESS_DENIED) && _tcslen(error_string(ERROR_ACCESS_DENIED)) > 0);
  owned = message_string(NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
  CHECK(owned && _tcslen(owned) > 0); LocalFree(owned);
}
