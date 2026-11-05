# NSSM 001 Registry expansion bounds

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 1 in the 2026 November 2 review |
| State | Released; merged in PR 5 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | Production code at `05205d7784dbd0e90dc6d26957d3133f7a1c62a9` with the new regression harness |
| Evidence | Guarded destinations, injected expansion failures and legacy source inspection |
| Fix PR | [PR 5](https://github.com/existical/nssm-native/pull/5) |
| Implementation commit | [1ecfc6be59e80fe78ea94553634d02a6ed9524d0](https://github.com/existical/nssm-native/commit/1ecfc6be59e80fe78ea94553634d02a6ed9524d0) |
| Merge commit | [bdeca1cf21314b8fc88b1927333aa69197a8571b](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b) |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

`get_string` accepted a byte capacity and passed it as a character capacity
to environment expansion. An 80-character expansion into a 64-character
destination returned success and overwrote the destination guard.

The reader now derives a character capacity, preflights expansion, writes into
a temporary buffer, and publishes the result only when it fits. It checks
the second call as well, so a changing environment cannot bypass the bound.
An oversized or failed expansion leaves the caller's output empty.
Source: [registry.cpp](../../src/registry.cpp), `get_string`.

[Microsoft's API contract](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw)
defines the expansion buffer size and returned length in characters, including
the terminator. Public NSSM helper capacities remain in bytes; callers do not
need a convention change.

`nssm.registry` in [tests/registry.cpp](../../tests/registry.cpp) checks
expansion lengths 0 through 80 against a 64-character capacity, guards,
exact fit, missing variables, disabled expansion, REG_SZ literal behavior,
allocation failures, API failures and growth between calls.
See [B01 validation](../reviews/b01-validation.md) for before/after results.

Valid values and hook quoting retain their behavior. Oversized values are
rejected rather than truncated. Correct such configuration before restarting
an affected service. No legacy distributed binary was tested.
