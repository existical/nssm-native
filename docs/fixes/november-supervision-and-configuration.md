# November supervision and configuration fixes

The reviewed baseline is `012cd5fa55197970ad8a75dae888a625cbefb791`. Findings 1–6 had high priority because they affected availability, control handling or persisted data. The following register assigns permanent IDs to the numbered findings, the two follow-ups and an additional bounds issue found during implementation. All 34 entries first shipped in [v2.25.0-beta.2](../releases/v2.25.0-beta.2.md). The [publication record](../releases/v2.25.0-beta.2-publication.json) identifies the verified public downloads.

| ID | Original finding | Trigger and corrected behavior | Verification path |
| --- | --- | --- | --- |
| NSSM-038 | 1 | When the executable temporarily disappears during relaunch, NSSM cleans up the failed attempt and retries. STOP wakes the retry wait. | Injected launch failure and STOP during retry |
| NSSM-039 | 2 | Natural exit and operator STOP compete for shutdown ownership. The owner publishes the terminal status once. | Gated Exit/Suicide versus STOP and repeated stop |
| NSSM-040 | 3 | Forced termination now waits for the process handle. An unconfirmed shutdown returns a service-specific error. Explicit stop-method opt-outs remain supported. | Process identity, live process and injected shutdown failure |
| NSSM-041 | 4 | STOP signals the service-main thread. ROTATE queues a worker. Slow pre-hooks execute outside the dispatcher, and rotation releases the hook lock while observing its worker. | Gated Rotate/Pre versus STOP, hook lifecycle and live lifecycle fixtures |
| NSSM-042 | 5 | An unrelated GUI edit preserves sharing, creation dispositions, file flags, copy/truncate and the rotation thresholds and delay. | GUI edit fixture |
| NSSM-043 | 6 | Separate names for one existing file share a writer. NSSM compares the files before offline rotation and checks again after creating stdout. | Aliased stdout/stderr writes |
| NSSM-044 | 7 | NSSM registers the child exit monitor before reporting RUNNING. A registration error follows the shutdown path. | Failed wait registration |
| NSSM-045 | 8 | Runtime redirection resolves relative file names against the application directory without changing the wrapper's working directory. | Relative and aliased redirection |
| NSSM-046 | 9 | Partial application and hook redirection now supplies three inheritable handles. Missing streams use a usable inherited handle or NUL. | Standard handle and real child fixtures |
| NSSM-047 | 10 | CONTINUE signals an event, retaining a wake that arrives just before the wait. The CLI follows CONTINUE_PENDING through START_PENDING. | Gated resume and simulated SCM |
| NSSM-048 | 11 | A stable application uses its configured restart delay. Exponential backoff applies to short-lived launches. | Measured wait argument for a 37 ms delay |
| NSSM-049 | 12 | A process exit notification for code 259 selects that code's exit action. | Simulated signalled child and registry exit rule |
| NSSM-050 | 13 | The editor displays raw I/O paths and preserves their registry types. An unreadable input control fails the save. | GUI and typed registry fixtures |
| NSSM-051 | 14 | Clearing Application fails. Run the wrapper executable registered with SCM to restore a missing Application or Parameters key with the CLI. | Mandatory-setting regression and live repair fixture |
| NSSM-052 | 15 | A mandatory write failure restores earlier launch values from the same save. Unrelated values and subkeys survive. | Injected second-write failure |
| NSSM-053 | 16 | Description, options, environment and hook writes report failure. The edit dialog keeps unsaved input. Installation rolls back the newly created service after hook failure and reports any failed rollback. | Install ownership, GUI retry and registry failures |
| NSSM-054 | 17 | Saving a virtual account borrows the already terminated username for the SCM call. | Account handling, installation and source review |
| NSSM-055 | 18 | Native Environment append/remove operations request registry query and set access together. | Live export/configuration fixture |
| NSSM-056 | 19 | Environment removal matches names case-insensitively and compares the entire case-sensitive value when supplied. An empty specified value matches only an empty value. | Environment regression |
| NSSM-057 | 20 | Dumping a native service returns an error before printing an install command. | Read-only native CLI check |
| NSSM-058 | 21 | Export quotes the executable once, allowing an executable path containing spaces. | Command quoting and dump fixtures |
| NSSM-059 | 22 | A clone dump omits DisplayName so the target can use its own name. A same-name backup retains a custom display name. | Live export fixture |
| NSSM-060 | 23 | The UAC parent passes the raw executable path, waits and returns the child exit status. Only interactive edit uses the edit-family elevation fallback. | Injected shell launch, cancellation, missing handle and real child exit status |
| NSSM-061 | 24 | DWORD readers validate the type and exact length before publishing the number. Invalid priorities revert to the default at runtime. | Registry and numeric regressions |
| NSSM-062 | 25 | Reload starts priority and hook-sharing settings from their defaults, including after a registry value is removed. | Two reads on one service object |
| NSSM-063 | 26 | The wrapper restores inherited environment strings literally and clears variables using a writable copy. A missing initial snapshot fails startup. | Literal percent-variable restoration |
| NSSM-064 | 27 | Driver account queries handle a missing SCM account name. | Read-only driver CLI check |
| NSSM-065 | 28 | A display-name lookup translates directly to a key name using a CONNECT manager handle. Lookup errors propagate to callers requiring canonical names. | Process listing and dependency tests |
| NSSM-066 | 29 | Missing, unreadable and malformed Parameters no longer produce successful incomplete backups. | Registry error paths and incomplete-wrapper export |
| NSSM-067 | 30 | Rotate/Post waits for every participating logger's successful manual rotation. Repeated controls coalesce. A quiet stream waits for its next line boundary, and teardown cancels the request. | Two-stream completion and cancellation |
| NSSM-068 | 31 | After online rename, NSSM opens or creates the active log for continued output, including with OPEN_EXISTING or TRUNCATE_EXISTING at startup. | Exact archive and active-file bytes |
| NSSM-069 | Offline follow-up | Offline copy/truncate records success only after copying, reopening the existing active file and truncating it successfully. | Real file denying write-sharing after a successful archive copy |
| NSSM-070 | Hook follow-up | Hook get/dump reads raw commands. Export uses the selector `REG_SZ:Start/Pre` for a literal Start/Pre hook. The ordinary selector retains expandable semantics. Use the updated CLI to replay a literal-hook export. | Typed hook reads, writes and dump output |
| NSSM-071 | Implementation review | Conversion of multiline configuration input now bounds every read by the supplied character count, skips blank lines and returns a double-terminated block. | Counted input ending in CR without a trailing NUL |

The [original review](../reviews/2026-11-19-code-review.txt) gives the affected paths, evidence and operator impact. The [implementation checklist](../reviews/2026-11-19-implementation-plan.md) tracks the work. The [verification review](../reviews/2026-11-19-post-implementation-review.md) records which checks ran.
