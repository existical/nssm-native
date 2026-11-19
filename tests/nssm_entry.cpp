// The renamed main loses C++'s implicit return. Its final call exits the process.
__declspec(noreturn) void nssm_exit(int);
#include "test.h"
extern bool is_admin;
namespace admin_test {
static bool active, sid_ok, query_ok;
static BOOL member;
static unsigned queries, frees;
static int sid_token;
static BOOL WINAPI sid(PSID_IDENTIFIER_AUTHORITY authority, BYTE count, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g, DWORD h, PSID *out) {
  if (! active) return AllocateAndInitializeSid(authority, count, a, b, c, d, e, f, g, h, out);
  if (sid_ok) *out = &sid_token;
  return sid_ok;
}
static PVOID WINAPI free_sid(PSID value) {
  if (! active) return FreeSid(value);
  CHECK(value == &sid_token); frees++; return NULL;
}
static BOOL WINAPI membership(HANDLE token, PSID sid, PBOOL out) {
  if (! active) return CheckTokenMembership(token, sid, out);
  queries++;
  // Reject the legacy four-byte write into a one-byte bool without corrupting memory.
  CHECK(out != reinterpret_cast<PBOOL>(&is_admin));
  if (out == reinterpret_cast<PBOOL>(&is_admin)) is_admin = member != FALSE;
  else *out = member;
  return query_ok;
}
}
namespace elevation_test {
static unsigned scenario;
static TCHAR command[] = _T("\"C:\\Suite Path\\nssm.exe\" edit \"My service\"");
static BOOL WINAPI launch(SHELLEXECUTEINFO *info) {
  CHECK(! _tcscmp(info->lpFile, _T("C:\\Suite Path\\nssm.exe")));
  CHECK(! _tcscmp(info->lpParameters, _T("edit \"My service\"")));
  CHECK(info->fMask & SEE_MASK_NOCLOSEPROCESS);
  if (scenario == 1) return FALSE;
  if (scenario == 2) return TRUE;
  TCHAR exe[MAX_PATH]; REQUIRE(GetModuleFileName(NULL, exe, _countof(exe)));
  std::wstring args = L"\"" + std::wstring(exe) + L"\" --elevation-child";
  STARTUPINFO si = {}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  REQUIRE(CreateProcess(exe, &args[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
  CloseHandle(pi.hThread); info->hProcess = pi.hProcess; return TRUE;
}
static LPTSTR WINAPI command_line() { return command; }
}
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
#define AllocateAndInitializeSid admin_test::sid
#define FreeSid admin_test::free_sid
#define CheckTokenMembership admin_test::membership
#undef ShellExecuteEx
#define ShellExecuteEx elevation_test::launch
#undef GetCommandLine
#define GetCommandLine elevation_test::command_line
#include "../src/nssm.cpp"
#undef GetCommandLine
#undef ShellExecuteEx
#undef GetProcessAffinityMask
#undef AllocateAndInitializeSid
#undef FreeSid
#undef CheckTokenMembership

void test_admin_check() {
  using namespace admin_test;
  active = true;
  for (unsigned scenario = 0; scenario < 4; scenario++) {
    sid_ok = scenario != 3; query_ok = scenario != 2;
    member = scenario == 1 ? FALSE : TRUE; queries = frees = 0; is_admin = true;
    check_admin();
    CHECK(is_admin == (scenario == 0));
    CHECK(queries == (sid_ok ? 1U : 0U) && frees == queries);
  }
  active = false;
  _tcscpy_s(unquoted_imagepath, _T("C:\\Suite Path\\nssm.exe"));
  for (elevation_test::scenario = 0; elevation_test::scenario < 3; elevation_test::scenario++)
    CHECK(elevate(NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT) == (elevation_test::scenario ? 100 : 43));
}

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
