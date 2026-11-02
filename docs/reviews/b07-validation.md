# B07 process identity validation

Validation date: 2026 November 2. Scope: [NSSM-009](../fixes/NSSM-009.md).
The fix record tracks merge and release status.

The pre-fix commit was `9a5a14549b1837ba518ed577905aaafe04236d66`, after
PR 11 merged. Its complete `src/process.cpp` matches `process.cpp` at
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`).

Two initial fake-process cases produced four failed checks against that
pre-fix source. A child PID was replaced when the validation handle closed;
legacy recursion then terminated the replacement. The root was reopened,
the caller's handle was overwritten, and a missing root handle still allowed
PID-based targeting. All four assertions pass with the repair. This is a
controlled API-contract reproduction, not a live unrelated-process kill.

`nssm.process_identity` compiles the production walker and stop methods with
test-only Windows API substitutions. Fake objects retain reference counts,
quarantine closed handles and reject use after close. Every case checks that
all owned process/thread/snapshot handles close; the root remains caller-owned.

| Case | Evidence |
| --- | --- |
| Child reused after validation close | Original child handled once, replacement untouched, no root reopen |
| Reuse between snapshot and open | Later birth rejected; equal-time replacement with another parent rejected by fresh snapshot |
| Root identity failures | Missing handle, wrong PID, changed/zero creation time, missing identity API and failed time query produce no termination |
| Failed running-state query | Failed wait does not authorize termination |
| Candidate failures | Disappeared, access-denied and failed-time-query children skipped; valid sibling still handled |
| Parent lifetimes | Children predating their parent or born after its exit are skipped |
| Deeper generations | Grandchild after root exit but during immediate parent's lifetime is handled; grandchild predating immediate parent is skipped |
| Context preservation | Caller handle, PID, depth and timestamps remain byte-for-byte unchanged |
| Read-only traversal | Both nodes visited, no termination, child opened without PROCESS_TERMINATE |
| Snapshot/enumeration/termination failure | Nonzero aggregate result, error event and no leaked fixture handles |
| Depth bound | 130 descendant levels; only depths 0 through 127 acted on, deeper subtree skipped |
| Thread signalling | Current owner and held thread handle verified; wrong owner, later birth, denial, query failure, exited thread/process and missing API skipped |
| Window signalling | Process must be live; changed owner between messages prevents the second post |
| Console signalling | Exit before attachment or between attachment and delivery prevents broadcast; acquired attachment and handler state restored |
| Service ownership | Existing launch harness checks root is still open inside tree cleanup and closed exactly once afterward |

Console/window/thread messages are intercepted, so these deterministic cases
cannot signal unrelated applications. They establish behavior at controlled
boundaries, not atomic safety for foreign HWNDs or isolated Ctrl+C delivery.

`nssm.process_live` starts seven copies of the regression executable: a root,
two children and four grandchildren. They are detached from consoles, report
their PIDs/parent PIDs through an inherited pipe, and wait on a fixture-owned
event with a 30-second fallback. Test wrappers filter real snapshot entries to
reported fixture PIDs, preserving their actual parent relationships. Production
OpenProcess and TerminateProcess calls are independently guarded by that PID
allowlist. The test creates no service and sends no real console, window or
thread messages. Production enumeration itself remains system-wide.

An initial x86 Release CI run exposed the fixture's use of an unrestricted
host snapshot: its ownership guard rejected a non-fixture PID during traversal,
and handle counts also differed. Filtering the fixture's snapshot input avoids
depending on unrelated host processes and stale parent-PID entries; the
process-opening and termination guards remain strict. Fake-process tests cover
rejected and reused candidates without inspecting unrelated live processes.
Both identity groups subsequently passed 20 consecutive x86 Release runs.

Read-only traversal sees all seven without terminating them. Cleanup is then
tested with the root running and with the root already terminated while its
original handle remains open. All seven process handles become signalled.
Caller identity and handle counts remain stable. A local x64 Release run had
97 handles before and after the measured cases, after first-launch warm-up;
host counts can differ. Failure cleanup releases the gate and uses only owned
process handles. The group has a 60-second CTest timeout; deterministic cases
retain the standard 20-second timeout.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All sixteen CTest groups passed |
| x64 Debug | All sixteen CTest groups passed |
| x86 Release | All sixteen CTest groups passed |
| x86 Debug | All sixteen CTest groups passed |
| x64 AddressSanitizer | process_identity, process_live, launch and hook_live passed |
| MSVC `/analyze` Release | Build completed; existing diagnostics retained |

The analyzer reports a large stack frame in `print_process`, plus existing
service warnings for large stack frames, ignored API results, critical-section
initialization and an uninitialized enumeration result. Other translation
units retain registry, event, GUI, settings, hook and logger diagnostics.
The separately reviewed debug-token initialization issue remains scheduled as
NSSM-028/B08. This is not an analyzer-clean claim; no warning was suppressed
as part of B07.

Use the [building instructions](../building.md) for the four-configuration
matrix. Targeted sanitizer validation is reproducible with:

```powershell
cmake -S . -B build/b07-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b07-asan --config Release --parallel
# Use a VS developer shell or add the toolset's x64 ASan DLL directory to PATH.
ctest --test-dir build/b07-asan -C Release -R '^nssm.(process_identity|process_live|launch|hook_live)$' --output-on-failure
```

The separate analysis build uses `-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`.
Production sources retain CRLF. The new skip event is appended to the UTF-16
message file without changing prior message definitions or numeric IDs.
