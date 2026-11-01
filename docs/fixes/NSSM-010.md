# NSSM 010 Registry string validation

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 10 in the 2026 November 2 review |
| State | Fixed, unreleased; merged in PR 5 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | Production code at `05205d7784dbd0e90dc6d26957d3133f7a1c62a9` with the new regression harness |
| Evidence | Malformed HKCU values, injected query changes and legacy source inspection |
| Fix PR | [PR 5](https://github.com/existical/nssm-native/pull/5) |
| Implementation commit | [1ecfc6be59e80fe78ea94553634d02a6ed9524d0](https://github.com/existical/nssm-native/commit/1ecfc6be59e80fe78ea94553634d02a6ed9524d0) |
| Merge commit | [bdeca1cf21314b8fc88b1927333aa69197a8571b](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b) |
| First release | Not yet released |

The readers previously trusted registry data to contain its terminators.
An unterminated environment block was accepted and scanned past its returned
bytes. [RegQueryValueEx](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw)
does not guarantee termination of stored strings.

`get_string` now accepts only REG_SZ/REG_EXPAND_SZ values with aligned byte
sizes and a NUL inside the returned data. `get_environment` accepts only
REG_MULTI_SZ with a complete double NUL inside the returned data. Both accept
zero padding and reject nonzero data after the end marker. Wrong types,
odd-byte Unicode data, absent terminators and zero-byte stored strings fail
cleanly. Optional missing values still succeed with empty output.

Environment reads revalidate the actual query's type and size and retry growth
at most three times. They free a previous owned environment block and return
a null pointer and zero length on failure, without exposing partial data.
A correctly stored empty two-NUL environment block is now accepted.
Source: [registry.cpp](../../src/registry.cpp), `get_string`,
`get_environment`, `terminated_string` and `double_null_length`.

`nssm.registry` in [tests/registry.cpp](../../tests/registry.cpp) covers these
representations, repeated reads, allocation failure, size growth, perpetual
growth, changed type, odd returned size and query failure.
See [B01 validation](../reviews/b01-validation.md).

Malformed manually written registry values must be corrected; the helpers
report failure and log the read error instead of attempting to repair their
contents. A caller may choose to continue without optional environment
settings; this change does not promise that every malformed optional value
stops service startup. Valid empty values inside an environment block retain
their previous meaning. A two-NUL AppEnvironment block explicitly describes
an empty replacement environment; remove the value to use inherited defaults.
