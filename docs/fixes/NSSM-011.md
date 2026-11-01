# NSSM 011 Bounded MULTI SZ formatting

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 11 in the 2026 November 2 review |
| State | Fixed, unreleased; merged in PR 5 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | Production code at `05205d7784dbd0e90dc6d26957d3133f7a1c62a9` with the new regression harness |
| Evidence | Nonzero sentinel reproduction, protected-page coverage and legacy source inspection |
| Fix PR | [PR 5](https://github.com/existical/nssm-native/pull/5) |
| Implementation commit | [1ecfc6be59e80fe78ea94553634d02a6ed9524d0](https://github.com/existical/nssm-native/commit/1ecfc6be59e80fe78ea94553634d02a6ed9524d0) |
| Merge commit | [bdeca1cf21314b8fc88b1927333aa69197a8571b](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b) |
| First release | Not yet released |

`format_double_null` inspected the character after the final valid index.
A five-character `A=x/NUL/NUL` block followed by a nonzero sentinel produced
an incorrect six-character result with a spurious trailing CRLF. An allocation
at an inaccessible page boundary could instead fault.

The formatter now finds the end marker within the declared character count,
calculates the CRLF result with checked arithmetic, bounds every output write,
and preserves two final NULs. Its returned character count includes both
terminators. An absent null/zero-length block remains absent; an empty
two-NUL block formats to an empty two-NUL block. Invalid input returns failure
with null output and zero length.
Source: [registry.cpp](../../src/registry.cpp), `format_double_null`.

`nssm.multisz` in [tests/registry.cpp](../../tests/registry.cpp) checks the
sentinel reproduction, empty/single/multiple entries, CRLF format/unformat
round trips, allocation failure and valid/malformed blocks ending exactly
at a protected memory page. See [B01 validation](../reviews/b01-validation.md).

Valid environment and dependency text keeps its formatting. No registry
conversion is needed. The separate legacy `unformat_double_null` implementation
is unchanged; this fix does not claim a complete audit of that reverse path.
