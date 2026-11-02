#include "test.h"

typedef std::basic_string<TCHAR> text_t;
static text_t dump_output;
static text_t dump_executable = _T("C:\\Tools\\nssm.exe");
static bool capture_dump = false;

static const TCHAR *test_nssm_exe() { return dump_executable.c_str(); }

static int test_dump_printf(const TCHAR *format, ...) {
  va_list args;
  va_start(args, format);
  if (! capture_dump) {
    int result = _vtprintf(format, args);
    va_end(args);
    return result;
  }
  int length = _vsctprintf(format, args);
  va_end(args);
  REQUIRE(length >= 0);
  std::vector<TCHAR> buffer((size_t) length + 1);
  va_start(args, format);
  int result = _vstprintf_s(&buffer[0], buffer.size(), format, args);
  va_end(args);
  REQUIRE(result == length);
  dump_output += &buffer[0];
  return result;
}

// Compile the production dump helper with test-only output and executable
// selection. No fake service, registry or shell APIs enter the shipped build.
#undef _tprintf
#define _tprintf test_dump_printf
#define nssm_exe test_nssm_exe
#include "../src/settings.cpp"
#undef nssm_exe
#undef _tprintf

void test_dump_quoting() {
  capture_dump = true;
  value_t value = {};
  TCHAR valid[] = _T("final quoted argument");
  value.string = valid;
  CHECK(setting_dump_string(_T("Test"), REG_SZ, _T("AppParameters"), &value, NULL) == 0);
  CHECK(dump_output == _T("C:\\Tools\\nssm.exe set Test AppParameters \"final quoted argument\"\n"));
  dump_output.clear();
  TCHAR empty[] = _T("");
  value.string = empty;
  CHECK(setting_dump_string(_T("Test"), REG_SZ, _T("AppParameters"), &value, empty) == 0);
  CHECK(dump_output == _T("C:\\Tools\\nssm.exe set Test AppParameters \"\" \"\"\n"));
  dump_output.clear();

  // Each input fits its original setting limit. Escaping exceeds the fixed
  // dump buffer, and no part of that command may be printed on failure.
  text_t service(SERVICE_NAME_LENGTH - 1, _T('&'));
  CHECK(setting_dump_string(service.c_str(), REG_SZ, _T("AppParameters"), &value, NULL) == 1);
  CHECK(dump_output.empty());
  text_t oversized(VALUE_LENGTH - 1, _T('&'));
  value.string = &oversized[0];
  CHECK(setting_dump_string(_T("Test"), REG_SZ, _T("AppParameters"), &value, NULL) == 2);
  CHECK(dump_output.empty());
  value.string = valid;
  CHECK(setting_dump_string(_T("Test"), REG_SZ, _T("AppParameters"), &value, oversized.c_str()) == 3);
  CHECK(dump_output.empty());
  dump_executable.assign(EXE_LENGTH - 1, _T('&'));
  CHECK(setting_dump_string(_T("Test"), REG_SZ, _T("AppParameters"), &value, NULL) == 3);
  CHECK(dump_output.empty());
  dump_executable = _T("C:\\Tools\\nssm.exe");
  capture_dump = false;
}
