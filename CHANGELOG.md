# Changelog

## Unreleased

- Bound command quoting by counting and writing through the same checked
  traversal. Oversized dump arguments fail without overwriting buffers or
  emitting a truncated command. [NSSM-002](docs/fixes/NSSM-002.md).
- Add quoting capacity, dump failure and real cmd.exe argument regressions;
  document the existing batch-file percent-sign limitation.
- Bound registry environment expansion to the actual destination capacity.
  [NSSM-001](docs/fixes/NSSM-001.md).
- Replace duplicate environment keys once, preserving unrelated entry order
  and preventing heap overwrites. [NSSM-003](docs/fixes/NSSM-003.md).
- Validate registry string types, byte sizes and termination; reject malformed
  data, bound retries for changing environment blocks, and release previous
  loaded blocks. [NSSM-010](docs/fixes/NSSM-010.md).
- Stop MULTI_SZ formatting at its bounded end marker and preserve terminated
  output. [NSSM-011](docs/fixes/NSSM-011.md).
- Add registry and MULTI_SZ regressions with allocation guards, protected
  memory pages and deterministic query/expansion failures.
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
