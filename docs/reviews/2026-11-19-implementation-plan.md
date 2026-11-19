# NSSM review implementation plan

Baseline commit `012cd5f`. Numbers in parentheses refer to [the review](2026-11-19-code-review.txt). Complete each step after checking its production path and regression coverage.

## Configuration and CLI

- [ ] Validate DWORD types and lengths in a shared reader. Reset priority and hook redirection before every reload. Exercise missing, malformed and valid replacements (24, 25).
- [ ] Separate verbatim environment restoration from configured value expansion. Handle allocation failures, distinguish key-only removal from exact value removal and request query access for native read-modify-write operations. Check empty values and overlapping names (18, 19, 26).
- [ ] Preserve GUI logging options without controls and raw I/O strings and types on unrelated edits. Remove the virtual-account overread. Check install and edit paths (5, 13, 17).
- [ ] Keep existing Parameters on failed edits, propagate native, optional, environment and hook write errors to the dialog, and make partially completed installation recoverable. Exercise write failures (15, 16).
- [ ] Reject clearing Application. Distinguish missing, invalid and inaccessible wrapper settings from native services by checking the SCM image. Permit repair of a missing Application and reject incomplete dumps (14, 29).
- [ ] Make driver account reads null-safe and resolve service display names for process listings. Check native service and driver handling (27, 28).
- [ ] Reject unsupported native exports before emitting commands, quote the wrapper executable once, and omit a conflicting display name on same-host clones. Check exported command lines (20–22).
- [ ] Wait for elevated commands, return the child exit code and restrict elevation fallback to eligible operations. Verify error and success returns (23).

## Logging

- [ ] Resolve relative redirections against AppDirectory without temporarily changing the process directory. Recognize file aliases before a second destructive open and share the writer. Check distinct files, aliases and shared streams (6, 8).
- [ ] Supply all three inheritable standard handles for partial redirection and release owned handles on failures. Check single-stream redirection and hook inheritance (9).
- [ ] Recreate the active log after online rename regardless of initial disposition. Propagate offline copy/truncate failures. Exercise rotation and continued writes (31 and offline follow-up).
- [ ] Tie Rotate/Post to completed manual rotation. Account for both output streams, idle writers, coalescing, shutdown and rotation failure. Preserve the asynchronous dispatcher contract (30).

## Supervision

- [ ] Differentiate failed restart attempts from terminal shutdown. Make retry delays interruptible and prevent an unmonitored process being reported as running. Test retry recovery, cancellation and wait-registration failure (1, 7).
- [ ] Assign terminal status to a single owner. Confirm root exit after forced termination, propagate failure and preserve explicit stop-method opt-outs. Exercise natural and requested stops (2, 3).
- [ ] Move slow Stop/Pre and Rotate/Pre work out of the SCM dispatcher while retaining ordering and a bounded lifetime. Check overlapping controls (4).
- [ ] Synchronize continue/throttle state, permit the start-pending resume transition, separate configured delays from crash backoff and preserve exit code 259 for a signalled process. Exercise delay and exit-action selection (10–12).

## Verification

- [ ] Check CLI hook reads and dumps for raw registry value and type preservation (hook follow-up).
- [ ] Add focused regression coverage at the existing test entry points. Build and run the x64 and x86 Debug and Release presets, run static analysis, and exercise applicable Windows integration tests. Record the exact results and any environmental limitations.
- [ ] Review the final diff afresh for configuration loss, resource ownership, races and command compatibility. Fix and retest any issues found, record remaining risks and commit the completed checklist and changes.
