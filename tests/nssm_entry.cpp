// The renamed main loses C++'s implicit return. Its final call exits the process.
__declspec(noreturn) void nssm_exit(int);
#include "nssm.h"
// Retain the production utility functions without running the service CLI.
#undef _tmain
#define _tmain unused_nssm_main
#include "../src/nssm.cpp"
