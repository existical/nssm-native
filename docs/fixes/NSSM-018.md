# NSSM 018 Administrator-check output

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 18, B08 |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `8f435660e7bbee17468f02a9ac01c3a038128d17` |
| Merge commit | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| First release | Not yet released |

`check_admin` cast the address of its one-byte C++ bool to PBOOL. Windows
expects BOOL storage for this output, so the write could extend beyond the
object. The function also ignored whether the API succeeded. The checked
legacy function matches the pre-fix continuation.

The function now initializes a local BOOL to FALSE, checks the API return,
and converts the output to bool only on success. SID allocation or membership
failure leaves `is_admin` false. A successfully allocated SID is freed once.
This follows [CheckTokenMembership's output and return contract](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-checktokenmembership).

Four initial checks failed: three attempts to pass the bool's address as BOOL
storage and a failed query incorrectly leaving the administrator result true.
The wrapper refused the oversized write; it did not corrupt adjacent memory.
Final tests cover member, nonmember, failed membership with a misleading true
output, and SID allocation failure. See [admin tests](../../tests/nssm_entry.cpp)
and [B08 validation](../reviews/b08-validation.md).

Successful membership checks retain existing behavior. This does not grant
administrator rights or change the existing elevation flow; failed checks
produce a defined non-administrator result.
