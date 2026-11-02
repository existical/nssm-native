# Changelog

## Unreleased

- Prepare candidate packages from clean pinned source archives with full build
  and test metadata, SHA-256 checksums and preserved source identity. Support
  distinct beta/RC tags while retaining original version tags. Add a real
  NSSM-wrapped lifecycle acceptance test and packaging validation in CI.
  See [release preparation](docs/releases/README.md). No release is published.

- Bound stop, pause and continue polling with independent progress and total
  limits; derive the stop budget from enabled shutdown delays and hook waits.
  Preserve SCM query/terminal errors and service-specific shutdown codes,
  distinguish stalled progress from total timeout, and ignore stale last-error
  values after successful controls. Close status-query handles on every path.
  Add deterministic and opt-in live SCM regressions.
  [NSSM-024](docs/fixes/NSSM-024.md).

- Reset/remove service and group dependencies without retaining or discarding
  the wrong entries. Build typed updates from one snapshot, use explicit empty
  lists, preserve unrelated entries, validate bounded query/group data and
  close validation resources. Report dependency dump allocation failures.
  Add unit cases and an opt-in real SCM round-trip test.
  [NSSM-023](docs/fixes/NSSM-023.md).

- Keep the GUI service handle valid after validation/save failures, clean up
  temporary configuration consistently, and own the original object through
  dialog completion. [NSSM-020](docs/fixes/NSSM-020.md).
- Wipe and free password-confirmation buffers on every path, and release the
  typed username when replacing it with a canonical well-known account.
  [NSSM-025](docs/fixes/NSSM-025.md).
- Close account lookup policies, free SIDs and LSA/conversion buffers with their
  matching deallocators, initialize failure outputs and validate counted names.
  Lookup-only calls use lookup policy access. [NSSM-026](docs/fixes/NSSM-026.md).
- Compare LSA service-logon rights within their counted character length and
  reject malformed metadata. [NSSM-027](docs/fixes/NSSM-027.md).
- Preserve literal environment references, complete quoting and registry string
  types when editing hooks in the GUI; retain execution-time expansion.
  [NSSM-034](docs/fixes/NSSM-034.md), now confirmed.

- Preserve every explicit AppExit rule in export, reject invalid/noncanonical
  names, stop on enumeration errors, and write new numeric rules under
  canonical decimal names. Read source configuration when exporting under a
  new service name and release dump allocations/handles.
  [NSSM-016](docs/fixes/NSSM-016.md).
- Request query-only service access for dump/get while retaining change access
  for set/reset/edit. Add an opt-in test of real service ACLs on a disposable
  Windows runner. [NSSM-030](docs/fixes/NSSM-030.md).
- Define missing AppExit configuration as Restart, validate stored action types,
  sizes and text, and report malformed/query-failure cases with initialized
  outputs. [NSSM-033](docs/fixes/NSSM-033.md).

- Make configured stdin inheritable and retain launch-scoped parent cleanup.
  [NSSM-012](docs/fixes/NSSM-012.md).
- Correct UTF-16 timestamp buffers, preserve split code units and recognize
  complete newline characters; keep BOM handling consistent across rotation.
  [NSSM-013](docs/fixes/NSSM-013.md).
- Count actual log bytes once, complete short writes, stop on incomplete
  output, and preserve rotation policy when metadata queries fail. Check
  online copy/truncate failures and preserve content after failed rename.
  [NSSM-014](docs/fixes/NSSM-014.md).
- Replace event varargs with counted string insertions, fix startup error
  text and select the proper delayed-start event message.
  [NSSM-022](docs/fixes/NSSM-022.md).
- Add three B09 regression groups for real stdin children, exact stream bytes
  and event arguments, plus compile-time rejection of integer insertions.

- Preserve caller ownership when resetting string settings, including null
  inputs and borrowed defaults. [NSSM-015](docs/fixes/NSSM-015.md).
- Use correctly sized BOOL storage for administrator checks and treat API
  failure as nonmembership. [NSSM-018](docs/fixes/NSSM-018.md).
- Perform one nonblocking handle poll at zero timeout, with defined results
  and no wait-progress update. [NSSM-019](docs/fixes/NSSM-019.md).
- Leave install-object destruction to CLI/GUI callers; handle CLI allocation
  failure before field access and release objects on oversized arguments.
  [NSSM-021](docs/fixes/NSSM-021.md).
- Scope debug-token ownership, check privilege assignment, and restore the
  caller's prior privileges/impersonation after process inspection. Abort with
  code 111 if restoration fails. [NSSM-028](docs/fixes/NSSM-028.md).
- Correct error-message capacity units and fallback allocation; use automatic
  thread-local ownership for reusable buffers. [NSSM-032](docs/fixes/NSSM-032.md).
- Add six B08 regression groups covering API failures, hidden installer cleanup,
  real event waits and returning message-worker threads.
- Retain original process handles through descendant cleanup, validate each
  child against its immediate parent, and skip unverifiable identities.
  Check process/thread identity before signalling and close read-only tree
  traversal handles. [NSSM-009](docs/fixes/NSSM-009.md).
- Add deterministic PID/TID reuse and failure cases, service root ownership
  checks, and live cleanup of isolated seven-process trees. Document depth,
  snapshot, shared-console and foreign-window limitations.
- Correct offline rotation's swapped size/delay arguments for stdout and
  stderr. Named options preserve the full 64-bit size threshold and use only
  the configured copy/truncate delay. [NSSM-008](docs/fixes/NSSM-008.md).
- Add offline rotation regressions for thresholds, ages, delay requests,
  both modes and streams, shared paths, failures and archive collisions.
- Close both parent log-pipe writers before joining workers, retain valid
  thread handles through waits, and give each worker its own reference-counted
  state and I/O handles. Timeout/failure requests cancellation while preserving
  ownership until the worker exits. [NSSM-007](docs/fixes/NSSM-007.md).
- Keep parent pipe writers non-inheritable, clean up partial output setup and
  service disposal, and add logger EOF, blocked-I/O, failure and restart tests.
- Keep hook metadata alive independently of worker completion, reserve worker
  tracking before thread creation, and retain unfinished workers on failed or
  timed-out waits. [NSSM-005](docs/fixes/NSSM-005.md).
- Avoid duplicate frees when hook thread creation fails and return the actual
  synchronous fallback result, including an abort request.
  [NSSM-006](docs/fixes/NSSM-006.md).
- Close primary-thread handles from hook/application launches and both handles
  from environment-validation launches. Confirm validator exit with a bounded
  wait, clean up pre-start-abort I/O handles, and stop after a failed application
  resume. [NSSM-017](docs/fixes/NSSM-017.md).
- Add forced hook schedules/failures, launch cleanup regressions, concurrent
  cleanup/registration and 200-launch live handle-count checks.
- Parse affinity lists directly into a checked 64-bit mask, rejecting invalid
  endpoints, reversed ranges and empty tokens without overflowing a temporary
  array. [NSSM-004](docs/fixes/NSSM-004.md).
- Count sparse CPU masks correctly and map GUI affinity rows to actual CPU
  identifiers; stop inventing CPUs when the affinity query fails.
  [NSSM-029](docs/fixes/NSSM-029.md).
- Reject unsigned numeric inputs without digits, negative values and overflow;
  preserve complete-input validation for settings and prefix parsing for
  affinity ranges. [NSSM-031](docs/fixes/NSSM-031.md).
- Add numeric/affinity regressions, registry preservation checks and hidden
  Windows list-box tests. Fix the affinity formatter's allocation-failure check
  and terminate the invalid-affinity warning's argument list.
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
