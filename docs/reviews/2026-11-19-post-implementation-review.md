# NSSM post-implementation review

Reviewed production revision `395c7b98b5f1c00d6df30c23e688cd2573f4b892` against baseline `012cd5f`. All 31 numbered findings and both follow-ups have implementation changes. The five original probes now pass, and all 148 tests in the standard Windows build matrix passed. Acceptance under the Windows Service Control Manager (SCM), including upgrade and rollback, remains open. The [original review](2026-11-19-code-review.txt) and [fix register](../fixes/november-supervision-and-configuration.md) give the evidence for each issue.

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

## Remaining validation and operational limits

- **Live SCM and upgrade.** The repository requires an elevated disposable Windows host for `nssm.export_live`, `nssm.dependencies_live`, `nssm.controls_live`, `nssm.lifecycle_live` and `nssm.upgrade_live`. This workstation's token is non-administrative. The open checklist entry covers all five suites, the pinned legacy executable and the ten lifecycle repeats. The [Windows workflow](../../.github/workflows/ci.yml) specifies the run.
- **Configuration write failures.** An edit that fails partway through optional settings can leave earlier successful changes in place. The dialog reports the failure and keeps the entered values for retry. Restoring earlier mandatory values also requires continued registry write access.
- **Quiet log streams.** Manual rotation takes place at a line boundary. Rotate/Post can wait indefinitely if a participating stream stays open and silent. Concurrent rotation requests coalesce, and shutdown cancels the outstanding wait.
