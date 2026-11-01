# Changelog

## Unreleased

- Add a prioritized [fix implementation plan](docs/remediation-plan.md),
  [records for previously merged changes](docs/fixes/implemented.md), and a
  [legacy build comparison guide](docs/comparing-builds.md). Review tasks remain
  planned until their implementations are merged.
- Fix environment block iteration so empty values do not skip subsequent
  variables or walk past the block's end. [CONT-003](docs/fixes/implemented.md#cont-003).
- Preserve hook command-line quotes, including executable paths with spaces
  and final quoted arguments, while retaining environment expansion.
  [CONT-004](docs/fixes/implemented.md#cont-004).
- Separate `nssm start` waiting from `AppThrottle`. Follow SCM checkpoints and
  wait hints with a 60-second polling limit; report startup, query and timeout
  failures instead of treating a pending start as successful.
  [CONT-005](docs/fixes/implemented.md#cont-005).
- Add Windows regression tests for environment blocks, hook commands and
  service startup, alongside the executable smoke checks.
- Pass registry value types directly to the settings-dump helper, eliminating
  x64 pointer conversion warnings. Treat pointer-size conversion warnings as
  compiler errors to prevent regressions. [CONT-002](docs/fixes/implemented.md#cont-002).
- Move C++ code, Windows resources and historical documentation into dedicated
  directories while preserving the original Git history and release tags.
- Add a CMake build with shared x86/x64 Debug and Release presets, static C
  runtime linkage and generated message/version files outside the source tree.
  [CONT-001](docs/fixes/implemented.md#cont-001).
- Add executable smoke checks and a Windows GitHub Actions build matrix.
- Add current build, usage and contributor documentation.

Historical release notes are preserved in
[docs/upstream/ChangeLog.txt](docs/upstream/ChangeLog.txt).
