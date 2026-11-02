# NSSM 028 Scoped debug-token state

| Field | Value |
| --- | --- |
| Priority and review | P2; follow-up C, B08 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `ce6581da064e5c6006f81162e93b31e6604ca998` |
| First release | Not yet released |

`get_debug_token` used an uninitialized handle after some failures, ignored
ImpersonateSelf and retry-open results, and treated a successful BOOL from
AdjustTokenPrivileges as proof that the requested privilege was assigned.
It also left self-impersonation and changed privileges in place after the
`processes` command closed the returned handle. The checked legacy function
matches the pre-fix continuation.

An explicit `debug_token_t` scope now owns the token, saved privilege state
and whether this call established impersonation. Acquisition checks each API,
enables SeDebugPrivilege once while saving its previous state, and checks
GetLastError for incomplete assignment. Cleanup restores any changed privilege,
reverts only impersonation established by this scope, then closes its token.
Acquisition failures unwind the same state and preserve the original error.

The `processes` command releases the scope on its calling thread after the
walk. If debug privilege cannot be acquired, it still attempts inspection with
the caller's available access. A failure to restore privilege/impersonation
state prints a diagnostic and exits with code 111 instead of continuing in an
unverified context. Pre-existing caller impersonation is not reverted.
The contracts are documented by Microsoft for
[AdjustTokenPrivileges](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges)
and [RevertToSelf](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-reverttoself).

Three initial checks failed for incomplete assignment, leaked impersonation
and an ignored impersonation failure. Final tests simulate no/existing token,
already-enabled privilege, open/retry/lookup/adjustment failures, incomplete
assignment, balanced restoration, repeated empty cleanup and fatal restoration
failures. All token and impersonation operations in these cases are fake;
tests do not elevate the runner or impersonate another user. Fatal exits are
intercepted. See [token tests](../../tests/token.cpp) and
[B08 validation](../reviews/b08-validation.md).

This is a same-thread scope for the command's existing privilege request,
not a new privilege grant or a general security-context framework.
