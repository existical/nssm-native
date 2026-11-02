# NSSM 033 Initialized AppExit fallback values

| Field | Value |
| --- | --- |
| Priority and review | P2; B10 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| Fix PR | [PR 15](https://github.com/existical/nssm-native/pull/15) |
| Implementation commit | `fc029e716391d3aa9ff3399cd388b428da8e4561` |
| First release | Not yet released |

The legacy reader could return success after a missing/default query failed,
leaving the action buffer unchanged or partially written. It passed a character
count as a byte capacity and accepted arbitrary registry types without checking
termination or action names. Numeric query errors were treated as missing
overrides and silently retried against the default. The relevant legacy code
matches the B10 baseline. Five initial output/error assertions failed.

Both outputs are now initialized before registry access: action is Restart and
default_action is true. An absent AppExit key or absent unnamed default returns
successful Restart. An absent numeric rule tries the unnamed default once.
A valid explicit numeric rule sets default_action to false; a valid default
keeps it true. Query/open errors and malformed values return failure with
initialized outputs; callers must inspect that result before interpreting them.

A shared reader uses a byte capacity of ACTION_LEN times sizeof(TCHAR),
requires REG_SZ with a bounded, terminated string, permits only zero padding
after the terminator, and validates Restart/Ignore/Exit/Suicide without regard
to case. Successful text is canonicalized to those spellings. Wrong types,
empty/unknown actions, odd sizes, embedded trailing data, missing terminators
and oversized data fail. REG_EXPAND_SZ is not an AppExit action type; no
environment expansion is attempted. Malformed explicit rules do not silently
fall back to an otherwise valid Ignore/Exit default.

CLI get reports invalid configuration instead of exposing uninitialized text.
Runtime callers retain their initialized Restart choice when the reader fails.
Missing configuration now has a defined successful Restart result. The getter
propagates allocation failure and tolerates null default metadata. The setter
validates the entire input before opening a key, initializes its default/code
choices, and uses actual registry return codes for diagnostics. Invalid input
cannot leak the opened key. An overlong registry path now fails with a null
handle instead of reporting success with an unset handle.

Tests use isolated HKCU keys and production functions. Coverage includes
missing key/default/override, all valid actions and mixed case, malformed raw
values, query/open denial, long paths, allocation failure, default resets and
numeric canonicalization. The unterminated fixture has a nonzero adjacent
guard and checks its stored byte count. See [registry tests](../../tests/registry.cpp),
[export tests](../../tests/exit_export_cases.h), [NSSM-016](NSSM-016.md) and
[B10 validation](../reviews/b10-validation.md).
