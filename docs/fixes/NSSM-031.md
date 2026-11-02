# NSSM 031 Checked unsigned numeric parsing

| Field | Value |
| --- | --- |
| Priority and review | P2; follow-up F in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `a3c6260b23b1c229cb9c74a2b172655e41c30c55` |
| Evidence | Production conversion tests and isolated HKCU-backed settings/exit-action tests |
| Fix PR | [PR 8](https://github.com/existical/nssm-native/pull/8), branch `fix/b03-affinity-parsing` |
| Implementation commit | Pending merge |
| First release | Not yet released |

The old `str_number` accepted empty text as zero and ignored conversion
overflow. It accepted `-1` as 4294967295 and values above the DWORD maximum
as a saturated maximum. Even a rejected suffix overwrote the caller's output.
The same helper feeds numeric settings, AppExit identifiers and affinity
tokens.

[nssm.cpp](../../src/nssm.cpp) now requires digits, rejects a minus sign,
clears stale conversion error state and checks `ERANGE`. The two-argument
form requires complete consumption and only publishes the number on success.
The three-argument form preserves the affinity parser's valid-prefix contract:
0 means complete input, 2 means a valid number followed by a suffix, and 1
means invalid input or overflow. It leaves the numeric output unchanged on
invalid input; the end pointer remains available for delimiter handling.

Accepted standalone values are 0 through 4294967295. To preserve existing
valid input, base-zero syntax remains: decimal normally, octal after a leading
zero, and hexadecimal after `0x`. Leading whitespace and an optional plus
sign remain accepted. Empty/whitespace/sign-only input, negatives including
`-0`, overflow and trailing characters are rejected. For example, `010` is
8 and `08` is invalid; this PR does not silently reinterpret legacy octal as
decimal.

Caller audit: the generic DWORD setter and AppExit setter/getter require the
complete-input form. Only affinity parsing accepts the prefix form, then
validates its delimiters and 0-through-63 range. Numeric storage remains DWORD;
no setting-specific policy limit is added. Reset paths still bypass conversion.
AppExit key-name normalization/export remains separate B10 work: its setter
retains the supplied key spelling while retrieval uses the numeric code.
Use canonical decimal identifiers for that legacy interface.

`nssm.numeric` tests zero/maximum values in supported bases, stale `errno`,
overflow in each base, whitespace/sign handling, suffixes and pointer errors.
Registry tests verify rejection preserves the previous DWORD, maximum values
can be stored, reset still deletes the value, invalid AppExit IDs fail, and
the maximum decimal AppExit code can be retrieved. Tests use a redirected,
temporary HKCU registry, never a real service.
See [tests/parsing.cpp](../../tests/parsing.cpp),
[tests/dump_quoting.cpp](../../tests/dump_quoting.cpp) and
[B03 validation](../reviews/b03-validation.md).

The checked legacy helper is identical to the pre-fix continuation. Other
historic binaries were not tested. Valid stored registry values need no
migration; invalid command-line values that previously wrapped or saturated
now produce a failed setting operation. The
[Microsoft conversion contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strtoul-strtoul-l-wcstoul-wcstoul-l)
documents base selection, sign handling, no-conversion results and overflow.
