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

- [ ] Give the hook list one monotonic wait budget. Poll remaining handles at expiry and retain unfinished workers. Test elapsed time, completed workers and a later retry.
- [ ] Recover a missing creation timestamp from the original owned process handle at cleanup. Keep the existing PID and creation-time checks for all tree operations. Exercise transient and persistent failures.
- [ ] Verify that the edit dialog loaded the complete text of each persisted field it displays before permitting a save. Exercise failed and partial writes on hidden Windows controls and retain native-service tab behavior.
- [ ] Use the actual source service name when classifying its account during export. Cover renamed clones, ordinary accounts with similar names and the source service's own virtual account.
- [ ] Compare executable version resources and reported build flavor with both manifests. Verify a clean candidate and reject deliberately inconsistent package metadata.
- [ ] Review the final diff, run the four Windows build and test configurations, production static analysis, x64 AddressSanitizer and the Windows 11 integration suite. Record results and coverage limits.
