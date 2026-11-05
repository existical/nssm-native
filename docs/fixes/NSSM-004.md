# NSSM 004 Affinity parser bounds

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 4 in the 2026 November 2 review |
| State | Released; merged in PR 8 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `a3c6260b23b1c229cb9c74a2b172655e41c30c55` |
| Evidence | Invalid endpoints/reversed ranges reproduced; array overflow source-traced; post-fix boundary and registry tests |
| Fix PR | [PR 8](https://github.com/existical/nssm-native/pull/8), branch `fix/b03-affinity-parsing` |
| Implementation commit | [5ffd1ce84e177074e29881dd57c374f155a376d2](https://github.com/existical/nssm-native/commit/5ffd1ce84e177074e29881dd57c374f155a376d2) |
| Merge commit | [3982016dcd89358feafb156d19a91e4f7598c1f2](https://github.com/existical/nssm-native/commit/3982016dcd89358feafb156d19a91e4f7598c1f2) |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

The old `affinity_string_to_mask` in [service.cpp](../../src/service.cpp)
accepted `0-64` and did not validate the upper endpoint before shifting.
Reversed ranges such as `3-1` could succeed with an empty mask. A fixed
64-element temporary array had no bound on the number of comma-separated
tokens, so even repeated valid IDs could exceed its stack storage.

The parser now accumulates directly into an unsigned 64-bit mask. Both range
endpoints must be 0 through 63, the end must be at least the start, and a
valid token must precede each comma. Checked [numeric parsing](NSSM-031.md)
rejects overflow, negatives and missing digits. Each range requires at most
64 shifts, all within 0 through 63. The result is published only after the
complete input succeeds; failure leaves the mask zero.

Duplicate/overlapping entries remain idempotent. Empty text is rejected by
the parser, but CLI empty/default/`All` resets still bypass parsing. A null
internal input retains its absent-mask meaning. The portable configuration
format stays 64-bit on x86 and x64; hardware/group availability is separate,
as described in [NSSM-029](NSSM-029.md).

The companion formatter uses unsigned shifts, correct format argument types
and checks the allocated pointer on failure. The malformed-affinity registry
warning now supplies its required null sentinel. This last change repairs
one NSSM-022 call site; the broader logging task was subsequently completed in
[NSSM-022](NSSM-022.md) and is included in the same beta.

`nssm.affinity` covers every pair of endpoints from 0 through 64 (4,225
combinations), 64/65-entry lists, one through 130 repeated IDs, trailing commas,
empty tokens, huge endpoints, negatives and mask/string round trips including
bit 63 and all bits. Injected formatter allocation failure returns a null
output. Production setter tests verify invalid input does not replace the
stored configuration. See [tests/parsing.cpp](../../tests/parsing.cpp),
[tests/startup.cpp](../../tests/startup.cpp),
[tests/dump_quoting.cpp](../../tests/dump_quoting.cpp) and the
[B03 validation record](../reviews/b03-validation.md).

Valid existing lists retain their meaning. Previously accepted malformed
lists now fail; when loaded as application parameters they follow the existing
invalid-affinity warning/default path. Correcting a service's affinity takes
effect on its next application launch. No change is made to processor-group
support or other service settings. The legacy functions match the checked
source snapshot; other historic binaries were not separately tested.
