#include "test.h"
#include <sddl.h>
#include <lmcons.h>
#include <algorithm>

namespace account_test {
static bool active;
static int failure, allocation_failure = -1, allocations_seen;
static unsigned opens, closes, domain_frees, translation_frees, additions;
static unsigned lsa_buffers;
static std::vector<void *> allocations;
static LSA_TRUST_INFORMATION trust;
static LSA_REFERENCED_DOMAIN_LIST domains;
static LSA_TRANSLATED_SID translated_sid;
static LSA_TRANSLATED_NAME translated_name;
static LSA_UNICODE_STRING right;
static BYTE domain_sid[SECURITY_MAX_SID_SIZE];
static wchar_t domain[] = L"MACHINE", name[] = L"Example", right_text[] = L"SeServiceLogonRight!";
static int policy_token;
static ACCESS_MASK last_access;
static LSA_HANDLE policy = &policy_token;
static LPVOID WINAPI allocate(HANDLE h, DWORD f, SIZE_T n) {
  if (active && allocations_seen++ == allocation_failure) return NULL;
  void *p = HeapAlloc(h, f, n);
  if (active && p) allocations.push_back(p);
  return p;
}
static BOOL WINAPI release(HANDLE h, DWORD f, LPVOID p) {
  auto i = std::find(allocations.begin(), allocations.end(), p);
  if (i != allocations.end()) allocations.erase(i);
  return HeapFree(h, f, p);
}
static PVOID WINAPI free_sid(PSID p) {
  if (! active) return FreeSid(p);
  CHECK(false); // username_sid allocates with HeapAlloc, not AllocateAndInitializeSid.
  release(GetProcessHeap(), 0, p); return NULL;
}
static NTSTATUS NTAPI open(PLSA_UNICODE_STRING n, PLSA_OBJECT_ATTRIBUTES a, ACCESS_MASK m, PLSA_HANDLE p) {
  if (! active) return LsaOpenPolicy(n, a, m, p);
  last_access = m;
  *p = NULL;
  if (failure == 1) return (NTSTATUS) 0xc0000022L;
  opens++; *p = policy; return 0;
}
static NTSTATUS NTAPI close(LSA_HANDLE p) {
  if (! active) return LsaClose(p);
  CHECK(p == policy); closes++; return 0;
}
static NTSTATUS NTAPI free_memory(PVOID p) {
  if (! active) return LsaFreeMemory(p);
  if (! p) return 0;
  if (p == &domains) domain_frees++;
  else if (p == &translated_sid || p == &translated_name || p == &right) translation_frees++;
  else CHECK(false);
  REQUIRE(lsa_buffers); --lsa_buffers;
  return 0;
}
static NTSTATUS NTAPI lookup_names(LSA_HANDLE h, ULONG n, PLSA_UNICODE_STRING s, PLSA_REFERENCED_DOMAIN_LIST *d, PLSA_TRANSLATED_SID *t) {
  if (! active) return LsaLookupNames(h, n, s, d, t);
  CHECK(h == policy && n == 1);
  CHECK(s->Length % sizeof(wchar_t) == 0 && s->MaximumLength >= s->Length);
  *d = &domains; *t = &translated_sid;
  lsa_buffers += 2;
  if (failure == 8) { *d = NULL; *t = NULL; lsa_buffers -= 2; return (NTSTATUS) 0xc0000022L; }
  return failure == 2 ? (NTSTATUS) 0xc0000073L : 0;
}
static NTSTATUS NTAPI lookup_sids(LSA_HANDLE h, ULONG n, PSID *s, PLSA_REFERENCED_DOMAIN_LIST *d, PLSA_TRANSLATED_NAME *t) {
  if (! active) return LsaLookupSids(h, n, s, d, t);
  CHECK(h == policy && n == 1 && IsValidSid(*s));
  *d = &domains; *t = &translated_name;
  lsa_buffers += 2;
  return failure == 3 ? (NTSTATUS) 0xc0000073L : 0;
}
static NTSTATUS NTAPI enumerate(LSA_HANDLE h, PSID s, PLSA_UNICODE_STRING *r, PULONG n) {
  if (! active) return LsaEnumerateAccountRights(h, s, r, n);
  *r = NULL; *n = 0;
  if (failure == 4) return (NTSTATUS) 0xc0000034L;
  if (failure == 5) return (NTSTATUS) 0xc0000022L;
  *r = &right; *n = 1; lsa_buffers++; return 0;
}
static NTSTATUS NTAPI add(LSA_HANDLE h, PSID s, PLSA_UNICODE_STRING r, ULONG n) {
  if (! active) return LsaAddAccountRights(h, s, r, n);
  additions++; CHECK(n == 1); return failure == 6 ? (NTSTATUS) 0xc0000022L : 0;
}
static int convert(const wchar_t *s, TCHAR **p, unsigned long *n) {
  if (active && failure == 7) { *p = NULL; return 1; }
  int result = from_utf16(s, p, n);
  if (active && ! result) allocations.push_back(*p);
  return result;
}
static int to_wide(const TCHAR *s, wchar_t **p, unsigned long *n) {
  if (active && failure == 9) { *p = NULL; *n = 0; return 1; }
  int result = to_utf16(s, p, n);
  if (active && ! result) allocations.push_back(*p);
  return result;
}
static int compare(const wchar_t *a, const wchar_t *b, size_t n) {
  if (active && a == right.Buffer) {
    CHECK(n <= right.Length / sizeof(wchar_t)); // Bound the unsafe legacy comparison during red tests.
    n = (std::min)(n, right.Length / sizeof(wchar_t));
  }
  return _wcsnicmp(a, b, n);
}
static BOOL WINAPI initialize(PSID p, PSID_IDENTIFIER_AUTHORITY a, BYTE n) {
  if (active && failure == 10) { SetLastError(ERROR_INVALID_SID); return FALSE; }
  return InitializeSid(p, a, n);
}
static BOOL WINAPI computer(LPTSTR p, LPDWORD n) {
  if (active && failure == 11) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  return GetComputerName(p, n);
}
static void reset(int scenario = 0) {
  REQUIRE(allocations.empty()); active = true; failure = scenario;
  allocation_failure = -1; allocations_seen = 0;
  opens = closes = domain_frees = translation_frees = additions = lsa_buffers = 0;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  REQUIRE(InitializeSid(domain_sid, &authority, 1)); *GetSidSubAuthority(domain_sid, 0) = 21;
  trust.Name.Buffer = domain; trust.Name.Length = 7 * sizeof(wchar_t); trust.Name.MaximumLength = sizeof(domain); trust.Sid = domain_sid;
  domains.Entries = 1; domains.Domains = &trust;
  translated_sid.Use = SidTypeUser; translated_sid.DomainIndex = 0; translated_sid.RelativeId = 123;
  translated_name.Use = SidTypeUser; translated_name.DomainIndex = 0;
  translated_name.Name.Buffer = name; translated_name.Name.Length = 7 * sizeof(wchar_t); translated_name.Name.MaximumLength = sizeof(name);
  right.Buffer = right_text; right.Length = 19 * sizeof(wchar_t); right.MaximumLength = right.Length;
}
static void finish() {
  CHECK(opens == closes); CHECK(allocations.empty()); CHECK(lsa_buffers == 0);
  for (void *p : allocations) HeapFree(GetProcessHeap(), 0, p);
  allocations.clear(); active = false;
}
}
#define HeapAlloc account_test::allocate
#define HeapFree account_test::release
#define FreeSid account_test::free_sid
#define LsaOpenPolicy account_test::open
#define LsaClose account_test::close
#define LsaFreeMemory account_test::free_memory
#define LsaLookupNames account_test::lookup_names
#define LsaLookupSids account_test::lookup_sids
#define LsaEnumerateAccountRights account_test::enumerate
#define LsaAddAccountRights account_test::add
#define from_utf16 account_test::convert
#define to_utf16 account_test::to_wide
#define _wcsnicmp account_test::compare
#define InitializeSid account_test::initialize
#undef GetComputerName
#define GetComputerName account_test::computer
#include "../src/account.cpp"
#undef HeapAlloc
#undef HeapFree
#undef FreeSid
#undef LsaOpenPolicy
#undef LsaClose
#undef LsaFreeMemory
#undef LsaLookupNames
#undef LsaLookupSids
#undef LsaEnumerateAccountRights
#undef LsaAddAccountRights
#undef from_utf16
#undef to_utf16
#undef _wcsnicmp
#undef InitializeSid
#undef GetComputerName
#define GetComputerName GetComputerNameW

void test_account_lookup() {
  using namespace account_test;
  for (int scenario : {0, 1, 2, 3, 7, 8, 9, 10, 11}) {
    reset(scenario); TCHAR *output = reinterpret_cast<TCHAR *>(1);
    int result = canonicalise_username(scenario == 11 ? _T(".\\Example") : _T("MACHINE\\Example"), &output);
    CHECK(last_access == POLICY_LOOKUP_NAMES);
    CHECK((result == 0) == (scenario == 0));
    if (! result) { CHECK(! _tcscmp(output, _T("MACHINE\\Example"))); release(GetProcessHeap(), 0, output); }
    else CHECK(! output);
    finish();
  }
  for (int fail = 0; fail < 4; fail++) {
    reset(); allocation_failure = fail; TCHAR *output = NULL;
    int result = canonicalise_username(_T(".\\Example"), &output);
    if (! result) release(GetProcessHeap(), 0, output); else CHECK(! output);
    finish();
  }
  for (int malformed = 0; malformed < 8; malformed++) {
    reset();
    switch (malformed) {
      case 0: translated_sid.DomainIndex = -1; break;
      case 1: translated_sid.DomainIndex = 1; break;
      case 2: translated_name.DomainIndex = -1; break;
      case 3: translated_name.DomainIndex = 1; break;
      case 4: translated_name.Name.Length--; break;
      case 5: translated_name.Name.MaximumLength = 0; break;
      case 6: translated_name.Name.Buffer = NULL; break;
      case 7: trust.Sid = NULL; break;
    }
    TCHAR *output = reinterpret_cast<TCHAR *>(1);
    CHECK(canonicalise_username(_T("MACHINE\\Example"), &output) != 0); CHECK(! output); finish();
  }
  // Both components exceed a combined USHORT byte count, but fit size_t.
  reset(); std::wstring large(20000, L'x');
  trust.Name.Buffer = &large[0]; trust.Name.Length = trust.Name.MaximumLength = 40000;
  translated_name.Name = trust.Name;
  TCHAR *output = NULL; REQUIRE(canonicalise_username(_T("Example"), &output) == 0);
  CHECK(_tcslen(output) == 40001); release(GetProcessHeap(), 0, output); finish();
  reset(); SID *sid = NULL; LSA_HANDLE caller_policy = policy;
  CHECK(username_sid(_T("Example"), &sid, &caller_policy) == 0);
  CHECK(opens == 0 && closes == 0); release(GetProcessHeap(), 0, sid); finish();
  reset(); CHECK(username_equiv(_T("Example"), _T("Example")) == 1); finish();
  reset(); translated_sid.Use = SidTypeWellKnownGroup;
  SID *invalid = reinterpret_cast<SID *>(1);
  CHECK(username_sid(_T("Example"), &invalid) != 0); CHECK(! invalid); finish();
  // Real read-only lookup of the current account; never change account rights.
  TCHAR current[UNLEN + 1]; DWORD chars = _countof(current);
  REQUIRE(GetUserName(current, &chars));
  output = NULL; REQUIRE(canonicalise_username(current, &output) == 0);
  HeapFree(GetProcessHeap(), 0, output); // Warm up Windows' LSA/RPC lookup cache.
  DWORD before = test_handle_count();
  for (unsigned i = 0; i < 30; i++) {
    output = NULL; REQUIRE(canonicalise_username(current, &output) == 0);
    CHECK(output && _tcschr(output, _T('\\'))); HeapFree(GetProcessHeap(), 0, output);
  }
  CHECK(test_handle_count() <= before + 2);
}
void test_account_rights() {
  using namespace account_test;
  for (int scenario : {0, 4, 5, 6}) {
    reset(scenario);
    if (scenario == 6) right.Length -= sizeof(wchar_t);
    CHECK((grant_logon_as_service(_T("MACHINE\\Example")) == 0) == (scenario == 0 || scenario == 4));
    CHECK(last_access == POLICY_ALL_ACCESS);
    CHECK(additions == (scenario == 4 || scenario == 6 ? 1U : 0U)); finish();
  }
  for (int variant = 0; variant < 8; variant++) {
    reset(); std::wstring value = L"SeServiceLogonRight";
    if (variant == 0) value = L"sEsERVICELOGONrIGHT";
    if (variant == 1) value += L"Extra";
    if (variant == 2) value = L"SeServiceLogonRigh";
    if (variant == 3) value = L"SeInteractiveLogonRight";
    right.Buffer = &value[0]; right.Length = right.MaximumLength = (USHORT) (value.size() * sizeof(wchar_t));
    if (variant == 4) right.Length--;
    if (variant == 5) right.MaximumLength = 0;
    if (variant == 6) right.Buffer = NULL;
    if (variant == 7) value[3] = 0;
    int result = grant_logon_as_service(_T("Example"));
    CHECK((result != 0) == (variant >= 4 && variant <= 6));
    CHECK(additions == (variant >= 1 && variant <= 3 || variant == 7 ? 1U : 0U)); finish();
  }
  // Exact counted string at a page boundary: there is no accessible terminator.
  SYSTEM_INFO info; GetSystemInfo(&info);
  BYTE *pages = static_cast<BYTE *>(VirtualAlloc(NULL, info.dwPageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); REQUIRE(pages);
  DWORD old; REQUIRE(VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old));
  reset(); right.Buffer = reinterpret_cast<wchar_t *>(pages + info.dwPageSize - right.Length);
  memcpy(right.Buffer, NSSM_LOGON_AS_SERVICE_RIGHT, right.Length);
  CHECK(grant_logon_as_service(_T("Example")) == 0); CHECK(! additions); finish();
  CHECK(VirtualFree(pages, 0, MEM_RELEASE));
}
