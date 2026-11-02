# NSSM 015 Reset-value ownership

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 15, B08 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `d1e07d4c143d25ab5bb9f0a1e8850ea8c75c9443` |
| First release | Not yet released |

`setting_set_string` replaced `value->string` with a borrowed default when
resetting a setting. The CLI subsequently freed that pointer as if it owned
an allocation. A null `value` with a nonnull default was also dereferenced.
The checked legacy function matches the pre-fix continuation function.

The setter now selects input/default text in a local pointer. It never changes
the caller's pointer or allocates a default copy. Registry errors return -1;
ownership remains unchanged on both success and failure.

| Reset setting | Registry result retained from legacy behavior |
| --- | --- |
| Application, AppParameters, AppDirectory | Store an empty REG_EXPAND_SZ value |
| AppStdin, AppStdout, AppStderr | Delete the value; an absent value also succeeds |
| Generic callback with nonempty default | Delete the override, including an explicit case-insensitive match |

The generic nonempty-default case is callback coverage; current settings using
this setter have empty or null defaults. Resetting Application does not make
an empty executable path launchable. No registry migration or default-value
change is introduced.

The initial regression failed three ownership assertions for the three
empty-default settings, without attempting to free a literal. Final tests
cover null input objects, null values, explicit values, empty/nonempty/null
defaults, registry failures and allocation denial. The setter makes no
allocations under that denial. See [settings tests](../../tests/dump_quoting.cpp)
and [B08 validation](../reviews/b08-validation.md).
