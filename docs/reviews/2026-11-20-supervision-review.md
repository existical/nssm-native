# Service supervision and configuration review

Baseline `377333b` on `review/beta4-followup`. This pass followed service startup and shutdown, hook ownership, process identity, configuration and account export, the GUI, log rotation, console I/O and the release verification path.

| Impact | Baseline location | Finding |
| --- | --- | --- |
| High | `src/hook.cpp:135` | Shutdown applies the whole hook wait allowance to each worker. Several slow workers can keep the service in STOP_PENDING longer than the CLI allows. |
| High | `src/hook.cpp:330`, `src/process.cpp:76` | A failed process creation-time query at launch can permanently prevent identity validation during cleanup. Hook launch even substitutes the current wall clock for the missing creation time. |
| High | `src/gui.cpp:222` | The editor ignores failed writes while populating application, argument, directory, logon, I/O and description fields. An unrelated edit can commit empty or shortened text. |
| Medium | `src/settings.cpp:1094` | ObjectName export recognizes a virtual account by a domain prefix alone. A clone can substitute the target's virtual account for a different source identity. |
| Medium | `tests/package.ps1:94` | A binary archive's architecture, build configuration and version labels can agree across manifests while disagreeing with the executable. The verifier runs the version command but compares it only to the recorded text. |

## Implementation plan

- [x] Give the hook list one monotonic wait budget. Poll remaining handles at expiry and retain unfinished workers. Test elapsed time, completed workers and a later retry.
- [x] Recover a missing creation timestamp from the original owned process handle at cleanup. Keep the existing PID and creation-time checks for all tree operations. Exercise transient and persistent failures.
- [x] Verify that the edit dialog loaded the complete text of each persisted field it displays before permitting a save. Exercise failed and partial writes on hidden Windows controls and retain native-service tab behavior.
- [x] Use the actual source service name when classifying its account during export. Cover renamed clones, ordinary accounts with similar names and the source service's own virtual account.
- [x] Compare executable version resources and reported build flavor with both manifests. Verify a clean candidate and reject deliberately inconsistent package metadata.
- [x] Review the final diff, run the four Windows build and test configurations, production static analysis, x64 AddressSanitizer and the Windows 11 integration suite. Record results and coverage limits.

## Verification

Tested implementation `6f6050855800724dc8b048dde6cca8aeb04b03d1`. The x64 Release binary sent to the Windows 11 guest had SHA-256 `a5dba95f7b1268be5a552c143bb42dcdf786cf973570d42a3f42567a5eb66c40`.

| Check | Result |
| --- | --- |
| Before and after | Ran the new hook lifecycle, process identity, account export, GUI retry and package rejection cases against an archived copy of baseline `377333b`. All five groups failed on the expected checks. All five passed on the implementation. |
| Clean-source matrix | All 37 test groups passed in each of x86 and x64, Debug and Release, for 148 passes. |
| Compiler analysis and memory checks | Fresh x86 and x64 production Release builds produced zero MSVC analysis warnings. All 37 test groups passed under x64 Release AddressSanitizer. |
| Packaging | The verifier accepted the new candidate and the preceding candidate. It rejected wrong architecture and Debug/Release labels, and rejected a rehashed archive whose two manifests agreed on an incorrect product version. |
| Windows 11 x64 integration | Export, dependencies, controls, lifecycle and legacy upgrade/rollback passed. The guest confirmed virtual-account translation on a real service. Three additional lifecycle runs passed, and the guest reported zero leftover fixture services. |

The GUI tests used real hidden Windows dialogs. Hook wait durations and failed process-time queries were exercised with deterministic fixtures. Live service coverage used one Windows 11 x64 guest. Logs, regression comparisons and JUnit reports are under `build/supervision-review-acceptance/`.
