#include "rotation_fixture.h"

namespace rotation_test {
bool active;
static bool fail_copy, fail_move, override_size;
static ULONGLONG reported_size;
static unsigned copies, moves, rotated_events, failed_events;
static std::vector<DWORD> sleeps;
static SYSTEMTIME clock_time = {2025, 1, 0, 2, 3, 4, 5, 0};

BOOL WINAPI copy(LPCTSTR from, LPCTSTR to, BOOL fail_exists) {
  if (active) {
    copies++;
    if (fail_copy) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  }
  return CopyFile(from, to, fail_exists);
}
BOOL WINAPI move(LPCTSTR from, LPCTSTR to) {
  if (active) {
    moves++;
    if (fail_move) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  }
  return MoveFile(from, to);
}
BOOL WINAPI information(HANDLE h, LPBY_HANDLE_FILE_INFORMATION info) {
  BOOL result = GetFileInformationByHandle(h, info);
  if (active && result && override_size) {
    info->nFileSizeHigh = static_cast<DWORD>(reported_size >> 32);
    info->nFileSizeLow = static_cast<DWORD>(reported_size);
  }
  return result;
}
void WINAPI sleep(DWORD milliseconds) {
  if (active) sleeps.push_back(milliseconds);
  else Sleep(milliseconds);
}
void WINAPI now(LPSYSTEMTIME value) {
  if (active) *value = clock_time;
  else GetSystemTime(value);
}
void event(unsigned long id) {
  if (! active) return;
  if (id == NSSM_EVENT_ROTATED) rotated_events++;
  if (id == NSSM_EVENT_ROTATE_FILE_FAILED) failed_events++;
}

struct fixture {
  nssm_service_t *service;
  STARTUPINFO si;
  TCHAR directory[MAX_PATH];
  std::vector<std::basic_string<TCHAR> > paths;
  fixture() : service(alloc_nssm_service()), si() {
    REQUIRE(service);
    active = true;
    fail_copy = fail_move = override_size = false;
    copies = moves = rotated_events = failed_events = 0; sleeps.clear();
    TCHAR temp[MAX_PATH];
    REQUIRE(GetTempPath(_countof(temp), temp));
    REQUIRE(GetTempFileName(temp, _T("nsr"), 0, directory));
    REQUIRE(DeleteFile(directory));
    REQUIRE(CreateDirectory(directory, NULL));
    _tcscpy_s(service->name, _T("Offline rotation test"));
    service->stdout_sharing = service->stderr_sharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
    service->stdout_disposition = service->stderr_disposition = OPEN_ALWAYS;
    service->stdout_flags = service->stderr_flags = FILE_ATTRIBUTE_NORMAL;
    service->rotate_files = true;
    InitializeCriticalSection(&service->hook_section);
    service->hook_section_initialised = true;
  }
  ~fixture() {
    stop(); cleanup_nssm_service(service);
    active = false;
    for (size_t i = 0; i < paths.size(); i++) {
      if (! DeleteFile(paths[i].c_str())) CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    }
    CHECK(RemoveDirectory(directory));
  }
  void stop() { close_output_handles(&si); cleanup_loggers(service); }
  std::basic_string<TCHAR> prepare(bool stderr_stream, const std::string &bytes, LONGLONG age_seconds = 120) {
    TCHAR *path = stderr_stream ? service->stderr_path : service->stdout_path;
    _sntprintf_s(path, PATH_LENGTH, _TRUNCATE, _T("%s\\%s.log"), directory, stderr_stream ? _T("stderr") : _T("stdout"));
    paths.push_back(path);
    HANDLE file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    DWORD written;
    REQUIRE(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL));
    REQUIRE(written == bytes.size());
    FILETIME time;
    REQUIRE(SystemTimeToFileTime(&clock_time, &time));
    ULARGE_INTEGER ticks; ticks.LowPart = time.dwLowDateTime; ticks.HighPart = time.dwHighDateTime;
    ticks.QuadPart -= age_seconds * 10000000LL;
    time.dwLowDateTime = ticks.LowPart; time.dwHighDateTime = ticks.HighPart;
    REQUIRE(SetFileTime(file, NULL, NULL, &time)); CloseHandle(file);
    SYSTEMTIME stamp;
    REQUIRE(FileTimeToSystemTime(&time, &stamp));
    TCHAR rotated[PATH_LENGTH];
    _sntprintf_s(rotated, _countof(rotated), _TRUNCATE, _T("%s\\%s-%04u%02u%02uT%02u%02u%02u.%03u.log"), directory,
      stderr_stream ? _T("stderr") : _T("stdout"), stamp.wYear, stamp.wMonth, stamp.wDay, stamp.wHour, stamp.wMinute, stamp.wSecond, stamp.wMilliseconds);
    paths.push_back(rotated);
    return rotated;
  }
  std::string contents(const TCHAR *path) {
    HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    char buffer[4096]; DWORD count; std::string text;
    while (ReadFile(file, buffer, sizeof(buffer), &count, NULL) && count) text.append(buffer, count);
    CloseHandle(file); return text;
  }
};

static void small_file_regression(bool stderr_stream) {
  fixture f;
  std::basic_string<TCHAR> archive = f.prepare(stderr_stream, "data");
  f.service->rotate_bytes_low = 1048576;
  f.service->rotate_delay = 0;
  f.service->stdout_copy_and_truncate = f.service->stderr_copy_and_truncate = true;
  REQUIRE(get_output_handles(f.service, &f.si) == 0);
  f.stop();
  CHECK(copies == 0 && moves == 0);
  CHECK(sleeps.empty());
  CHECK(f.contents(stderr_stream ? f.service->stderr_path : f.service->stdout_path) == "data");
  CHECK(GetFileAttributes(archive.c_str()) == INVALID_FILE_ATTRIBUTES);
  if (! sleeps.empty()) printf("Unexpected offline rotation delay requested: %lu ms\n", sleeps[0]);
}

static bool exists(const std::basic_string<TCHAR> &path) {
  return GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void policy_case(bool stderr_stream, bool copy_mode, ULONGLONG minimum, ULONGLONG size,
  DWORD seconds, LONGLONG age, DWORD delay, bool expected) {
  fixture f;
  const std::string original = size ? "data" : "";
  std::basic_string<TCHAR> archive = f.prepare(stderr_stream, original, age);
  // High-word cases model metadata; they never create multi-gigabyte files.
  override_size = size != original.size(); reported_size = size;
  f.service->rotate_bytes_low = static_cast<DWORD>(minimum);
  f.service->rotate_bytes_high = static_cast<DWORD>(minimum >> 32);
  f.service->rotate_seconds = seconds;
  f.service->rotate_delay = delay;
  f.service->stdout_copy_and_truncate = f.service->stderr_copy_and_truncate = copy_mode;
  REQUIRE(get_output_handles(f.service, &f.si) == 0); f.stop();
  CHECK(copies == (expected && copy_mode ? 1U : 0U));
  CHECK(moves == (expected && ! copy_mode ? 1U : 0U));
  CHECK(rotated_events == (expected ? 1U : 0U) && failed_events == 0);
  CHECK(sleeps.size() == (expected && copy_mode ? 1U : 0U));
  if (! sleeps.empty()) CHECK(sleeps[0] == delay);
  CHECK(exists(archive) == expected);
  CHECK(f.contents(stderr_stream ? f.service->stderr_path : f.service->stdout_path) == (expected ? "" : original));
  if (exists(archive)) CHECK(f.contents(archive.c_str()) == original);
}

static void failures_and_collision(bool stderr_stream, bool copy_mode, bool collision) {
  fixture f;
  std::basic_string<TCHAR> archive = f.prepare(stderr_stream, "data");
  f.service->rotate_bytes_low = 4; f.service->rotate_delay = 123;
  f.service->stdout_copy_and_truncate = f.service->stderr_copy_and_truncate = copy_mode;
  if (collision) {
    HANDLE file = CreateFile(archive.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    DWORD written; REQUIRE(WriteFile(file, "keep", 4, &written, NULL) && written == 4); CloseHandle(file);
  }
  else { fail_copy = copy_mode; fail_move = ! copy_mode; }
  REQUIRE(get_output_handles(f.service, &f.si) == 0); f.stop();
  CHECK(copies == (copy_mode ? 1U : 0U) && moves == (copy_mode ? 0U : 1U));
  CHECK(rotated_events == 0 && failed_events == 1 && sleeps.empty());
  CHECK(f.contents(stderr_stream ? f.service->stderr_path : f.service->stdout_path) == "data");
  CHECK(exists(archive) == collision);
  if (collision) CHECK(f.contents(archive.c_str()) == "keep");
}

static void both_streams(bool shared, bool stdout_copy) {
  fixture f;
  std::basic_string<TCHAR> stdout_archive = f.prepare(false, "stdout");
  std::basic_string<TCHAR> stderr_archive;
  if (shared) _tcscpy_s(f.service->stderr_path, f.service->stdout_path);
  else stderr_archive = f.prepare(true, "stderr");
  f.service->rotate_bytes_low = 6; f.service->rotate_delay = 31;
  f.service->stdout_copy_and_truncate = stdout_copy;
  f.service->stderr_copy_and_truncate = ! stdout_copy;
  REQUIRE(get_output_handles(f.service, &f.si) == 0); f.stop();
  CHECK(copies == (shared ? (stdout_copy ? 1U : 0U) : 1U));
  CHECK(moves == (shared ? (stdout_copy ? 0U : 1U) : 1U));
  CHECK(rotated_events == (shared ? 1U : 2U) && ! failed_events);
  CHECK(sleeps.size() == (shared && ! stdout_copy ? 0U : 1U));
  if (! sleeps.empty()) CHECK(sleeps[0] == 31);
  CHECK(f.contents(stdout_archive.c_str()) == "stdout");
  CHECK(f.contents(f.service->stdout_path).empty());
  if (! shared) { CHECK(f.contents(stderr_archive.c_str()) == "stderr"); CHECK(f.contents(f.service->stderr_path).empty()); }
}

static void disabled_or_missing(bool stderr_stream, bool missing, bool copy_mode) {
  fixture f;
  std::basic_string<TCHAR> archive = f.prepare(stderr_stream, "data");
  TCHAR *path = stderr_stream ? f.service->stderr_path : f.service->stdout_path;
  if (missing) REQUIRE(DeleteFile(path));
  else f.service->rotate_files = false;
  f.service->stdout_copy_and_truncate = f.service->stderr_copy_and_truncate = copy_mode;
  f.service->rotate_delay = 59;
  REQUIRE(get_output_handles(f.service, &f.si) == 0); f.stop();
  CHECK(! copies && ! moves && ! rotated_events && ! failed_events && sleeps.empty());
  CHECK(! exists(archive));
  CHECK(f.contents(path) == (missing ? "" : "data"));
}
}

void test_offline_rotation() {
  rotation_test::small_file_regression(false);
  rotation_test::small_file_regression(true);
  // Warm up file-copy infrastructure and error-message resources before
  // measuring handles across the remaining lifecycle cases.
  rotation_test::both_streams(false, true);
  rotation_test::failures_and_collision(false, true, false);
  rotation_test::failures_and_collision(false, true, true);
  rotation_test::failures_and_collision(false, false, false);
  rotation_test::failures_and_collision(false, false, true);
  DWORD handles_before = test_handle_count();
  struct size_case { ULONGLONG minimum, actual; bool rotate; };
  const ULONGLONG high = 1ULL << 32, maximum = ~0ULL;
  const size_case cases[] = {
    {0, 0, true}, {0, 4, true}, {3, 4, true}, {4, 4, true}, {5, 4, false}, {1048576, 4, false},
    {high - 1, high - 1, true}, {high, high - 1, false}, {high, high, true},
    {high + 17, high + 16, false}, {high + 17, high + 17, true}, {high + 17, high + 18, true},
    {2 * high, high + 17, false}, {maximum, maximum - 1, false}, {maximum, maximum, true}
  };
  for (int stream = 0; stream < 2; stream++) {
    for (int copy = 0; copy < 2; copy++) {
      for (size_t i = 0; i < _countof(cases); i++) {
        rotation_test::policy_case(stream != 0, copy != 0, cases[i].minimum, cases[i].actual, 0, 120, 37, cases[i].rotate);
      }
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, 60, 59, 0, false);
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, 60, 60, 0, true);
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, 60, 61, 123, true);
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, 60, -60, 123, false);
      rotation_test::policy_case(stream != 0, copy != 0, 0, 4, 0, -60, 123, true);
      rotation_test::policy_case(stream != 0, copy != 0, 5, 4, 60, 120, 123, false); // Both conditions must pass.
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, MAXDWORD, MAXDWORD - 1LL, 123, false);
      rotation_test::policy_case(stream != 0, copy != 0, 4, 4, MAXDWORD, MAXDWORD, MAXDWORD - 1, true);
      rotation_test::failures_and_collision(stream != 0, copy != 0, false);
      rotation_test::failures_and_collision(stream != 0, copy != 0, true);
      rotation_test::disabled_or_missing(stream != 0, false, copy != 0);
      rotation_test::disabled_or_missing(stream != 0, true, copy != 0);
    }
  }
  rotation_test::both_streams(false, true);
  rotation_test::both_streams(false, false);
  rotation_test::both_streams(true, true);
  rotation_test::both_streams(true, false);
  DWORD handles_after = test_handle_count();
  CHECK(handles_before == handles_after);
  printf("Offline rotation handles: before=%lu after=%lu; requested delays intercepted without sleeping\n", handles_before, handles_after);
}
