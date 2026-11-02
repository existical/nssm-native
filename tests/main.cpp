#include "test.h"

int failures = 0;

int wmain(int argc, TCHAR **argv) {
  if (argc >= 2 && ! _tcscmp(argv[1], _T("--tree-child"))) return process_tree_child(argc, argv);
  if (argc >= 2 && ! _tcscmp(argv[1], _T("--quote-child"))) return quoting_child(argc, argv);
  if (argc >= 2 && ! _tcscmp(argv[1], _T("--logger-child"))) return logger_child(argc, argv);
  if (argc == 3 && ! _tcscmp(argv[1], _T("--hook-child"))) {
    return _tcscmp(argv[2], _T("final quoted argument")) ? 1 : 0;
  }
  try {
    REQUIRE(argc == 2);
    if (! _tcscmp(argv[1], _T("environment"))) test_environment_entries();
    else if (! _tcscmp(argv[1], _T("admin"))) test_admin_check();
    else if (! _tcscmp(argv[1], _T("waits"))) test_handle_waits();
    else if (! _tcscmp(argv[1], _T("install_ownership"))) test_install_ownership();
    else if (! _tcscmp(argv[1], _T("debug_token"))) test_debug_token();
    else if (! _tcscmp(argv[1], _T("error_messages"))) test_error_messages();
    else if (! _tcscmp(argv[1], _T("setting_ownership"))) test_setting_ownership();
    else if (! _tcscmp(argv[1], _T("hooks"))) test_hook_commands();
    else if (! _tcscmp(argv[1], _T("startup"))) test_startup();
    else if (! _tcscmp(argv[1], _T("registry"))) test_registry_bounds();
    else if (! _tcscmp(argv[1], _T("multisz"))) test_multisz_bounds();
    else if (! _tcscmp(argv[1], _T("quoting"))) test_quoting();
    else if (! _tcscmp(argv[1], _T("numeric"))) test_numeric();
    else if (! _tcscmp(argv[1], _T("affinity"))) { test_affinity(); test_cpu_detection(); }
    else if (! _tcscmp(argv[1], _T("hook_lifecycle"))) test_hook_lifecycle();
    else if (! _tcscmp(argv[1], _T("launch"))) test_launch_handles();
    else if (! _tcscmp(argv[1], _T("hook_live"))) test_hook_live_handles();
    else if (! _tcscmp(argv[1], _T("logger"))) test_logger_ownership();
    else if (! _tcscmp(argv[1], _T("rotation"))) test_offline_rotation();
    else if (! _tcscmp(argv[1], _T("process_identity"))) test_process_identity();
    else if (! _tcscmp(argv[1], _T("process_live"))) test_process_live();
    else throw std::runtime_error("Unknown test group");
  }
  catch (const std::exception &error) {
    fprintf(stderr, "Test fixture failed: %s\n", error.what());
    failures++;
  }
  printf("%d failed checks\n", failures);
  return failures ? 1 : 0;
}
