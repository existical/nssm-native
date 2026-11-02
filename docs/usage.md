# Basic usage

Run these commands from the directory containing your built `nssm.exe`. Use an
elevated terminal when installing, removing or changing a Windows service.

```powershell
# Show version and architecture.
./nssm.exe version

# Install a service using the graphical installer.
./nssm.exe install MyService

# Install directly with an executable and its arguments.
./nssm.exe install MyService 'C:\Apps\worker.exe' --config 'C:\Apps\worker.ini'

# Start, inspect and stop it.
./nssm.exe start MyService
./nssm.exe status MyService
./nssm.exe stop MyService

# Open the graphical editor.
./nssm.exe edit MyService

# Remove the service, accepting the removal confirmation.
./nssm.exe remove MyService confirm
```

`nssm start` waits for the Service Control Manager to report `SERVICE_RUNNING`.
Its polling is limited to 60 seconds and can fail sooner if the service stops,
a status query fails, or a checkpoint stalls beyond its wait hint. A missing
wait hint gets a 10-second progress window; hints below one second get one
second. Changing only the hint does not count as checkpoint progress.
`AppThrottle` continues to control restart throttling and does not set this
command's polling deadline. Startup errors, including service-specific exit
codes, are reported with a nonzero command exit status.

A polling timeout does not stop the service; use `nssm status MyService` to check
its current state. `SERVICE_RUNNING` describes the Windows service state and
does not guarantee that the wrapped application's endpoints are ready.

NSSM supports application arguments, startup directories, service accounts,
environment settings, restart policies, output redirection, log rotation and
event hooks.

## Logger shutdown and trailing output

NSSM closes both parent log-pipe writers before waiting for logging workers.
This lets shared stdout/stderr reach EOF and drain trailing output. A hook or
descendant which still holds a writer can delay EOF.

Each worker has a 1500 ms drain wait. If the wait times out or fails, NSSM
signals stop, requests synchronous I/O cancellation when Windows supports it,
and waits another 100 ms. These are per-worker waits, not a total service-stop
deadline. Cancellation is best effort; unread or unwritten trailing output can
be lost after this point.

A worker that remains blocked keeps its own state, read pipe and file until
it can exit. It cannot access freed service data or change a later restart's
rotation state. A driver operation or rotation already in progress can still
finish late and retain resources until completion or NSSM process exit.
See [NSSM-007](fixes/NSSM-007.md) for compatibility and validation details.

## Hook completion and launch failures

Hook result codes remain success (0), missing (1), abort (99), internal error
(100), not run (101), timeout (102) and failed command (111). An asynchronous
success means the hook was launched and registered, not that its command has
already completed. If worker creation or tracking allocation fails, NSSM waits
synchronously and returns the hook's actual result. A pre-start abort still
cancels application startup in that fallback path.

Workers own their command metadata and process handles. Timed-out or failed
caller waits keep the worker tracked for later cleanup. A synchronous wait
timeout now reports 102 instead of exposing a still-running thread's Windows
status. Hook process-tree termination behavior remains subject to the
separate process-identity review.

Environment validation waits up to five seconds for its suspended test child
to exit and closes both returned handles. If exit cannot be confirmed, it
reports an internal error. An OS refusal to terminate a process is not a
successful validation, unless exit is independently observed. Failure to
resume an affinity-suspended application now fails startup and enters the
existing service cleanup path.

## Numeric settings and processor affinity

Unsigned numeric settings accept values from 0 through 4294967295. Decimal
input is supported; the legacy base-zero syntax is retained, so `0x10` means
16 and `010` means 8. Leading whitespace and a plus sign remain accepted.
Negative numbers, missing digits, overflow and trailing characters are rejected
without replacing the stored value. Use canonical decimal AppExit identifiers;
the separate export/exit-action work still tracks legacy key-name behavior.

`AppAffinity` accepts comma-separated CPU IDs and inclusive ranges such as
`0-2,4,10-12`. IDs must be in 0 through 63; both endpoints are checked, reversed
ranges and empty tokens are rejected, and duplicate entries are harmless.
`All`, the default/reset command and the CLI's empty affinity value still
remove the restriction.

The configuration format remains 64-bit on both architectures. Actual
availability is determined by the Windows affinity API in its current/primary
processor group; the x86 API mask is 32-bit. Parsing a valid identifier does
not establish that that CPU exists locally. At application launch, NSSM
intersects the requested mask with the system mask. A disjoint mask causes the
existing affinity-setting failure warning and leaves the child on its inherited
affinity. This work does not add cross-group selection.

The GUI lists actual system-mask CPU IDs, even when they have gaps. If the query
fails or reports no mask, it lists no specific CPUs; the `All` option remains
available. Choosing a specific affinity with no selected CPU fails validation.
Selecting every displayed CPU retains the existing unrestricted behavior.

## Configuration dumps

`nssm dump MyService` prints commands for cmd.exe. Check its exit status before
using the output: an error can leave earlier complete lines in the stream, so
discard the entire output on failure. Do not pipe a dump directly into a shell.
Arguments whose escaped form exceeds the dump buffer now cause an error instead
of a buffer overwrite.

The quoting tests use `cmd.exe /d /v:off`, with AutoRun and delayed expansion
disabled. The latter keeps exclamation marks literal. Legacy escaping still has
limits: percent signs can change or disappear in saved batch files, and
environment expansion can affect percent-delimited text. Newlines and nested
shell contexts such as `CALL` or parenthesized blocks are not guaranteed to
round-trip. Review such values before executing a saved dump; a successful dump
exit status does not verify its shell interpretation. See
[NSSM-002](fixes/NSSM-002.md) for the tested cases and remaining limitations.

The preserved [upstream manual](upstream/README.txt) contains the
complete command and configuration reference, along with author and contributor
credits. Its old compiler and Windows compatibility notes describe upstream
releases; see [current build requirements](building.md) for this continuation.
