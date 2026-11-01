# Previously implemented continuation changes

These records backfill the changes merged before the full review of
`5d7454b2cd878b66bcd4b036c33a5b53b184cf3a`. They distinguish existing fixes from
the open NSSM review tasks. **First continuation release: not yet released**
for each record below.

The source comparison baseline is
[dee49fc1a0804f06687299b8e9612a3fce1c5e9a](https://github.com/existical/nssm-native/commit/dee49fc1a0804f06687299b8e9612a3fce1c5e9a),
the recovered `v2.24-103-gdee49fc` development snapshot.
Legacy behavior below was checked by source inspection. Tests described below
exercise the continuation; they do not authenticate or test every distributed
binary labelled 2.24 or 2.24-103.

### CONT 001

**Source layout and maintained builds. Status: implemented, unreleased.**

Legacy source and build resources lived together with Visual Studio 2008
project files. The continuation separates C++ into `src/`, resources into
`resources/`, and historical documentation into `docs/upstream/`. CMake
generates build products outside the source tree and supplies shared x86/x64
Debug/Release presets, static CRT linkage, generated version/message resources
and Windows CI.

Implementation:
[486e81fbcf8eb74a45d6969663f713f33e16d062](https://github.com/existical/nssm-native/commit/486e81fbcf8eb74a45d6969663f713f33e16d062),
[PR 1](https://github.com/existical/nssm-native/pull/1).
Earlier recovery/setup is retained separately at
[2a7aa086a709914e478b98273c970937dca81aad](https://github.com/existical/nssm-native/commit/2a7aa086a709914e478b98273c970937dca81aad).

**Validation:** all four configurations build. `tests/smoke.ps1` checks version
output, PE architecture and embedded metadata; it runs as `nssm.smoke`.
The full four-configuration matrix also passed at the reviewed commit.

**Compatibility:** build paths/tooling changed. This is infrastructure, not a
claim that an application runtime defect was fixed. Compatibility with old
Windows releases must be retested with the current compiler/SDK; original
manual claims apply to original builds. See [building](../building.md) and
[provenance](../upstream.md).

### CONT 002

**Typed registry values in dump helpers. Status: fixed, unreleased.**

The legacy `setting_dump_string` routed a numeric registry type through
`void *` and cast it back to `unsigned long`. On x64 that crossed different
pointer/integer widths and produced conversion diagnostics. The helper now
accepts an `unsigned long` type directly, and its callers pass the numeric
type without pointer conversions.

Implementation:
[40b8969bd9f8f4fa0b469ee755ff21ac9f7ada56](https://github.com/existical/nssm-native/commit/40b8969bd9f8f4fa0b469ee755ff21ac9f7ada56),
[PR 2](https://github.com/existical/nssm-native/pull/2).
Source: [settings.cpp](../../src/settings.cpp), [CMakeLists.txt](../../CMakeLists.txt).

**Validation:** the build treats C4302, C4311 and C4312 as errors. x86/x64
Debug/Release compile successfully; smoke checks pass. This change's guard is
compiler enforcement, not a comprehensive dump/import behavior test.

**Compatibility:** no intended CLI or registry format change. This corrects
the helper's type contract; the separate AppExit export defect in
[NSSM-016](../remediation-plan.md#nssm-016) remains open.

### CONT 003

**Empty environment values. Status: fixed, unreleased.**

When an environment block contained an empty value, the legacy iterator could
advance past the following entry, or past the final terminator. A block such
as `A=` followed by `B=value` could therefore leave B incorrectly configured.
Set and unset processing now find the current entry's end before splitting it
and advance from that saved boundary.

Implementation:
[8fede4b7421525cea125ef1930a3b53faf123065](https://github.com/existical/nssm-native/commit/8fede4b7421525cea125ef1930a3b53faf123065),
[PR 3](https://github.com/existical/nssm-native/pull/3).
Source: [env.cpp](../../src/env.cpp), `set_environment_block`.

**Validation:** [tests/environment.cpp](../../tests/environment.cpp),
`nssm.environment`, checks all 16 empty/nonempty combinations across four
entries in set and unset modes, trailing sentinel protection, an empty block,
expansion and malformed-entry continuation. Passed in all four configurations
at the reviewed commit.

**Compatibility:** an empty value remains an empty value; unset still removes
the named variable. Existing settings require no conversion. Duplicate-key
updating and malformed registry blocks remain separate open findings
[NSSM-003](../remediation-plan.md#nssm-003) and
[NSSM-010](../remediation-plan.md#nssm-010).

### CONT 004

**Hook command quoting. Status: fixed, unreleased.**

Legacy `get_hook` applied path sanitization to a complete command line.
Stripping its outer quotes could break an executable path containing spaces
or alter a final quoted argument. Hook retrieval now preserves the complete
command's quotes while retaining the existing registry-type expansion rules.

Implementation:
[8fede4b7421525cea125ef1930a3b53faf123065](https://github.com/existical/nssm-native/commit/8fede4b7421525cea125ef1930a3b53faf123065),
[PR 3](https://github.com/existical/nssm-native/pull/3).
Source: [registry.cpp](../../src/registry.cpp), `get_hook`.

**Validation:** [tests/hooks.cpp](../../tests/hooks.cpp), `nssm.hooks`, checks
paths with spaces, final quoted/empty arguments, missing/deleted hooks,
REG_EXPAND_SZ expansion, literal REG_SZ and actual launch of a copied test
executable in a path with spaces. Passed in all four configurations at the
reviewed commit.

**Compatibility:** correctly quoted stored commands remain intact. This
does not add quotes to an incorrectly configured command. GUI preservation of
literal environment references still requires the separate
[NSSM-034 investigation](../remediation-plan.md#nssm-034).

### CONT 005

**Startup waiting and failure reporting. Status: fixed, unreleased.**

The legacy START command used AppThrottle as its waiting cutoff, coupling a
restart policy to SCM startup observation. Slow startup could return an
incorrect failure while the service was still progressing.

START now follows SCM checkpoints and wait hints independently of AppThrottle,
with a 60-second polling limit. It reports success after SERVICE_RUNNING and
returns failure for stopped services, query errors, stalled progress and
timeouts, including service-specific startup errors.

Implementation:
[8fede4b7421525cea125ef1930a3b53faf123065](https://github.com/existical/nssm-native/commit/8fede4b7421525cea125ef1930a3b53faf123065),
[PR 3](https://github.com/existical/nssm-native/pull/3).
Source: [service.cpp](../../src/service.cpp), `await_service_start` and
`control_service`.

**Validation:** [tests/startup.cpp](../../tests/startup.cpp), `nssm.startup`,
uses an injected SCM and clock to check slow startup, throttle independence,
checkpoint progress, stalls, endless progress, query/API failures,
service-specific errors and clock rollover. Passed in all four configurations
at the reviewed commit. These are simulated SCM tests, not a live Windows
service readiness test.

**Compatibility:** command duration and error reporting change. AppThrottle
still controls restart throttling. A polling timeout does not stop the
service; SERVICE_RUNNING does not prove application endpoint readiness.
See [usage](../usage.md) for the exact timing policy. Other control commands
are tracked separately in [NSSM-024](../remediation-plan.md#nssm-024).
