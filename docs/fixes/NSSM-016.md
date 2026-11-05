# NSSM 016 AppExit export and numeric key names

| Field | Value |
| --- | --- |
| Priority and review | P2; B10 |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| Fix PR | [PR 15](https://github.com/existical/nssm-native/pull/15) |
| Implementation commit | `fc029e716391d3aa9ff3399cd388b428da8e4561` |
| Merge commit | `f28a43141a576033713190fe9b716fb503ac227a` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

The old dump tested the terminator after scanning a value name to decide
whether it was the default. Every numeric entry was consequently exported as
Default. Its digit test used OR, accepting non-digits, and enumeration errors
retried the same index indefinitely. The callback also received settings
metadata where it expected a default string, and leaked its key/value storage.
These defects exist in recovered legacy source and the B10 baseline.

The dumper now opens AppExit relative to the source Parameters key and queries
each stored action directly through the validated NSSM-033 reader. Only an
empty registry name selects Default. Canonical decimal names from 0 through
4294967295 are accepted, and every explicit rule is emitted, including Restart
overrides and explicit defaults. Stack-owned action values avoid per-rule
allocations; the key closes on success and failure. Direct stored-value reads
also avoid confusing metadata with default text or exporting a fallback when
an enumerated value disappears.

Enumeration errors, oversized names, invalid names and malformed values stop
that setting's export with a diagnostic and a nonzero result. The fixed name
buffer holds every supported DWORD name; longer names are rejected, never
retried indefinitely. Output is streaming: a failed dump can leave a partial
script. Check the command's exit status and discard output on failure. A dump
is not a transactional snapshot while another process changes configuration.

Set/get/reset retain the checked base-zero CLI number parser, but new writes
and numeric resets use canonical decimal names. For example, `42`, `052`
(octal), `0x2a`, `+42` and leading-whitespace `42` address the same key `42`.
`042` means octal 34 at the CLI. Runtime lookup already used canonical decimal.
Old raw aliases such as registry names `042`, `0x2a` or `+42` were not read by
runtime lookup. Export now rejects them rather than silently activating them.
They are not automatically renamed or removed. Back up and inspect such raw
registry entries before manually replacing/removing them; setting or resetting
a canonical code does not delete historical alias names.

When `dump Source Target` is used, AppExit, hooks and native Environment now
read Source while emitting commands for Target. Dump result strings and CLI
service/registry objects are released on return; GUI ownership remains a
separate B11 task. Existing batch quoting limitations in [NSSM-002](NSSM-002.md)
still apply. No registry format, action name or message resource was replaced.

The initial regression lost `42=Exit` with `Default=Ignore` and failed two
enumeration-error assertions. Final tests cover all four actions, default and
numeric rules, DWORD maximum, aliases/invalid/long names, wrong value types,
forced enumeration failure, allocation failure, setter/reset canonicalization,
renamed dumps and 100 repeated exports with balanced allocations/handles.
Actual emitted arguments are replayed through the production setter and
re-exported for equality. See [export tests](../../tests/exit_export_cases.h),
[settings implementation](../../src/settings.cpp), [NSSM-033](NSSM-033.md) and
[B10 validation](../reviews/b10-validation.md).
