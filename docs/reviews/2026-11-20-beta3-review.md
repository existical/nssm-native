# Beta.3 code review and implementation plan

Reviewed `3c8da5571941dd0f490b660dd9d9c30a420bda7d` on 20 November 2026. The review followed service startup, stop and wait handling, hooks, process-tree and account management, registry and environment settings, GUI editing, stdout/stderr rotation, command dispatch and package verification. The following items record concrete failure paths found in that source.

- [x] **Environment edits.** Removing the last entry from a replacement `AppEnvironment` reaches the generic empty-string deletion branch. The next child then inherits the wrapper environment. Preserve a present, double-NUL empty replacement until an explicit reset. Reject malformed additions before they can replace a longer key by prefix. Exercise replacement, append and reset on a private registry fixture.
- [x] **Launch and hook environments.** Configured environment expansion and assignment failures are dropped before both child launches. Failed expansion can substitute unexpanded text. Propagate application errors, restore the wrapper baseline on failure and ensure subsequent launches can recover from a failed restore. Verify that a failed environment cannot launch an application or configured hook.
- [x] **Log rotation.** After a failed automatic rotation, the file stays over its size threshold and each following newline immediately retries the archive. Bound automatic retry frequency, keep forwarding output, and let explicit rotate requests run immediately. Exercise repeated failure, retry and success.
- [x] **GUI field integrity.** The application, directory, arguments and description fields accept a shortened prefix when the control text exceeds the destination, and a failed directory read can become an empty setting. Verify the complete input before saving and preserve intentionally blank fields. Cover exact-fit, overflow and failed reads.
- [x] **GUI strings and file browsing.** Rebuilding the hook action list on each text change loses allocated label strings. Tab and combo setup and file browsing also leave allocated messages behind. The browse filter advances by the untruncated label length and only clears half of its Unicode buffer. Give messages bounded lifetimes and construct a complete double-NUL filter before opening the dialog.
- [x] **SCM status and delayed start.** Generic handle waits add each interval to the previous DWORD wait hint, which can wrap or exaggerate the next checkpoint deadline. Publish the next bounded interval plus a margin. GUI install/edit also reports success when a requested delayed start receives `ERROR_INVALID_LEVEL`. Align it with the CLI and verify both outcomes.
- [x] **Registry diagnostics.** Event-source registration leaks its key and reports success after failed value writes. Several registry operations report the thread's last error instead of the status returned by the registry API. Close the key on every path, report the actual failing operation, and test handle balance and write errors.
- [x] **Headless usage output.** A CLI launched without a console can open a modal usage box even when stderr is redirected. Deliver diagnostics to redirected callers and retain the interactive help dialog. Exercise a headless invalid command with a timeout.
- [x] **Package integrity.** The verifier checks files listed in the checksum manifest but accepts missing checksum entries for the manifest, archives and other evidence. Archive paths from the build manifest are opened separately from the validated paths. Require complete checksum coverage and resolve package archives only through validated top-level ZIP names. Verify genuine historical packages and rejection of incomplete packages.
- [x] **Startup checkpoint handoff.** The first live lifecycle run timed out during startup. Windows initially advertised a 2,000 ms wait hint at checkpoint zero. The wrapper also used checkpoint zero when publishing its longer wait hint, so a client already polling could retain the original two-second deadline. Advance the checkpoint when the handler registers and when launch preparation progresses. Cover the SCM-to-wrapper handoff with the real polling logic and rerun the matrix and live suites.
- [x] **Acceptance.** Review the resulting diff, test clean-source x86 and x64 Debug and Release packages, and run the five elevated Windows service suites against the packaged x64 executable. Record the tested commit and any validation limits.

## Verification

Tested implementation commit `8f407e89246bd82e8cf9e79c2d4254a9b5cd07f9`. The packaged x64 Release executable used on Windows 11 had SHA-256 `88ab5ed0c7d9b5a0c9f46684aa7c93d83e1fcd3cecd6a1bf563208e505a8c3c2`.

| Check | Result |
| --- | --- |
| Clean-source x86 and x64, Debug and Release | 37 of 37 groups per configuration, 148 passes total |
| MSVC x64 Release with AddressSanitizer | 37 of 37 groups passed |
| Elevated Windows 11 service suites | Export, dependencies, controls, wrapped lifecycle and upgrade/rollback all passed |
| Repeated lifecycle on the final binary | Three additional consecutive passes, each covering crash, restart, stop, start and descendant cleanup |
| Package verifier | Verified the final source archive, four binary and symbol packages, file inventories, hashes and test evidence. Genuine beta.2 and beta.3 packages also passed. |

The original verifier accepted a copy of beta.3 with the checksum entry for `build-info.json` removed. The corrected verifier rejected the same copy. The Windows CLI also preserved an empty replacement after removing the last variable, rejected a malformed prefix addition without changing the existing value, and restored inheritance on explicit reset.

The first candidate timed out in the lifecycle test. The guest event log placed the cleanup stop about two seconds after startup. Subsequent tracing showed the SCM advertising checkpoint zero and a 2,000 ms hint before the wrapper published a longer hint. The final revision advances the checkpoint as soon as the control handler registers. A deterministic SCM polling test starts before that handoff and waits six seconds for the service to reach Running.

MSVC static analysis completed with 46 warnings, including large stack frames, ignored API results and tick-count arithmetic. The rotation tests exercise the retry delay across a 32-bit tick rollover. The live results cover the tested Windows 11 guest. Down-level delayed-start behavior was exercised with a simulated unsupported SCM configuration level. The GUI regressions used real edit controls and a simulated file picker.

Build manifests, JUnit reports and the guest log are available in the local `build/beta3-review-acceptance` directory. The guest reported zero new fixture services after cleanup.
