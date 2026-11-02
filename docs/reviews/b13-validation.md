# B13 service control validation

Validation date: 2026 November 3. Scope: [NSSM-024](../fixes/NSSM-024.md).
Implementation is pending review and merge; no release is published.

Baseline: `89c918c6d33a3f7548d4899a718e55e4f61c5be4`, after PR 17 merged.
Implementation: `d92c27cb022d1d573070e8f6a62b1964339db42b`.
Legacy source: `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`
(`v2.24-103-gdee49fc`). The zero-cutoff non-START overload and callers establish
the legacy unbounded loop. No historical executable was run.

The initial isolated control regression failed 18 checks against the baseline:
nine 2,000-query guards detected unbounded polling and nine elapsed-time
assertions failed. Production polling and control code were compiled with
substituted SCM/clock calls; registry writes used a process-local HKCU fixture.

Final deterministic checks cover:

- Stop/pause/continue completion and delayed initial-to-pending transitions;
  STOP from PAUSED, shutdown control, already-stopped STOP and rotate.
- Stalled checkpoints, hints changed without progress, decreasing checkpoints,
  zero/tiny/maximum hints and total deadlines despite continuous progress.
- Query failure, unexpected state, Windows and service-specific errors,
  stale last-error on success, ERROR_IO_PENDING and immediate control denial.
- Clock rollover on completion and stall; unchanged START regression suite.
- Default/native and configured policy, enabled/skipped methods, malformed
  DWORD size/type, saturating sums of MAXDWORD delays, a successful 240-second
  stop and a 376-second total timeout with ongoing progress.
- Status-query success/failure and manager/service handle closure.

| Local configuration | Result |
| --- | --- |
| x64 Release | 35/35 groups passed |
| x64 Debug | 35/35 groups passed |
| x86 Release | 35/35 groups passed |
| x86 Debug | 35/35 groups passed |
| x64 Release AddressSanitizer | 7/7 selected groups passed |
| x64 Release MSVC /analyze | Build completed; existing warnings remain |

The sanitizer selection was startup, control_waits, waits, launch,
hook_lifecycle, dependencies and export_access. No sanitizer findings were
reported. Static analysis retained GetTickCount rollover guidance (tested by
unsigned subtraction cases), ignored API-result/sizing warnings, large stack
frames, shadowing, critical-section guidance and the registry MULTI_SZ warning.
This is not a claim that the repository is analyzer-clean.

Local environment: Windows 10.0.26300, Visual Studio 2022 Build Tools,
MSVC 19.44.35229 and Windows SDK 10.0.26100. Local token is not an administrator;
no real local service was created or controlled. Local logs are ignored build
artifacts: build/b13-red-tests.log, build/b13-x64-Release.log,
build/b13-x64-Debug.log, build/b13-x86-Release.log, build/b13-x86-Debug.log,
build/b13-asan.log and build/b13-analysis.log.

The opt-in controls_live fixture is compiled in every configuration and will
run on the disposable elevated x64 Release CI runner. Three unique native
services exercise real start/pause/continue/stop, a pause stalled past its hint
and a shutdown reporting service-specific code 42. Each host has a 30-second
watchdog; the fixture stops/deletes only its own services and does not force
terminate any process. Existing export/dependency fixtures remain disabled.
CI results will be recorded after the PR run completes.

Limits: native control integration does not validate full NSSM application
shutdown, simultaneous hooks, output draining and descendant teardown as one
end-to-end scenario. Those remain B14 release acceptance work. No manual GUI,
domain account, older Windows or release-package acceptance was performed.
Polling bounds exclude Windows API call/scheduler delays and do not cancel
accepted controls. Registry snapshots may differ from settings already loaded
by a running wrapper; arbitrary descendant/hook counts are not predictable.

Reproduce the standard matrix with scripts/build.ps1 for each architecture and
configuration; see [building](../building.md) for opt-in SCM commands and test
limits, and [usage](../usage.md) for the published timeout policy.
