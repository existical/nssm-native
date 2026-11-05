# NSSM 037 Startup cancellation and process-wait ownership

| Field | Value |
| --- | --- |
| Priority and origin | P1; B15 release acceptance follow-up |
| State | Fixed, unreleased |
| Reproduced against | `9812f2ecaf1ccf7ea8788885d0105d62db4312a7`, with forced startup/STOP overlap |
| Live failing revision | `bd94d75db99855a34f05933ef5f564ca20acb26b` in PR 21; production source matches that baseline |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Fix PR | [PR 22](https://github.com/existical/nssm-native/pull/22) |
| Implementation commit | `ab23475f71d700e1705d5be1681c49eb72775466` |
| Merge commit | `6ff83e100dddabc879000c3122b93759c0848a66` |
| First release | Not yet released |

During B15's final rerun, the upgrade/rollback scenario passed but the existing
wrapped lifecycle test reported that NSSM terminated unexpectedly while STOP
arrived during automatic restart. See
[CI 41629829699](https://github.com/existical/nssm-native/actions/runs/41629829699).
The run retained final application output but lacked completed wrapper cleanup.

Follow-up found three unprotected startup intervals. STOP could close the
application handle while startup waited on it; a canceled startup could return
success to monitor_service, which then registered a wait using an already-cleared
handle; and cancellation during Start/Pre could still be followed by process
creation. NSSM-036 serialized cleanup itself, but did not protect startup or
monitor publication. Legacy source contains these same intervals.

The extended nssm.launch regression forces STOP while startup is waiting using
real scheduling events and fake process/SCM boundaries. It also injects STOP in
Start/Pre and Start/Post. The original source failed six checks: premature
closure of the waited handle, late/invalid registrations, process creation
after cancellation, and a process remaining after STOP. No real service was
controlled by this reproduction.

The fix uses the existing service-side hook/logger lock for launch publication,
SCM status updates and registration. Restart permission uses interlocked access.
It rechecks cancellation after throttling and Start/Pre, and before publishing
RUNNING or registering a monitor. Startup waits on its own non-inheritable
SYNCHRONIZE handle, outside the lock, so STOP can still clean up and wake it.
SCM progress updates remain protected and their wait hint saturates safely.
Duplication failure reports a startup error and cleans up the created process.
The old one-shot registration is unregistered before replacement without waiting
for a callback which may be the current thread.

Windows requires wait handles to remain valid throughout a pending wait.
Even a one-shot thread-pool registration must be unregistered after use; an
in-progress callback can produce ERROR_IO_PENDING while cancellation succeeds.
See Microsoft's [wait contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject),
[registration contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-registerwaitforsingleobject)
and [unregistration contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-unregisterwait).

Additional tests cover cancellation before launch, duplication failure,
replacement/cancellation of a previous registration and complete handle cleanup.
The forced wait test requires STOP to complete while startup remains paused,
checking that the repair does not hold its lock across the wait. AppThrottle's
existing behavior and the CLI's separate bounded control policy are retained.
No registry schema, command syntax or newer Windows API is introduced.

Validation date: 2026 November 3. All 37 standard groups passed locally in each
of x64/x86 Release/Debug. AddressSanitizer passed launch, hook_lifecycle, logger,
stdin and control_waits (5/5). MSVC /analyze completed with existing large-frame,
unchecked-result and related warnings; no analyzer-clean claim is made.
Logs remain in ignored build/b15-race-*.log. CI additionally repeats the real
crash/restart/STOP lifecycle ten times, stopping on the first failure; results
are recorded in the PR before merge.

The focused repair at `d995289fd939461839bbd89d464e21eeb922d204` passed the
four-configuration CI matrix (41 groups in x64 Release; 37 elsewhere) and all
ten additional lifecycle repetitions in
[CI 41632491156](https://github.com/existical/nssm-native/actions/runs/41632491156).
The combined acceptance branch at `9e5a139784bddd59e6335f48bb48f99be171307e`
also passed its matrix (42 in x64 Release, including upgrade/rollback), plus
ten repetitions, in
[CI 41632720465](https://github.com/existical/nssm-native/actions/runs/41632720465).
See [B15 validation](../reviews/b15-validation.md) for the actual binary identities.

PR 22 merged before PR 21 on 2026 November 3. The combined master revision
`f0fdfdd5d3f3762bd825694a3288e9fd99aa07e6` passed all five jobs in
[post-merge CI 41643001489](https://github.com/existical/nssm-native/actions/runs/41643001489),
including 42 x64 Release groups, ten additional lifecycle repetitions and
source-archive packaging. This establishes merged, tested inclusion; the
first public release remains unassigned.

The failing live run has no captured crash dump, so it records the observed
termination rather than a proven exception instruction. The deterministic tests
establish the ownership defects independently. Legacy evidence for these races
is source inspection, not execution of an original distributed binary. This
repair does not claim exhaustive coverage of every concurrent control schedule.

```powershell
git diff 9812f2ecaf1ccf7ea8788885d0105d62db4312a7 ab23475f71d700e1705d5be1681c49eb72775466 -- src/service.cpp src/service.h tests/launch.cpp
git show dee49fc1a0804f06687299b8e9612a3fce1c5e9a:service.cpp
```
