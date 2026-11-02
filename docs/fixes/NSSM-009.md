# NSSM 009 Process identity during cleanup

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 9 in the 2026 November 2 review |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `9a5a14549b1837ba518ed577905aaafe04236d66` |
| Evidence | Deterministic PID replacement and ownership assertions; isolated live descendant trees |
| Fix PR | [PR 12](https://github.com/existical/nssm-native/pull/12) |
| Implementation commit | `329738b85deb0f454ca6cde1e1882d535530f48e`; merged at `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| First release | Not yet released |

The pre-fix `src/process.cpp` is identical to `process.cpp` in the checked
legacy source. `check_parent` opened a child, checked its creation time, then
closed that handle. Recursion opened the PID again for termination. If the
original process exited and its PID was reused in that gap, the new process
could be terminated. The root was also reopened by PID; `end_service` had
already closed its original handle before starting descendant cleanup.
Recursion compared every generation against the root's timestamps, and
overwrote the caller's handle field with a subsequently closed handle.

The initial deterministic regression failed four assertions: the root was
reopened, the caller's handle changed, a replacement child was terminated,
and a root with no retained handle still triggered termination by PID.
These were simulated objects and API schedules. No unrelated real process
was killed, and no historic binary was executed for this reproduction.

The implementation in [process.cpp](../../src/process.cpp) now:

- Borrows the original root handle and validates its PID and nonzero creation
  time before acting. It never falls back to reopening a missing root by PID.
- Opens each candidate child once, retains that handle through recursion,
  and keeps every ancestor handle until its descendants have been considered.
  A fresh snapshot taken while the child is held confirms its parent PID,
  including replacement cases with equal timestamps.
- Checks creation times against the immediate parent's actual lifetime and
  the parent's pre-snapshot time. A grandchild born after the root exited can
  still qualify when its own parent was alive at its birth.
- Uses local traversal contexts, preserving the caller's handle, PID, depth
  and timestamps. Read-only traversal does not request termination rights.
- Skips unverifiable processes and their subtrees, closes owned handles on
  failure paths, and continues with other candidates where possible. Depth
  128 and deeper is skipped, bounding recursive frames and held snapshots.

Keeping a process or thread handle open retains the object and its identifier;
this is the basis for using the same handle from validation through action.
See [Microsoft's object-lifetime explanation](https://devblogs.microsoft.com/oldnewthing/20150918-00/?p=91561).
Snapshots remain observations of parent relationships, not a security boundary
against deliberately assigned parent attributes.

[service.cpp](../../src/service.cpp) retains the service's root handle through
cleanup and closes it afterward. The `processes` command also closes its
borrowed root handle on success, time-query failure and allocation failure.
Tree helpers return an aggregate failure count, zero when no failure is
observed. The CLI returns nonzero on traversal failure. Service/hook cleanup
keeps its existing outward result conventions and reports failures through
events; a successful stop or hook result does not certify every descendant
exited. Successful `TerminateProcess` initiation is not itself an exit wait.

The appended `NSSM_EVENT_PROCESS_TREE_SKIPPED` message identifies rejected
PIDs and validation/depth errors. Open, query, enumeration and termination
failures retain their existing events. Existing message IDs and UTF-16
resource encoding are preserved, with the new message in all three resource
languages. Registry settings and stop-method ordering are unchanged.

Console/window signalling first validates the retained process handle and
checks that it is running. Console delivery rechecks after attachment and
restores attachment/handler state when the process has exited. Each thread
candidate is opened and held while its current owner, birth time and running
state are checked before posting `WM_QUIT`.

There are explicit compatibility limits:

- `GetProcessId` and `GetProcessIdOfThread` are resolved dynamically. Missing
  process identity support causes cleanup/traversal to skip safely; missing
  thread ownership support disables that signalling method. No new hard
  import or supported-old-Windows claim is introduced. Actual validation was
  on the modern Windows/toolchain versions in the validation record.
- Foreign HWND ownership is rechecked before each message, including after
  `WM_CLOSE`. Windows provides no atomic owner-check-and-post operation: a
  window can still be recycled between the check and message. This remaining
  race is documented in [Microsoft's IsWindow reference](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindow).
- Ctrl+C retains its shared-console broadcast behavior. Other processes
  sharing that console can receive it; process identity checks do not change
  [GenerateConsoleCtrlEvent's delivery scope](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent).
- Descendants born after a snapshot, inaccessible or already-disappeared
  intermediate processes, clock changes affecting timestamp comparisons, and
  the depth cap can leave descendants running. This is best-effort cleanup,
  not a containment mechanism. Job Objects and service-callback concurrency
  remain separate work.

Live test snapshots are restricted to reported fixture PIDs before the walker
sees them; OpenProcess and TerminateProcess retain independent ownership
guards. This isolates tests from unrelated host processes and stale parent-PID
entries. Production enumeration remains system-wide.

See [deterministic tests](../../tests/process_tree.cpp),
[owned live trees](../../tests/process_live.cpp),
[service ownership assertions](../../tests/launch.cpp) and
[B07 validation](../reviews/b07-validation.md) for evidence and limits.
