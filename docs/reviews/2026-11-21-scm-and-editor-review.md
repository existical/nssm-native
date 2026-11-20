# Service status, process listing and configuration review

Reviewed the service lifecycle and CLI, process enumeration, hooks, logging, registry and configuration, GUI, and the build and test entry points on `review/beta4-followup` at `193614a`.

## Findings and implementation checklist

- [x] **Process listing can attribute a reused PID to the wrong service.** Query the SCM for a state with a valid PID, open the process, and query the same service handle again before walking the tree. Return an empty listing for an already stopped service and a retryable error when identity changes. Exercise stale, changing, and valid snapshots.
- [x] **Stable service status retains transition progress.** Clear checkpoints and wait hints when reporting running, paused or stopped, and clear accepted controls on stop. Publish progress only during pending states. Test stable reports and pending waits. See the [SERVICE_STATUS contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status) and [SCM process ID contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex).
- [x] **A service start argument of `debug` blocks handler registration indefinitely.** Remove the private debugger spin loop and exercise the argument through service startup. The SCM must be able to register the handler regardless of the first start argument.
- [x] **Saving the GUI can clear an I/O path if its edit control is missing.** Use the checked text reader for all three stream paths. Exercise missing controls and deliberate clearing of a populated path.
- [x] **CLI installation accepts an explicitly empty executable until after service creation.** Reject it before contacting the SCM and cover the distinction between an omitted path (interactive installation) and an explicitly empty path.
- [x] **Verify the changes.** Run the Windows build and test matrix, check the candidate package, exercise the release on the Windows VM, and review the final diff against each finding.

## Verification

The candidate was built from implementation commit `6c7eea1`.

| Check | Result |
| --- | --- |
| MSVC x86 and x64, Debug and Release | 37 tests passed in each of four builds. No compiler warnings in the build logs. |
| MSVC static analysis | Zero warnings for the x86 and x64 Release binaries. |
| x64 Release with AddressSanitizer | All 37 tests passed. |
| Candidate package | Checksums, executable and symbol archives, JUnit reports, and source identity verified. |
| Elevated Windows 11 VM, x64 Release | All five suites passed, including controls, export, dependencies, wrapped-service lifecycle, and upgrade from the pinned legacy build. Three further lifecycle runs passed. No new fixture services remained. |

The process identity tests supply deterministic SCM transitions and open a real process handle. Live lifecycle tests also check a stopped service's empty listing and successful startup with the `debug` argument. Final diff review found no additional issue in the changed paths.
