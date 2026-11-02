#include "test.h"
#include <algorithm>

typedef std::basic_string<TCHAR> text_t;
void test_dump_quoting();

static void check_capacity(const text_t &input, const text_t &expected, size_t capacity) {
  // The backing allocation contains every possible legacy write. The logical
  // capacity is guarded separately, so the pre-fix test reports corruption
  // without corrupting the test process's heap.
  const TCHAR guard = _T('#');
  std::vector<TCHAR> storage(input.size() * 8 + 64, guard);
  TCHAR *output = &storage[1];
  int result = quote(input.c_str(), output, capacity);
  bool fits = capacity > expected.size();
  CHECK((result == 0) == fits);
  if (! result) CHECK(! _tcscmp(output, expected.c_str()));
  if (result) CHECK(std::count(storage.begin(), storage.end(), guard) == storage.size());
  CHECK(storage[0] == guard);
  CHECK(std::count(storage.begin() + 1 + capacity, storage.end(), guard) == storage.size() - 1 - capacity);
}

static void check_bounds(const text_t &input) {
  std::vector<TCHAR> output(input.size() * 8 + 64);
  REQUIRE(quote(input.c_str(), &output[0], output.size()) == 0);
  text_t expected(&output[0]);
  if (input.size() <= 4) {
    for (size_t capacity = 0; capacity <= expected.size() + 2; capacity++) check_capacity(input, expected, capacity);
  }
  else {
    check_capacity(input, expected, 0);
    check_capacity(input, expected, 1);
    check_capacity(input, expected, input.size());
    check_capacity(input, expected, expected.size());
    check_capacity(input, expected, expected.size() + 1);
  }
}

static void generated_bounds(text_t &input, unsigned remaining) {
  check_bounds(input);
  if (! remaining) return;
  const TCHAR alphabet[] = _T("A \t\\\"&%^|");
  for (size_t i = 0; i < _countof(alphabet) - 1; i++) {
    input.push_back(alphabet[i]);
    generated_bounds(input, remaining - 1);
    input.pop_back();
  }
}

static std::vector<text_t> arguments(bool batch) {
  const TCHAR *cases[] = { _T(""), _T("plain"), _T("two words"), _T("a\tb"),
    _T("a\"b"), _T("a\\\"b"), _T("a\\\\\"b"), _T("space \\"),
    _T("&\\\\"), _T("\\\\&\\\\"), _T("&&&&"), _T("<>|^"), _T("*"),
    _T("!NSSM_QUOTE_TEST!"), _T("final quoted argument"), _T("") };
  std::vector<text_t> result(cases, cases + _countof(cases));
  // The legacy percent escaping differs between /c and batch-file parsing.
  if (! batch) {
    result.push_back(_T("100%"));
    result.push_back(_T("%NSSM_QUOTE_TEST%"));
  }
  return result;
}

int quoting_child(int argc, TCHAR **argv) {
  if (argc < 3) return 1;
  std::vector<text_t> expected = arguments(! _tcscmp(argv[2], _T("batch")));
  if (argc != (int) expected.size() + 3) {
    fprintf(stderr, "Argument count: expected %zu, got %d\n", expected.size(), argc - 3);
    return 2;
  }
  for (size_t i = 0; i < expected.size(); i++) {
    if (expected[i] != argv[i + 3]) {
      fwprintf(stderr, L"Argument %zu: expected [%s], got [%s]\n", i, expected[i].c_str(), argv[i + 3]);
      return 3;
    }
  }
  return 0;
}

static text_t rendered(const text_t &input) {
  // Dump callers explicitly emit two quotes for an empty argument.
  if (input.empty()) return _T("\"\"");
  std::vector<TCHAR> output(input.size() * 8 + 64);
  REQUIRE(quote(input.c_str(), &output[0], output.size()) == 0);
  return &output[0];
}

class shell_fixture {
  text_t directory;
public:
  text_t executable, batch;
  shell_fixture() {
    TCHAR current[PATH_LENGTH];
    DWORD length = GetModuleFileName(NULL, current, _countof(current));
    REQUIRE(length && length < _countof(current));
    directory = current;
    directory.erase(directory.find_last_of(_T("\\/")) + 1);
    TCHAR suffix[80];
    _stprintf_s(suffix, _T("Quoting test %lu %lu"), GetCurrentProcessId(), GetTickCount());
    directory += suffix;
    REQUIRE(CreateDirectory(directory.c_str(), NULL));
    executable = directory + _T("\\argument probe.exe");
    batch = directory + _T("\\dump.cmd");
    if (! CopyFile(current, executable.c_str(), TRUE)) {
      RemoveDirectory(directory.c_str());
      throw std::runtime_error("Could not copy quoting executable");
    }
  }
  ~shell_fixture() {
    if (GetFileAttributes(batch.c_str()) != INVALID_FILE_ATTRIBUTES) CHECK(DeleteFile(batch.c_str()));
    CHECK(DeleteFile(executable.c_str()));
    CHECK(RemoveDirectory(directory.c_str()));
  }
};

static void run_cmd(const text_t &inner) {
  TCHAR system[PATH_LENGTH];
  UINT length = GetSystemDirectory(system, _countof(system));
  REQUIRE(length && length < _countof(system));
  text_t executable = text_t(system) + _T("\\cmd.exe");
  // /d disables AutoRun; /v:off makes ! literal; /s removes our outer quotes.
  text_t command = _T("\"") + executable + _T("\" /d /v:off /s /c \"") + inner + _T("\"");
  STARTUPINFO startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  REQUIRE(CreateProcess(executable.c_str(), &command[0], NULL, NULL, FALSE,
    CREATE_NO_WINDOW, NULL, NULL, &startup, &process));
  DWORD waited = WaitForSingleObject(process.hProcess, 5000);
  CHECK(waited == WAIT_OBJECT_0);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
  }
  DWORD exit_code = 1;
  CHECK(GetExitCodeProcess(process.hProcess, &exit_code));
  CHECK(exit_code == 0);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

static void shell_round_trips() {
  shell_fixture fixture;
  REQUIRE(SetEnvironmentVariable(_T("NSSM_QUOTE_TEST"), _T("expanded")));
  REQUIRE(SetEnvironmentVariable(_T("NSSM_QUOTE_TEST^"), NULL));
  REQUIRE(SetEnvironmentVariable(_T("NSSM_QUOTE_CHILD"), fixture.executable.c_str()));
  for (int batch = 0; batch < 2; batch++) {
    text_t line = batch ? _T("\"%NSSM_QUOTE_CHILD%\" --quote-child batch") : rendered(fixture.executable) + _T(" --quote-child direct");
    std::vector<text_t> inputs = arguments(batch != 0);
    for (size_t i = 0; i < inputs.size(); i++) line += _T(" ") + rendered(inputs[i]);
    if (! batch) run_cmd(line);
    else {
      text_t content = _T("@echo off\r\n") + line + _T("\r\n");
      // Only ASCII is used in the script; the Unicode executable path comes
      // from the inherited environment, avoiding code-page dependence.
      std::string bytes;
      for (size_t i = 0; i < content.size(); i++) {
        REQUIRE(content[i] <= 127);
        bytes.push_back(static_cast<char>(content[i]));
      }
      HANDLE file = CreateFile(fixture.batch.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      REQUIRE(file != INVALID_HANDLE_VALUE);
      DWORD written = 0;
      BOOL saved = WriteFile(file, bytes.data(), (DWORD) bytes.size(), &written, NULL);
      CloseHandle(file);
      REQUIRE(saved && written == bytes.size());
      run_cmd(rendered(fixture.batch));
    }
  }
  CHECK(SetEnvironmentVariable(_T("NSSM_QUOTE_CHILD"), NULL));
  CHECK(SetEnvironmentVariable(_T("NSSM_QUOTE_TEST"), NULL));
}

void test_quoting() {
  // NSSM-002: the legacy preflight accepted capacity 9, then wrote 13 TCHARs.
  check_capacity(_T("&&&&"), _T("^\"^&^&^&^&^\""), 9);
  const TCHAR *inputs[] = { _T(""), _T("plain"), _T("two words"), _T("a\tb"),
    _T("a\nb"), _T("a\vb"), _T("a\"b"), _T("a\\\"b"), _T("a\\\\\"b"),
    _T("space \\"), _T("&\\\\"), _T("\\\\&\\\\"), _T("<>|%^") };
  for (size_t i = 0; i < _countof(inputs); i++) check_bounds(inputs[i]);
  text_t input;
  generated_bounds(input, 4);
  check_bounds(text_t(32767, _T('&')));
  check_bounds(_T("&") + text_t(32767, _T('\\')));
  check_bounds(text_t(32767, _T('\\')) + _T("\""));
  TCHAR untouched = _T('#');
  CHECK(quote(_T("x"), NULL, 1) != 0);
  CHECK(quote(NULL, &untouched, 1) != 0);
  CHECK(quote(_T("x"), &untouched, (size_t) -1) != 0);
  CHECK(untouched == _T('#'));
  test_dump_quoting();
  shell_round_trips();
}
