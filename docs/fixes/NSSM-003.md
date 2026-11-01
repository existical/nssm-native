# NSSM 003 Duplicate environment updates

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 3 in the 2026 November 2 review |
| State | Fixed, unreleased; merged in PR 5 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | Production code at `05205d7784dbd0e90dc6d26957d3133f7a1c62a9` with the new regression harness |
| Evidence | Guarded allocations, exact resulting blocks and legacy source inspection |
| Fix PR | [PR 5](https://github.com/existical/nssm-native/pull/5) |
| Implementation commit | [1ecfc6be59e80fe78ea94553634d02a6ed9524d0](https://github.com/existical/nssm-native/commit/1ecfc6be59e80fe78ea94553634d02a6ed9524d0) |
| Merge commit | [bdeca1cf21314b8fc88b1927333aa69197a8571b](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b) |
| First release | Not yet released |

Updating `A=one` and `a=two` reserved space for one replacement but wrote it
twice, corrupting the heap. The shared `append_to_double_null` helper now
validates the bounded input, calculates the actual output size, writes the
replacement at the first matching position, and skips subsequent matches.
Unrelated entries retain their order. Character counts and allocation byte
sizes are checked for overflow.

Case-insensitive environment matching still includes the `=` separator, so
`A` does not replace `AB`. Whole-string users, including dependency names,
now compare the complete entry: adding `Service` does not replace
`ServiceLong`. Empty input blocks are supported, and a no-op copy reports its
length. Source: [registry.cpp](../../src/registry.cpp),
`append_to_double_null`.

`nssm.multisz` in [tests/registry.cpp](../../tests/registry.cpp) tests
duplicates at different positions, one through six consecutive duplicates,
empty/longer/shorter replacements, case-sensitive operation, prefix collisions,
empty input, removal after append, allocation failure and invalid blocks.
Allocation guards and protected pages check the bounds.
See [B01 validation](../reviews/b01-validation.md).

Updating a duplicated key now normalizes that key to one entry; it does not
rewrite every unrelated duplicate in the block. Valid single-key
configurations need no migration. Environment changes take effect when the
application is next launched. The existing CONT-003 empty-value fix remains
covered by `nssm.environment`.
