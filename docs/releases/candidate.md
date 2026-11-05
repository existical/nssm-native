# NSSM Native candidate notes

Status: published as [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) on 5 November 2026. See the
[release notes](v2.25.0-beta.1.md) and [publication record](v2.25.0-beta.1-publication.json).
The tag remains at the original tested source commit, `739b5feabdc768a4524fe28bad2c000b05ee5e07`.
The [QRScanner checklist](qrscanner-beta-acceptance.md) remains reusable for
subsequent application acceptance.
Candidate artifacts are identified by the full commit in build-info.json.
All original tags and history are preserved. The legacy development comparison
baseline is v2.24-103-gdee49fc (`dee49fc1a0804f06687299b8e9612a3fce1c5e9a`),
distinct from the original v2.24 release.

Included work: CONT-001 through CONT-005 and NSSM-001 through NSSM-037.
The [fix records](../fixes/README.md) link every issue to its implementation,
tests and behavior differences. The original 34 tasks are merged. NSSM-035 and NSSM-036
are the B14 startup and cleanup blockers, now merged in PR 20 before PR 19.
The further startup/STOP race NSSM-037 merged in PR 22 before
the extended acceptance in PR 21. All 37 tasks are merged; no original review
task remains unimplemented. [Post-merge CI](https://github.com/existical/nssm-native/actions/runs/41643001489)
passed all five jobs at `f0fdfdd5d3f3762bd825694a3288e9fd99aa07e6`. New
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
The automation merged in [PR 21](https://github.com/existical/nssm-native/pull/21).
Its validation record identifies the exact comparison builds;
this controlled workload does not establish deployment compatibility for every
application.

The supplied [QRScanner acceptance](qrscanner-beta-results.md) passed on
4 November 2026 at 16:38 Riga: Windows 11 x64/LocalSystem, reboot, 24 monitored
hours, 289 samples, 339,235 received events, no observed wrapper/application
restarts and exact legacy rollback. This used the unchanged beta bytes and a
test-only application helper admitting their hash. QRScanner's production helper
still rejects that hash; its helper/provenance adoption change remains separate.

Remaining acceptance gaps include other applications, domain accounts, manual
GUI/accessibility, other Windows builds, large process trees, concurrent hook
storms, signing and production QRScanner installer/helper adoption. No broad OS
compatibility or production-soak claim is made. CI artifacts may
expire; this beta publishes the original packages, manifest and build/test
evidence as release assets.

See [release preparation](README.md) for package commands and publication steps,
and [B14 validation](../reviews/b14-validation.md) for observed results.
