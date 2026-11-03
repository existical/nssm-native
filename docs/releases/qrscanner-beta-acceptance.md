# QRScanner beta acceptance checklist

Status: prepared, not executed. This document does not claim that QRScanner
has run on the candidate. It is the handoff for testing
[NSSM Native 2.25.0-beta.1](v2.25.0-beta.1.md) against the actual application.
Complete the record on an isolated VM or an explicitly designated test service.
Do not replace a production service while following this preparation document.

## Record before testing

| Field | Value to record |
| --- | --- |
| Operator and start/end time | Include timezone and elapsed hours |
| Windows environment | Edition, version/build, architecture and VM/snapshot identity |
| Application | QRScanner commit/version, configuration, data snapshot and representative inputs |
| Test service | Exact name, NSSM executable path, startup mode and service account type |
| Installed baseline | Actual nssm.exe version output, SHA-256 and origin; do not infer identity from the 2.24-103 label |
| Candidate | Package filename/hash, full source commit, tag and nssm.exe hash from build-info.json |
| Readiness | Application-specific probe/command, expected successful scan/output and timeout |
| Restart policy | AppExit, AppRestartDelay, AppThrottle and expected retry behavior |
| Stop/cleanup | Configured shutdown methods, hooks, expected child processes and log tails |
| Resource baseline | Warmed-up NSSM and application memory/handle/thread counts, observed workload and sampling interval |
| Evidence directory | Location for settings, logs, results and observations; redact secrets before sharing |

Read-only binary identification example, with an explicitly chosen path:

```powershell
$binary = 'C:\Test\NSSM Beta\nssm.exe'
& $binary version
Get-FileHash -LiteralPath $binary -Algorithm SHA256
```

- [ ] Verify candidate checksums against SHA256SUMS.txt and inspect its manifest.
- [ ] Retain the original executable and its hash/version independently of the
  replacement location. Record every test service sharing that executable path.
- [ ] Capture service configuration, Parameters including value types, account
  type, dependencies, startup mode, wrapper path, working directory, arguments,
  environment, hooks, streams and rotation settings. Retain a candidate CLI
  configuration export as well as the underlying configuration snapshot.
- [ ] Retain application data/configuration and a VM snapshot or other tested
  recovery method. A service export does not back up account credentials or
  application data. Do not paste secrets into the public result record.
- [ ] Run baseline readiness and representative scans with the installed legacy
  executable. Record expected output and pre-existing defects separately.

## Upgrade and functional checks

Stop the designated test service and verify both the wrapper and its application
processes have exited. A stopped SCM state alone is insufficient evidence of
complete process cleanup. Replace the stopped wrapper at its existing path with
the chosen beta Release executable, without reinstalling or changing settings.
If a shared path cannot be isolated and all owners stopped, use a dedicated VM.

| Check | Pass condition | Result/evidence |
| --- | --- | --- |
| Identity and settings | Executable reports 2.25.0-beta.1 with the expected hash; stored settings and service account/startup/dependencies remain unchanged | Pending |
| Start and readiness | Start command succeeds, SCM reports RUNNING, and the application-specific readiness probe succeeds within its own timeout | Pending |
| Representative work | QRScanner produces expected results on the same inputs as the baseline, including its persistent application state | Pending |
| Environment and paths | Existing empty/nonempty environment values and paths/arguments with spaces reach the application correctly | Pending |
| Hooks | Configured hook commands preserve quoting/literal expressions and run with expected arguments, timing and exit results | Pending |
| Streams and rotation | Configured input works; stdout/stderr, timestamps, final output and enabled rotation are correct | Pending |
| Controlled application failure | An application-owned failure mechanism exits the intended process; NSSM applies the configured restart policy and readiness returns with retained state | Pending |
| STOP during restart/startup | Bounded repeated trials end in STOPPED with intended child/hook cleanup, final logs and no unexpected wrapper termination | Pending |
| Repeated start/stop | At least ten bounded cycles pass without leftover processes or progressive resource growth | Pending |
| Reboot | Reboot the disposable test host; service follows its configured startup policy and the application becomes ready | Pending |
| GUI, if used | View/edit/save required settings without unintended changes; stored hooks retain literal expressions and quoting | Pending |

Use an application-owned failure trigger where possible. Never terminate a
process selected only by a stale PID or a generic image name. Record the actual
service/application process identities for any controlled failure. NSSM's own
CI has deterministic race tests; a manual timing trial does not prove exhaustive
concurrency coverage. Treat an unexpected failure as a finding to investigate,
not a result to erase by rerunning until it passes.

## Sustained run and rollback

- [ ] Run a representative workload for a suggested 24-48 hours. Record the
  actual duration; a shorter test must not be reported as a completed soak.
- [ ] Sample wrapper and application memory, handles, threads, restart count,
  readiness and log growth at a fixed interval (for example every five minutes).
  Separate application growth from NSSM growth. Investigate sustained increases
  and unexpected restarts; retain the measurements rather than declaring an
  arbitrary universal memory threshold.
- [ ] Stop the candidate and verify wrapper/application/owned child exit and
  final log output before replacing its executable.
- [ ] Restore the exact saved legacy executable at the same path. Verify its
  hash/version, unchanged service configuration, readiness, representative
  scans and retained application state. Stop it cleanly again.
- [ ] Record any application data-format changes. Replacing NSSM does not undo
  application migrations; use the application's documented recovery procedure.

## Result and handoff

Record one outcome: **passed**, **failed**, or **incomplete**, with elapsed run
time, package/binary identities, checked rows and links to evidence. Mark a
check **not applicable** only with a reason (for example no hooks configured).
Do not turn pending or skipped work into a pass.

For a failure, preserve command exit codes, SCM states, application observations,
relevant Windows event/log excerpts, timestamps and the smallest reproducible
configuration. Distinguish an expected CLI timeout from an unexpected service
or application failure using the [control policies](../usage.md).

The publication decision should cite this record and any accepted limitations.
Share a sanitized summary in Git; retain sensitive settings and application
data privately. Candidate preparation and automated CI do not authorize or
claim this application's deployment, reboot or production acceptance.
