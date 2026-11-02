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

void test_numeric_settings() {
  registry_fixture registry;
  const TCHAR *service = _T("NSSM-Test-Numeric");
  HKEY key = open_registry(service, KEY_READ | KEY_WRITE);
  REQUIRE(key);
  CHECK(set_number(key, NSSM_REG_THROTTLE, 17) == 0);
  const TCHAR *invalid[] = { _T(""), _T("-1"), _T("4294967296"), _T("1,2"), _T("12ms") };
  for (size_t i = 0; i < _countof(invalid); i++) {
    value_t value = {};
    value.string = const_cast<TCHAR *>(invalid[i]);
    CHECK(setting_set_number(service, key, NSSM_REG_THROTTLE, NULL, &value, NULL) == -1);
    unsigned long stored = 0;
    CHECK(get_number(key, NSSM_REG_THROTTLE, &stored, false) == 1 && stored == 17);
  }
  TCHAR max_value[] = _T("4294967295");
  value_t value = {};
  value.string = max_value;
  CHECK(setting_set_number(service, key, NSSM_REG_THROTTLE, NULL, &value, NULL) == 1);
  unsigned long stored = 0;
  CHECK(get_number(key, NSSM_REG_THROTTLE, &stored, false) == 1 && stored == MAXDWORD);
  CHECK(setting_set_number(service, key, NSSM_REG_THROTTLE, NULL, NULL, NULL) == 0);
  RegCloseKey(key);

  TCHAR action[] = _T("Exit");
  value.string = action;
  const TCHAR *bad_codes[] = { _T("-1"), _T("4294967296"), _T("1,2"), _T("+") };
  for (size_t i = 0; i < _countof(bad_codes); i++) {
    CHECK(setting_set_exit_action(service, NULL, NSSM_REG_EXIT, NULL, &value, bad_codes[i]) == -1);
    value_t result = {};
    CHECK(setting_get_exit_action(service, NULL, NSSM_REG_EXIT, (void *) _T("Restart"), &result, bad_codes[i]) == -1);
    CHECK(result.string == NULL);
  }
  CHECK(setting_set_exit_action(service, NULL, NSSM_REG_EXIT, NULL, &value, max_value) == 1);
  value_t result = {};
  CHECK(setting_get_exit_action(service, NULL, NSSM_REG_EXIT, (void *) _T("Restart"), &result, max_value) == 1);
  REQUIRE(result.string);
  CHECK(! _tcscmp(result.string, action));
  HeapFree(GetProcessHeap(), 0, result.string);
}

void test_affinity_settings() {
  registry_fixture registry;
  const TCHAR *service = _T("NSSM-Test-Affinity");
  HKEY key = open_registry(service, KEY_READ | KEY_WRITE);
  REQUIRE(key);
  TCHAR valid[] = _T("0-2,2,4");
  value_t value = {};
  value.string = valid;
  CHECK(setting_set_affinity(service, key, NSSM_REG_AFFINITY, NULL, &value, NULL) == 1);
  TCHAR stored[512];
  CHECK(get_string(key, NSSM_REG_AFFINITY, stored, sizeof(stored), false, false, true) == 0);
  CHECK(! _tcscmp(stored, _T("0-2,4")));
  const TCHAR *invalid[] = { _T("0-64"), _T("3-1"), _T("0,"), _T("0,,1"), _T("4294967296"), _T("-1") };
  for (size_t i = 0; i < _countof(invalid); i++) {
    value.string = const_cast<TCHAR *>(invalid[i]);
    CHECK(setting_set_affinity(service, key, NSSM_REG_AFFINITY, NULL, &value, NULL) == -1);
    CHECK(get_string(key, NSSM_REG_AFFINITY, stored, sizeof(stored), false, false, true) == 0);
    CHECK(! _tcscmp(stored, _T("0-2,4")));
  }
  value_t result = {};
  CHECK(setting_get_affinity(service, key, NSSM_REG_AFFINITY, NULL, &result, NULL) == 1);
  REQUIRE(result.string);
  CHECK(! _tcscmp(result.string, _T("0-2,4")));
  HeapFree(GetProcessHeap(), 0, result.string);
  TCHAR empty[] = _T("");
  value.string = empty;
  CHECK(setting_set_affinity(service, key, NSSM_REG_AFFINITY, NULL, &value, NULL) == 0);
  result.string = NULL;
  CHECK(setting_get_affinity(service, key, NSSM_REG_AFFINITY, NULL, &result, NULL) == 0);
  REQUIRE(result.string);
  CHECK(! _tcscmp(result.string, _T("All")));
  HeapFree(GetProcessHeap(), 0, result.string);
  RegCloseKey(key);
}
