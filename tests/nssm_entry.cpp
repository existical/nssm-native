// The renamed main loses C++'s implicit return. Its final call exits the process.
__declspec(noreturn) void nssm_exit(int);
#include "test.h"
static bool fake_cpu_query = false;
static bool cpu_query_success = true;
static DWORD_PTR fake_system_mask = 0;
static BOOL WINAPI test_cpu_query(HANDLE process, PDWORD_PTR affinity, PDWORD_PTR system) {
  if (! fake_cpu_query) return GetProcessAffinityMask(process, affinity, system);
  // Deliberately populate outputs on failure: callers must discard them.
  *affinity = *system = fake_system_mask;
  return cpu_query_success;
}
// Retain the production utility functions without running the service CLI.
#undef _tmain
#define _tmain unused_nssm_main
#define GetProcessAffinityMask test_cpu_query
#include "../src/nssm.cpp"
#undef GetProcessAffinityMask

void test_cpu_detection() {
  fake_cpu_query = true;
  cpu_query_success = true;
  fake_system_mask = 0;
  CHECK(cpu_affinity_mask() == 0);
  CHECK(num_cpus() == 0);
  fake_system_mask = 5;
  CHECK(cpu_affinity_mask() == 5);
  CHECK(num_cpus() == 2);
  fake_system_mask = 10;
  CHECK(num_cpus() == 2);
  fake_system_mask = (DWORD_PTR) 1 << (sizeof(DWORD_PTR) * 8 - 1);
  CHECK(num_cpus() == 1);
  fake_system_mask = ~(DWORD_PTR) 0;
  CHECK(num_cpus() == sizeof(DWORD_PTR) * 8);
  cpu_query_success = false;
  CHECK(cpu_affinity_mask() == 0);
  CHECK(num_cpus() == 0);
  fake_cpu_query = false;
}
