# NSSM Native candidate notes

Status: preparation only; no release tag is assigned and nothing is published.
Candidate artifacts are identified by the full commit in build-info.json.
All original tags and history are preserved. The legacy development comparison
baseline is v2.24-103-gdee49fc (`dee49fc1a0804f06687299b8e9612a3fce1c5e9a`),
distinct from the original v2.24 release.

Included work: CONT-001 through CONT-005 and NSSM-001 through NSSM-036.
The [fix records](../fixes/README.md) link every issue to its implementation,
tests and behavior differences. The original 34 tasks are merged. NSSM-035 and NSSM-036
are the startup and cleanup blockers found during B14 acceptance, now merged
in PR 20 before release-preparation PR 19. All 36 tasks are fixed and unreleased.
No review task remains unimplemented. New
findings and release acceptance gaps remain possible; completion of this review
is not a claim that all bugs have been eliminated.

Changes include memory/handle ownership, registry bounds, command quoting,
logging/rotation, process identity, GUI retries, account handling, dependencies,
configuration exports and bounded service controls. See the
[changelog](../../CHANGELOG.md) and [comparison guide](../comparing-builds.md).
The command-line and registry schema are retained. Notable behavior changes:

- Invalid numeric/registry input and incomplete exports now report failures.
- Empty environment entries, hook quoting and literal hook editing are preserved.
- Dependency reset/removal clears exactly the requested type.
- START, STOP, PAUSE and CONTINUE have documented polling deadlines and preserve
  service errors. A CLI timeout does not cancel the service operation.
- Process cleanup verifies identity; logger and hook resources remain owned
  until workers complete or a bounded cleanup policy applies.

For upgrade evaluation, record the old executable hash/version and export each
service configuration before replacing binaries. Retain a known-good binary,
configuration backup and dependent application data. On a test host, stop the
service, confirm it stopped, replace the executable at its existing path, and
verify start, application readiness, restart behavior, stop, hooks and logs.
Review the [usage policies](../usage.md), especially timeouts and failure exit
codes that automation may previously have ignored. Existing services do not
need reinstalling merely to update the executable. Keep rollback local and
tested; this project has not validated every third-party service configuration.

Validation scope: the standard matrix covers x64/x86 Debug/Release. The opt-in
SCM checks use a disposable elevated Windows Server 2022 runner, including a
real NSSM wrapper with stdin, an empty environment entry followed by a value,
quoted hook paths/arguments, application crash/restart, repeated start/stop,
stdout/stderr tail draining and descendant cleanup. Hidden-dialog tests are
automated; they are not a manual interactive GUI acceptance pass.

The [B15 upgrade rehearsal](upgrade-rehearsal.md) passed a separate legacy-to-
candidate-to-legacy replacement check with a persistent PowerShell worker.
The automation is in [PR 21](https://github.com/existical/nssm-native/pull/21),
pending merge. Its validation record identifies the exact comparison builds;
this controlled workload does not establish deployment compatibility for every
application.

Known acceptance gaps: representative long-running applications, domain
accounts, manual GUI/accessibility, older Windows releases, large process trees,
concurrent hook storms, signing and application-specific upgrade/rollback trials.
No broad OS compatibility or production-soak claim is made. CI artifacts may
expire; a release must publish permanent assets and the matching manifest.

See [release preparation](README.md) for package commands and publication steps,
and [B14 validation](../reviews/b14-validation.md) for observed results.
