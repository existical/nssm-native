# NSSM post-implementation review

Reviewed production revision `395c7b98b5f1c00d6df30c23e688cd2573f4b892` against baseline `012cd5f`. All 31 numbered findings and both follow-ups have implementation changes. The five original probes, all 148 standard Windows matrix tests and all five live Service Control Manager (SCM) suites passed. The [original review](2026-11-19-code-review.txt) and [fix register](../fixes/november-supervision-and-configuration.md) give the evidence for each issue.

## Findings from reviewing the changes

| Area | What the review found and how it was checked |
| --- | --- |
| GUI configuration | The rotation tab can represent only the low 32 bits of a size threshold. An untouched field containing the dialog's default zero could overwrite a larger threshold. The save path now checks whether the user edited the field. A regression supplies a nonzero high word, leaves the field alone and verifies both words in the configuration passed on for saving. The GUI also retains the raw application directory when reading configuration for editing. |
| Counted configuration strings | The multiline-to-`REG_MULTI_SZ` conversion could read past the caller's character count. The conversion bounds its reads, handles a trailing carriage return and produces a double terminator. The regression supplies counted input without a trailing NUL. |
| Service control and hooks | The review traced terminal status ownership through competing exit and STOP paths. Tests pause an Exit/Suicide decision while delivering STOP and hold Rotate/Pre while STOP is processed. Rotation records the launch generation at dispatch, checks it before acting and waits for each participating logger. Shutdown wakes a rotation waiting on a quiet stream. |
| Log file ownership and errors | The review checked the second open of an aliased file, missing inherited streams and reopening the active file after a rename. A separate filesystem test permits an offline archive copy and denies the subsequent write-open. It checks the failure result and the retained active bytes. |
| Compiler and package metadata | MSVC identified a signed argument passed to an unsigned hook-counter format. The call now supplies the expected type. The package manifest also omitted the new fix IDs. All four final manifests include the 71 registered IDs through `NSSM-071`. |

## Verification

| Check | Result |
| --- | --- |
| x64 Release and Debug | 37/37 passed in each configuration. |
| x86 Release and Debug | 37/37 passed in each configuration. |
| x64 AddressSanitizer | 37/37 passed. |
| Repeated x64 Release tests | Ten runs each of `nssm.launch`, `nssm.logger`, `nssm.control_waits` and `nssm.log_bytes` passed. These exercise timing and byte preservation with deterministic fixtures and real child processes. |
| MSVC static analysis | The analysis build completed. Warnings remain for large stack frames, shadowed variables, API results and annotations, and a possible uninitialized hook handle. The handle is assigned after a successful thread creation and published under the hook lock. The counted-string bounds and hook format warnings no longer appeared. |
| Package verification | Built from a clean Git source archive. The verifier checked SHA-256 sums, unpacked each executable and symbol package, ran each executable's version command and matched the source archive, binary versions and recorded test results. |

The production x64 Release probes produced the following observations.

| Original probe | Observed after the change |
| --- | --- |
| Conditional environment removal | Removing a different value retained `MODE=production`. |
| Redirecting stdout alone | The child startup data enabled `STARTF_USESTDHANDLES` and supplied non-null stdin and stderr handles. |
| Two paths to one output file | The file contained exactly the eight bytes `AAAABBBB`. |
| Online rename with `OPEN_EXISTING` | The logger worker was still running and the active log path existed after rotation. The committed byte-level test also verifies continued writes. |
| Inherited literal environment value | Restoring the snapshot preserved the percent references literally. |

Read-only CLI checks also covered a native service, a driver, and service display-name resolution. Dumping EventLog returned an error without emitting an install command. Reading the Null driver's account returned success with an empty value. Both Print Spooler names resolved to the same service and PID. The process listing then returned an access error under the local token. The elevation regression captured a real child exit code of 43 through an injected shell launcher. The interactive UAC prompt remains untested.

## Live SCM and upgrade results

On 19 November 2026, an elevated Windows 11 Pro VM (build 26300) ran the five repository fixtures under CTest 4.4.3. The candidate executable came from the verified x64 Release package. Its SHA-256 was `9170d04b19acb868006fe160dc0e4d6de08e5eabe36300fd47c66ffda8e907f9`. The legacy executable was the pinned rebuild of `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, SHA-256 `ee78cef9d582a8655b33f826df359a810ba532278b67e40e7389124ee171b796`. Both executable hashes were checked on the VM before testing.

| Test | Result | Elapsed |
| --- | --- | ---: |
| `nssm.export_live` | Passed wrapper repair, query-only export, clone naming, quoted command replay and native environment updates. | 32.06 s |
| `nssm.dependencies_live` | Passed dependency edits and export/import across four disabled fixture services. | 0.92 s |
| `nssm.controls_live` | Passed CLI control checks on normal, stalled and failing services. | 10.02 s |
| `nssm.lifecycle_live` | Passed start, crash, restart and stop with a real wrapped application, hooks and child processes. | 10.23 s |
| `nssm.upgrade_live` | Passed legacy → candidate → legacy replacement at the same service and executable path. Configuration, persistent worker state and log tails survived the upgrade and rollback. | 41.29 s |

The lifecycle fixture then passed ten consecutive runs with CTest's `--repeat until-fail:10`. In run order, the durations were 9.77, 9.52, 9.23, 9.19, 9.24, 9.52, 9.21, 9.59, 9.63 and 9.65 seconds. Each run exercised the wrapped application's crash and restart, two operator stops, quoted hooks, log tails and descendant cleanup.

Comparing service names before and after both runs found zero new fixture services. The local evidence directory `build/vm-acceptance-395c7b9/` contains JUnit XML and the complete CTest logs for the five suites and the ten repetitions.

## Operational limits

- **Configuration write failures.** An edit that fails partway through optional settings can leave earlier successful changes in place. The dialog reports the failure and keeps the entered values for retry. Restoring earlier mandatory values also requires continued registry write access.
- **Quiet log streams.** Manual rotation takes place at a line boundary. Rotate/Post can wait indefinitely if a participating stream stays open and silent. Concurrent rotation requests coalesce, and shutdown cancels the outstanding wait.
