#include "test.h"

int failures = 0;
extern unsigned long tls_index;

int wmain(int argc, TCHAR **argv) {
  if (argc >= 2 && ! _tcscmp(argv[1], _T("--quote-child"))) return quoting_child(argc, argv);
  if (argc >= 2 && ! _tcscmp(argv[1], _T("--logger-child"))) return logger_child(argc, argv);
  if (argc == 3 && ! _tcscmp(argv[1], _T("--hook-child"))) {
    return _tcscmp(argv[2], _T("final quoted argument")) ? 1 : 0;
  }
  tls_index = TlsAlloc();
  if (tls_index == TLS_OUT_OF_INDEXES) return 2;
  try {
    REQUIRE(argc == 2);
    if (! _tcscmp(argv[1], _T("environment"))) test_environment_entries();
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
    else throw std::runtime_error("Unknown test group");
  }
  catch (const std::exception &error) {
    fprintf(stderr, "Test fixture failed: %s\n", error.what());
    failures++;
  }
  LocalFree(TlsGetValue(tls_index));
  TlsFree(tls_index);
  printf("%d failed checks\n", failures);
  return failures ? 1 : 0;
}
