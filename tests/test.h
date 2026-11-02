#ifndef NSSM_TEST_H
#define NSSM_TEST_H
#include "nssm.h"
#include <stdexcept>
#include <string>
#include <vector>

extern int failures;
inline void check(bool passed, const char *expression, int line) {
  if (! passed) {
    fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    failures++;
  }
}
#define CHECK(expression) check(!!(expression), #expression, __LINE__)
#define REQUIRE(expression) do { if (! (expression)) throw std::runtime_error(#expression); } while (0)

inline DWORD test_handle_count() {
  // Test-only lookup: keep the production legacy Windows target unchanged.
  typedef BOOL (WINAPI *get_count_t)(HANDLE, PDWORD);
  get_count_t get_count = reinterpret_cast<get_count_t>(GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetProcessHandleCount"));
  REQUIRE(get_count);
  DWORD count;
  REQUIRE(get_count(GetCurrentProcess(), &count));
  return count;
}

// HKLM redirection affects this test process only. All registry writes land in
// a newly created HKCU key and are removed when the fixture exits.
class registry_fixture {
  TCHAR path[128];
  HKEY key;
public:
  registry_fixture() : key(NULL) {
    _stprintf_s(path, _T("Software\\nssm-native-tests-%lu-%lu"), GetCurrentProcessId(), GetTickCount());
    DWORD disposition;
    REQUIRE(RegCreateKeyEx(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE,
      KEY_ALL_ACCESS, NULL, &key, &disposition) == ERROR_SUCCESS);
    if (disposition != REG_CREATED_NEW_KEY) {
      RegCloseKey(key);
      throw std::runtime_error("Registry fixture already exists");
    }
    if (RegOverridePredefKey(HKEY_LOCAL_MACHINE, key) != ERROR_SUCCESS) {
      RegCloseKey(key);
      SHDeleteKey(HKEY_CURRENT_USER, path);
      throw std::runtime_error("Could not isolate test registry");
    }
  }
  ~registry_fixture() {
    CHECK(RegOverridePredefKey(HKEY_LOCAL_MACHINE, NULL) == ERROR_SUCCESS);
    RegCloseKey(key);
    CHECK(SHDeleteKey(HKEY_CURRENT_USER, path) == ERROR_SUCCESS);
  }
private:
  registry_fixture(const registry_fixture &);
  registry_fixture &operator=(const registry_fixture &);
};

void test_environment_entries();
void test_admin_check();
void test_handle_waits();
void test_install_ownership();
void test_debug_token();
void test_error_messages();
void test_setting_ownership();
void test_hook_commands();
void test_startup();
void test_registry_bounds();
void test_multisz_bounds();
void test_quoting();
int quoting_child(int, TCHAR **);
void test_numeric();
void test_affinity();
void test_cpu_detection();
void test_hook_lifecycle();
void test_launch_handles();
void test_hook_live_handles();
void test_logger_ownership();
void test_stdin();
void test_log_bytes();
void test_event_insertions();
void test_exit_export();
void test_exit_fallback();
void test_export_access();
void test_dependencies();
void test_account_lookup();
void test_account_rights();
void test_gui_retry();
void test_gui_password();
void test_gui_hooks();
void test_pipe_events();
void test_offline_rotation();
void test_process_identity();
void test_process_live();
int process_tree_child(int, TCHAR **);
int logger_child(int, TCHAR **);
#endif
