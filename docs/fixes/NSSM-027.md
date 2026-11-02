# NSSM 027 Bounded LSA service-right comparison

| Field | Value |
| --- | --- |
| Priority and review | P2; B11 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f28a43141a576033713190fe9b716fb503ac227a` |
| Fix PR | Pending creation |
| Implementation commit | `b91392150c23ecec4299c4eec75a7fcb65f867fb` |
| First release | Not yet released |

The service-logon-right check compared an LSA string using MaximumLength,
a byte capacity, as a wchar_t count. LSA results need not contain a terminator.
The legacy source has the same expression. A bounded comparison wrapper
reported the invalid requested count without executing the unsafe read; the
initial rights group also reported four mismatched SID deallocator calls.

The comparison validates aligned Length, Length within MaximumLength, a
non-null buffer for nonempty text, and equal byte lengths. It compares exactly
Length / sizeof(wchar_t) characters without requiring a terminator. Existing
case-insensitive matching remains. Unrelated/prefix/suffixed rights do not
match; malformed metadata fails instead of triggering a rights mutation.
An absent-rights response uses zero entries, and policy/SID/LSA-array cleanup
is scoped on success and failure. An ordinary missing right still follows the
existing add-right path and reports its failure if Windows rejects it.

`nssm.account_rights` uses production code with injected LSA enumeration and
addition APIs. Cases cover exact/case-varied, prefix/suffix/unrelated, odd and
inconsistent lengths, null and embedded-NUL strings, no rights, enumeration
denial and addition denial. An exact nonterminated right ends immediately
before a PAGE_NOACCESS page. The comparison wrapper asserts the permitted
count and bounds legacy red runs. No real rights were added or removed.
See [tests](../../tests/accounts.cpp) and Microsoft's
[LSA_UNICODE_STRING contract](https://learn.microsoft.com/en-us/windows/win32/api/lsalookup/ns-lsalookup-lsa_unicode_string).

See [B11 validation](../reviews/b11-validation.md) for the build matrix,
sanitizer results, tool versions and remaining coverage limits.
