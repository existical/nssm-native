# NSSM 036 STOP waits for in-flight exit cleanup

| Field | Value |
| --- | --- |
| Priority and origin | P1; B14 lifecycle acceptance follow-up |
| State | In progress; merge pending |
| Reproduced against | `502ba2202d56db8c1d2c955504e6324a4075b176` with forced overlap; real SCM failure in PR 19 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Fix PR | [PR 20](https://github.com/existical/nssm-native/pull/20) |
| Implementation commit | `024bfdb59d5ad60f0b2a7d716c77330ee16260c4` |
| First release | Not yet released |

A STOP received around application exit can race the registered exit callback.
end_service marks stopping before doing descendant, hook and logger cleanup.
The explicit stop thread used to see that flag and return immediately from
its own end_service call, then publish SERVICE_STOPPED. The wrapper could exit
while the callback still owned unfinished cleanup. The real B14 fixture
observed a surviving descendant and missing post-exit hook output after a
STOP during automatic restart. Startup had already been repaired by NSSM-035.

A forced-overlap unit test pauses the callback inside cleanup, starts a STOP
thread, and verifies that neither the thread nor a STOPPED notification can
complete before the callback is released. It failed two assertions before the
fix. It also checks exactly one cleanup and eventual STOPPED after both workers
finish. Real threads/events control the schedule; no user service is touched.

Explicit STOP and the cleanup portion of end_service now share the existing
service-side hook/logger critical section. The stopping flag is inspected
under that lock, and STOP retains ownership through hook waiting and final
status publication. The critical section is recursive, so STOP may call
end_service safely. Restart/exit actions run after releasing cleanup ownership.
Hook/logger workers own snapshots and do not acquire this service-side lock;
no additional thread, handle or newer Windows import is introduced.

Legacy source contains the same unsynchronized early-return pattern. This is
source evidence of the race there, not execution of an original binary.
The live reproduction and deterministic tests ran on the continuation.
A stop may now wait for cleanup already in progress instead of returning
prematurely. CLI polling remains bounded by NSSM-024's policy. This repair
addresses cleanup ownership; it does not claim that every possible concurrent
start/control interleaving has been exhaustively tested.

Validation date: 2026 November 3. All 35 standard groups passed locally in
x64/x86 Debug/Release. AddressSanitizer passed launch, hook_lifecycle, logger,
stdin and control_waits (5/5). MSVC /analyze completed with existing warnings
retained, including tick-count guidance, unchecked API results and large
frames; no analyzer-clean claim is made. PR 20 carries the exact-head CI
checks; PR 19 retains the real lifecycle acceptance scenario. Logs: build/cleanup-red-tests.log,
build/cleanup-*-*.log, build/cleanup-asan.log and build/cleanup-analysis.log.
The live failing trace is [CI 41542921251](https://github.com/existical/nssm-native/actions/runs/41542921251).

```powershell
git diff 502ba2202d56db8c1d2c955504e6324a4075b176 024bfdb59d5ad60f0b2a7d716c77330ee16260c4 -- src/service.cpp tests/launch.cpp
git show dee49fc1a0804f06687299b8e9612a3fce1c5e9a:service.cpp
```
