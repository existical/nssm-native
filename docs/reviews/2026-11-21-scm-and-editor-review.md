# Service status, process listing and configuration review

Reviewed the service lifecycle and CLI, process enumeration, hooks, logging, registry and configuration, GUI, and the build and test entry points on `review/beta4-followup` at `193614a`.

## Findings and implementation checklist

- [ ] **Process listing can attribute a reused PID to the wrong service.** Query the SCM for a state with a valid PID, open the process, and query the same service handle again before walking the tree. Return an empty listing for an already stopped service and a retryable error when identity changes. Exercise stale, changing, and valid snapshots.
- [ ] **Stable service status retains transition progress.** Clear checkpoints and wait hints when reporting running, paused or stopped, and clear accepted controls on stop. Publish progress only during pending states. Test stable reports and pending waits. See the [SERVICE_STATUS contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status) and [SCM process ID contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex).
- [ ] **A service start argument of `debug` blocks handler registration indefinitely.** Remove the private debugger spin loop and exercise the argument through service startup. The SCM must be able to register the handler regardless of the first start argument.
- [ ] **Saving the GUI can clear an I/O path if its edit control is missing.** Use the checked text reader for all three stream paths. Exercise missing controls and deliberate clearing of a populated path.
- [ ] **CLI installation accepts an explicitly empty executable until after service creation.** Reject it before contacting the SCM and cover the distinction between an omitted path (interactive installation) and an explicitly empty path.
- [ ] **Verify the changes.** Run the Windows build and test matrix, check the candidate package, exercise the release on the Windows VM, and review the final diff against each finding.
