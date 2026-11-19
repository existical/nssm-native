# NSSM review implementation plan

Baseline commit `012cd5f`. Numbers in parentheses refer to [the review](2026-11-19-code-review.txt). Complete each step after checking its production path and regression coverage.

## Configuration and CLI

- [x] Validate DWORD types and lengths in a shared reader. Reset priority and hook redirection before every reload. Exercise missing, malformed and valid replacements (24, 25).
- [x] Separate verbatim environment restoration from configured value expansion. Handle allocation failures, distinguish key-only removal from exact value removal and request query access for native read-modify-write operations. Check empty values and overlapping names (18, 19, 26).
- [x] Preserve GUI logging options without controls and raw I/O strings and types on unrelated edits. Remove the virtual-account overread. Check install and edit paths (5, 13, 17).
- [x] Keep existing Parameters on failed edits, propagate native, optional, environment and hook write errors to the dialog, and make partially completed installation recoverable. Exercise write failures (15, 16).
- [x] Reject clearing Application. Distinguish missing, invalid and inaccessible wrapper settings from native services by checking the SCM image. Permit repair of a missing Application and reject incomplete dumps (14, 29).
- [x] Make driver account reads null-safe and resolve service display names for process listings. Check native service and driver handling (27, 28).
- [x] Reject unsupported native exports before emitting commands, quote the wrapper executable once, and omit a conflicting display name on same-host clones. Check exported command lines (20–22).
- [x] Wait for elevated commands, return the child exit code and restrict elevation fallback to eligible operations. Verify error and success returns (23).

## Logging

- [x] Resolve relative redirections against AppDirectory without temporarily changing the process directory. Recognize file aliases before a second destructive open and share the writer. Check distinct files, aliases and shared streams (6, 8).
- [x] Supply all three inheritable standard handles for partial redirection and release owned handles on failures. Check single-stream redirection and hook inheritance (9).
- [x] Recreate the active log after online rename regardless of initial disposition. Propagate offline copy/truncate failures. Exercise rotation and continued writes (31 and offline follow-up).
- [x] Tie Rotate/Post to completed manual rotation. Account for both output streams, idle writers, coalescing, shutdown and rotation failure. Preserve the asynchronous dispatcher contract (30).

## Supervision

- [x] Differentiate failed restart attempts from terminal shutdown. Make retry delays interruptible and prevent an unmonitored process being reported as running. Test retry recovery, cancellation and wait-registration failure (1, 7).
- [x] Assign terminal status to a single owner. Confirm root exit after forced termination, propagate failure and preserve explicit stop-method opt-outs. Exercise natural and requested stops (2, 3).
- [x] Move slow Stop/Pre and Rotate/Pre work out of the SCM dispatcher while retaining ordering and a bounded lifetime. Check overlapping controls (4).
- [x] Synchronize continue/throttle state, permit the start-pending resume transition, separate configured delays from crash backoff and preserve exit code 259 for a signalled process. Exercise delay and exit-action selection (10–12).

## Verification

- [x] Check CLI hook reads and dumps for raw registry value and type preservation (hook follow-up).
- [x] Add focused regression coverage at the existing test entry points. Build and run the x64 and x86 Debug and Release presets, run static analysis, and exercise applicable Windows integration tests. Record the exact results and any environmental limitations.
- [x] Review the final diff afresh for configuration loss, resource ownership, races and command compatibility. Fix and retest issues found. Record the results and remaining risks in the [post-implementation review](2026-11-19-post-implementation-review.md).
- [x] Run the five opt-in Service Control Manager suites on an elevated disposable Windows host, including the pinned legacy upgrade and rollback test. All five suites and ten additional lifecycle runs passed on the Windows 11 VM. A before/after service inventory confirmed fixture cleanup. See the [live SCM results](2026-11-19-post-implementation-review.md#live-scm-and-upgrade-results).

## Commits

| Commit | Work |
| --- | --- |
| `fb1020d` | Recorded the original findings and committed this plan before implementation. |
| `d2fb5a9` | Implemented the 31 findings and two follow-ups, fixed a bounds issue found during implementation and added regression coverage. The [fix register](../fixes/november-supervision-and-configuration.md) maps each issue to its checks. |
| `b2f8d7c` | Preserved the full rotation threshold and raw application directory on GUI edits. Exercised shutdown races and offline truncation failure. |
| `395c7b9` | Corrected hook counter formatting and candidate package fix IDs. This is the source revision used for the final package and test matrix. |
