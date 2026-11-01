#include "test.h"
// Include the implementation to exercise both set and private unset paths.
#include "../src/env.cpp"

static void expect_variable(const TCHAR *name, const TCHAR *expected) {
  TCHAR value[256] = {};
  SetLastError(ERROR_SUCCESS);
  DWORD length = GetEnvironmentVariable(name, value, _countof(value));
  DWORD error = GetLastError();
  if (! expected) CHECK(! length && error == ERROR_ENVVAR_NOT_FOUND);
  else {
    CHECK(error != ERROR_ENVVAR_NOT_FOUND);
    CHECK(length == _tcslen(expected));
    CHECK(! _tcscmp(value, expected));
  }
}

void test_environment_entries() {
  const TCHAR *names[] = { _T("NSSM_TEST_A"), _T("NSSM_TEST_B"), _T("NSSM_TEST_C"), _T("NSSM_TEST_D") };
  // Exercise all empty/nonempty combinations, covering first, middle, last,
  // consecutive and all-empty entries, in both set and unset mode.
  for (unsigned mask = 0; mask < 16; mask++) {
    for (int mode = 0; mode < 2; mode++) {
      std::vector<TCHAR> block;
      for (unsigned i = 0; i < _countof(names); i++) {
        REQUIRE(SetEnvironmentVariable(names[i], _T("old")));
        std::basic_string<TCHAR> entry = names[i];
        entry += _T("=");
        if (! (mask & (1U << i))) entry += _T("value=with=equals");
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(0);
      }
      block.push_back(0);
      // A sentinel past the double NUL detects walking past a trailing empty
      // value without relying on whether out-of-bounds memory happens to be 0.
      const TCHAR sentinel[] = _T("NSSM_TEST_SENTINEL=changed\0");
      block.insert(block.end(), sentinel, sentinel + _countof(sentinel));
      REQUIRE(SetEnvironmentVariable(_T("NSSM_TEST_SENTINEL"), _T("untouched")));
      CHECK((mode ? set_environment_block(block.data()) : unset_environment_block(block.data())) == 0);
      for (unsigned i = 0; i < _countof(names); i++) {
        expect_variable(names[i], ! mode ? NULL : (mask & (1U << i)) ? _T("") : _T("value=with=equals"));
      }
      expect_variable(_T("NSSM_TEST_SENTINEL"), _T("untouched"));
    }
  }
  TCHAR empty[] = { 0, 0 };
  CHECK(set_environment_block(empty) == 0);
  CHECK(unset_environment_block(empty) == 0);
  TCHAR expansion[] = _T("NSSM_TEST_A=\0NSSM_TEST_B=expanded\0NSSM_TEST_C=%NSSM_TEST_B% suffix\0");
  CHECK(set_environment_block(expansion) == 0);
  expect_variable(_T("NSSM_TEST_C"), _T("expanded suffix"));
  // Ignore a malformed entry and continue to the next valid assignment.
  TCHAR malformed[] = _T("MISSING_EQUALS\0NSSM_TEST_D=valid\0");
  CHECK(set_environment_block(malformed) == 0);
  expect_variable(_T("NSSM_TEST_D"), _T("valid"));
}
