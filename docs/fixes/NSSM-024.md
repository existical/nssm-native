# NSSM 024 Bounded service control waits

| Field | Value |
| --- | --- |
| Priority and review | P2; B13; review finding 24 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `89c918c6d33a3f7548d4899a718e55e4f61c5be4` |
| Fix PR | Pending creation |
| Implementation commit | `d92c27cb022d1d573070e8f6a62b1964339db42b` |
| First release | Not yet released |

Legacy stop/pause/continue called await_service_control_response with a zero
cutoff. Its growing polling interval stopped at 500 ms, but its loop had
neither a total deadline nor a no-progress deadline. The reviewed continuation
retained that loop after START received its separate fix. A service stuck in
its initial or pending state could leave the CLI waiting indefinitely. Legacy
source inspection confirmed the zero-cutoff overload and call sites; no legacy
binary was executed. The initial regression against the merge baseline failed
18 checks: nine query-guard failures and nine incorrect elapsed-time results.

The shared poller now distinguishes completion, terminal failure, query error,
stalled progress and total deadline. It preserves the existing START contract:
only RUNNING succeeds, its allowance stays 60 seconds, and AppThrottle remains
unrelated. Pause and continue each have a separate 60-second policy. STOP uses
180 seconds for native/unreadable configuration, or a bounded allowance derived
from enabled NSSM stop delays plus hook/logger/cleanup reserves. See the
[exact policy and defaults](../usage.md), including the 24-hour saturation cap.

State changes or increasing checkpoints renew the progress window. Hint-only
changes and decreasing checkpoints do not. Hints default to ten seconds and
are clamped between one second and the action allowance. Unsigned tick
subtraction supports clock rollover. Sleeping never deliberately exceeds the
remaining progress or total budget. A final successful query at the deadline
is accepted; a still-pending service times out even with continuous progress.
These rules follow Microsoft's
[SERVICE_STATUS contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status).

The CLI retains the exact query/terminal error, includes service-specific
shutdown codes, prints a reason for either timeout, and returns failure.
Successful ControlService calls no longer report a stale last-error value.
The historical already-stopped STOP result remains successful. The adjacent
status-query branch now closes both manager and service handles on success
and failure. Registry policy reads are optional, read-only and bounded, with
strict DWORD size/type validation and 64-bit summation.

Compatibility changes: a stuck control command now ends with failure; STOPPED
with a reported shutdown error also fails. Timeout leaves the service and its
accepted control running. The allowance is a client policy, not a promise that
any process tree or number of hooks will finish. Windows API/scheduling time
is additional; see Microsoft's
[ControlService blocking behavior](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice).
No registry migration, new service rights, release tag or upstream tag changes
are introduced.

The [deterministic cases](../../tests/control_waits_cases.h) execute production
code with substituted SCM/clock boundaries and an HKCU registry fixture.
They include a valid four-minute stop and a finite total timeout despite
continuous progress, as well as all outcome classes and clock rollover. The
[opt-in live cases](../../tests/controls_live_cases.h) execute the real CLI
against three owned native services, including stalled pause and shutdown
code 42. They do not claim full wrapped-application lifecycle coverage.
See [B13 validation](../reviews/b13-validation.md) for results and limitations.

Compare the immutable implementation with its merge baseline:

```powershell
git diff 89c918c6d33a3f7548d4899a718e55e4f61c5be4 d92c27cb022d1d573070e8f6a62b1964339db42b -- src/service.cpp src/nssm.h tests
git show dee49fc1a0804f06687299b8e9612a3fce1c5e9a:service.cpp
```
