#include "token_fixture.h"
namespace token_test {
bool active;
static bool impersonated, preexisting, impersonate_ok, lookup_ok, adjust_ok, restore_ok, revert_ok, closed;
static DWORD initial_error, retry_error, adjust_error, attributes, original;
static unsigned opens, adjustments, restores, reversions, closes;
static int token_marker;
static HANDLE token = &token_marker;
struct fatal_exit {};
static void reset() {
  active = true; impersonated = preexisting = closed = false;
  impersonate_ok = lookup_ok = adjust_ok = restore_ok = revert_ok = true;
  initial_error = ERROR_NO_TOKEN; retry_error = adjust_error = attributes = original = 0;
  opens = adjustments = restores = reversions = closes = 0;
}
BOOL WINAPI open(HANDLE h, DWORD access, BOOL self, PHANDLE result) {
  if (! active) return OpenThreadToken(h, access, self, result);
  DWORD error = opens++ ? retry_error : initial_error;
  *result = error ? NULL : token;
  SetLastError(error); return error == ERROR_SUCCESS;
}
BOOL WINAPI impersonate(SECURITY_IMPERSONATION_LEVEL level) {
  if (! active) return ImpersonateSelf(level);
  CHECK(! preexisting); if (impersonate_ok) impersonated = true;
  SetLastError(ERROR_ACCESS_DENIED); return impersonate_ok;
}
BOOL WINAPI lookup(LPCTSTR system, LPCTSTR name, PLUID luid) {
  if (! active) return LookupPrivilegeValue(system, name, luid);
  luid->LowPart = 20; luid->HighPart = 0; SetLastError(ERROR_NO_SUCH_PRIVILEGE); return lookup_ok;
}
BOOL WINAPI adjust(HANDLE h, BOOL all, PTOKEN_PRIVILEGES requested, DWORD size, PTOKEN_PRIVILEGES previous, PDWORD needed) {
  if (! active) return AdjustTokenPrivileges(h, all, requested, size, previous, needed);
  CHECK(h == token && ! closed);
  if (previous) {
    adjustments++; ZeroMemory(previous, sizeof(*previous));
    if (needed) *needed = sizeof(*previous);
    if (! adjust_ok) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (! adjust_error && attributes != requested->Privileges[0].Attributes) {
      previous->PrivilegeCount = 1; previous->Privileges[0].Luid = requested->Privileges[0].Luid;
      previous->Privileges[0].Attributes = attributes; attributes = requested->Privileges[0].Attributes;
    }
    SetLastError(adjust_error); return TRUE;
  }
  restores++;
  if (! restore_ok) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  attributes = requested->Privileges[0].Attributes; SetLastError(ERROR_SUCCESS); return TRUE;
}
BOOL WINAPI revert() {
  if (! active) return RevertToSelf();
  CHECK(impersonated && ! preexisting); reversions++;
  if (revert_ok) impersonated = false;
  SetLastError(ERROR_ACCESS_DENIED); return revert_ok;
}
BOOL WINAPI close(HANDLE h) {
  if (! active) return CloseHandle(h);
  CHECK(h == token && ! closed); closed = true; closes++; return TRUE;
}
void exit(int code) { if (! active) nssm_exit(code); CHECK(code == 111); throw fatal_exit(); }
}
void test_debug_token() {
  using namespace token_test;
  for (unsigned scenario = 0; scenario < 9; scenario++) {
    reset(); debug_token_t scope;
    if (scenario == 1 || scenario == 2) { initial_error = 0; preexisting = true; }
    if (scenario == 2) attributes = original = SE_PRIVILEGE_ENABLED;
    if (scenario == 3) initial_error = ERROR_ACCESS_DENIED;
    if (scenario == 4) impersonate_ok = false;
    if (scenario == 5) retry_error = ERROR_ACCESS_DENIED;
    if (scenario == 6) lookup_ok = false;
    if (scenario == 7) adjust_ok = false;
    if (scenario == 8) adjust_error = ERROR_NOT_ALL_ASSIGNED;
    CHECK(get_debug_token(&scope) == (scenario < 3));
    if (scenario < 3) CHECK(scope.token == token && attributes == SE_PRIVILEGE_ENABLED);
    else CHECK(! scope.token && GetLastError() != ERROR_SUCCESS);
    if (scenario == 3 || scenario == 4) CHECK(opens == 1 && ! closes);
    release_debug_token(&scope);
    CHECK(! scope.token && ! impersonated && attributes == original);
    CHECK(reversions == (scenario == 0 || scenario >= 5 ? 1U : 0U));
    CHECK(closes == (scenario < 3 || scenario >= 6 ? 1U : 0U));
    CHECK(restores == (scenario < 2 ? 1U : 0U));
    release_debug_token(&scope); // Empty scopes are harmless.
  }
  for (unsigned scenario = 0; scenario < 2; scenario++) {
    reset(); debug_token_t scope;
    REQUIRE(get_debug_token(&scope));
    if (scenario == 0) restore_ok = false;
    else revert_ok = false;
    bool fatal = false;
    try { release_debug_token(&scope); } catch (const fatal_exit &) { fatal = true; }
    CHECK(fatal && closes == 1 && ! scope.token);
  }
  active = false;
}
