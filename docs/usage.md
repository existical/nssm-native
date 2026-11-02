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

`nssm stop`, `pause` and `continue` also wait for their target SCM state:
STOPPED, PAUSED and RUNNING respectively. A stopped service with a nonzero
shutdown exit code is a failure. Query errors and service-specific codes are
reported; successful API calls ignore stale last-error values.

| Control | Maximum polling allowance |
| --- | --- |
| START | 60,000 ms, unchanged |
| PAUSE / CONTINUE | 60,000 ms each |
| STOP, native service or unavailable NSSM settings | 180,000 ms |
| STOP, readable NSSM settings | `min(86,400,000, max(180,000, 133,000 + D))` ms |

For STOP, `D` is the sum of AppStopMethodConsole, AppStopMethodWindow and
AppStopMethodThreads for methods enabled by AppStopMethodSkip. Each absent or
malformed DWORD uses its 1,500 ms default; zero is valid. A readable Application
string value identifies NSSM configuration for this calculation. The fixed
133,000 ms reserves 20,000 for Stop/Pre, 80,000 for outstanding hooks, 3,000 for
two logger drain waits, and 30,000 for cleanup/SCM overhead. This is a
conservative client policy: it does not multiply allowances by the unknown
number of descendants or outstanding hook threads. Default settings produce
180 seconds. For example, AppStopMethodConsole=240000 with other defaults
allows 376 seconds. The sum uses 64-bit arithmetic and saturates at 24 hours.
Registry reads are optional and require no configuration-write rights.

Each action can fail sooner when progress stalls: the wait hint at the last
checkpoint increase or state change sets the progress window, with a 10-second
default, a one-second minimum, and the action allowance as its maximum.
Changing the hint without progress does not renew that window. Poll intervals
are 100–1,000 ms, shortened for the remaining deadline. Continuous progress
never extends the total allowance. Stalled-progress and total-limit diagnostics
both return a nonzero command status; a final successful poll can complete at
the deadline. AppThrottle has no effect on any of these limits.

These limits govern polling after the Windows control call returns. Windows
API call time and scheduling can add elapsed time; ControlService itself may
block for 30 seconds, as documented by
[Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice).
A CLI timeout does not cancel the control or kill the service. Very large
process trees, multiple outstanding hooks, changed runtime settings or
configured delays above the cap may outlive the client wait. Check status
before retrying or taking further action. See [NSSM-024](fixes/NSSM-024.md).

NSSM supports application arguments, startup directories, service accounts,
environment settings, restart policies, output redirection, log rotation and
event hooks.

## Offline log rotation

With `AppRotateFiles` enabled, NSSM checks existing output files before
launching the application. When file metadata is available, every nonzero
limit must pass: `AppRotateSeconds` is the minimum age, and the minimum size
is `AppRotateBytesHigh * 4294967296 + AppRotateBytes`. Equality qualifies;
zero disables that limit.

`AppStdoutCopyAndTruncate` and `AppStderrCopyAndTruncate` select copying
followed by truncating the original. In that mode, `AppRotateDelay` is a delay
in milliseconds between copying and truncating. Rename mode retains its
existing no-delay behavior. Shared stdout/stderr files rotate once using
stdout's settings.

Legacy 2.24-103 source passed these arguments in the wrong order. A byte
threshold could become a long sleep, and a delay could become a very large
size threshold. Existing values need no format migration, but review any
workarounds for that behavior. See [NSSM-008](fixes/NSSM-008.md) for the exact
mapping, tests and remaining filesystem-error limitations.

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
status. Hook process-tree termination uses the identity checks described below.

Environment validation waits up to five seconds for its suspended test child
to exit and closes both returned handles. If exit cannot be confirmed, it
reports an internal error. An OS refusal to terminate a process is not a
successful validation, unless exit is independently observed. Failure to
resume an affinity-suspended application now fails startup and enters the
existing service cleanup path.

## Process-tree cleanup

Application and hook cleanup retain the original process handle while walking
descendants. Each candidate child is opened once, its current parent and
creation time are checked, and that same handle is used for cleanup. Child
times are checked against the immediate parent, including when the root has
already exited. `processes` uses read-only process rights and returns nonzero
when traversal fails.

Missing, inaccessible or unverified identities are skipped and reported rather
than reopened by PID. This can leave a subtree running. Cleanup also skips
depth 128 and deeper; concurrent process creation, disappearing intermediate
parents and clock changes can limit snapshot-based discovery. A successful
service stop is not proof that every descendant exited.

The existing stop-method ordering and settings remain available. Thread
signalling validates a retained thread handle and its owner. Window ownership
is rechecked before each post, but Windows cannot make a foreign-window check
and message atomic. Ctrl+C still reaches processes sharing the attached
console. Optional identity APIs are resolved dynamically; unavailable process
identity support causes cleanup to skip, and unavailable thread-owner support
skips thread signalling. See [NSSM-009](fixes/NSSM-009.md) for compatibility,
diagnostics and test coverage.

## Numeric settings and processor affinity

String resets retain their previous registry semantics: Application,
AppParameters and AppDirectory become empty REG_EXPAND_SZ values; AppStdin,
AppStdout and AppStderr are deleted. The reset no longer changes ownership of
the CLI's input pointer. Resetting Application still leaves no executable path
to launch. See [NSSM-015](fixes/NSSM-015.md).

A zero AppThrottle or stop-method delay now queries the process handle once
without sleeping or publishing wait progress. Signalled, still-running and
failed-wait outcomes have defined results. Positive delays retain their
existing behavior; see [NSSM-019](fixes/NSSM-019.md).

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

## Installation and diagnostic failures

Failed administrator checks now produce a non-administrator result using the
Windows API's correct output type. An SCM-open failure during installation
leaves object cleanup to the CLI or GUI caller, avoiding duplicate destruction.
Existing install error codes and elevation behavior remain in use. See
[NSSM-018](fixes/NSSM-018.md) and [NSSM-021](fixes/NSSM-021.md).

The `processes` command temporarily requests debug privilege and restores
the caller's previous state afterward. If acquisition fails, inspection still
uses available access. If privilege or impersonation restoration fails, the
command reports the failure and exits with code 111. See
[NSSM-028](fixes/NSSM-028.md).

Error-message buffers now have matching byte/character capacities, correct
fallback ownership and automatic cleanup when worker threads return. Message
text and locale selection remain unchanged; see [NSSM-032](fixes/NSSM-032.md).

## Redirected input and log bytes

AppStdin now gives the child an inheritable input file handle. Configured share,
disposition and flags are retained, and the parent's launch copy is closed after
creation succeeds or fails. See [NSSM-012](fixes/NSSM-012.md).

Timestamped UTF-16LE output preserves split code units and uses complete LF
characters as line boundaries. A leading UTF-16LE BOM gives reliable detection;
BOM-less detection remains heuristic. Known-empty UTF-16 output files receive
one BOM. Existing output encoding must match the child stream. An incomplete
final byte is copied unchanged; encoding changes within a stream are unsupported.
See [NSSM-013](fixes/NSSM-013.md) for probe and append behavior.

Online size rotation includes timestamps and BOM bytes and occurs after a
complete newline reaches the threshold. Short writes resume at the remaining
suffix. An unrecoverable/incomplete write stops that logger and records an error;
later child writes to its pipe may fail. Unknown initial file size no longer
disables the threshold. See [NSSM-014](fixes/NSSM-014.md) for rotation failure
behavior and the remaining offline/external-writer limitations.

Startup failure events now contain the numeric error as text. Event arguments
are counted and checked as string pointers at compilation; see
[NSSM-022](fixes/NSSM-022.md). Logging remains best-effort if Windows rejects an event.

## AppExit rules and configuration export

AppExit export now includes explicit default and numeric rules, including
`Default=Ignore` alongside `42=Exit`. New set/reset commands use canonical
decimal registry names while preserving the CLI's base-zero parser: `052`
and `0x2a` both mean 42, whereas `042` means 34. Runtime lookup already used
canonical decimal. Dumps reject old raw aliases and malformed values rather
than silently dropping or activating them; no existing aliases are migrated
automatically. See [NSSM-016](fixes/NSSM-016.md) before repairing old raw keys.

Missing AppExit configuration defaults to Restart. Missing numeric overrides
use the unnamed default, if present. Invalid types, unterminated/oversized
strings, unknown actions and query failures return errors with initialized
outputs; runtime error handling retains Restart. See [NSSM-033](fixes/NSSM-033.md).

`dump Source Target` reads Source while emitting commands for Target. Check
the dump command's exit status and discard any partial script on failure.
Use a quiet configuration window when consistency matters; export is not a
transactional snapshot. Existing command-shell quoting limits still apply.

Dump/get need configuration-query service access and read access to the SCM
and registry. They no longer require SERVICE_CHANGE_CONFIG. Set/reset/edit
still require change access. Prefer the canonical service name when access
is restricted; see [NSSM-030](fixes/NSSM-030.md) for the precise masks and
display-name enumeration limitation.

## GUI edits, accounts and hook references

Correcting invalid input and retrying a GUI save keeps the original service
handle open. The temporary configuration and confirmation password are cleaned
up on success and failure; closing the dialog releases its original service
object. See [NSSM-020](fixes/NSSM-020.md) and [NSSM-025](fixes/NSSM-025.md).

Account-name lookup now releases its Windows resources and requests only
lookup policy access. Malformed LSA results fail with initialized outputs;
service-logon rights use bounded counted-string comparisons. Installation may
still need account-right modification access. See [NSSM-026](fixes/NSSM-026.md)
and [NSSM-027](fixes/NSSM-027.md).

The hook editor displays and saves literal `%VARIABLE%` references and preserves
the existing REG_SZ/REG_EXPAND_SZ type, including explicit edits. New hooks use
REG_EXPAND_SZ. The execution-time reader expands REG_EXPAND_SZ in the hook's
runtime environment; REG_SZ remains literal. Unrelated saves skip entries that
could not be read. Existing values that earlier edits already expanded cannot
be reconstructed automatically. CLI hook get/dump behavior is unchanged.
See [NSSM-034](fixes/NSSM-034.md) for test evidence and limitations.

## Dependency updates

`reset Service DependOnService` clears service dependencies while retaining
load-order groups. `reset Service DependOnGroup` does the reverse. An empty
result is sent as an explicit empty list, so resetting the sole dependency
also clears it. Removing the last service/group with `-Name` retains the other
type. Empty values and `:` with no entries also clear only the selected type.

Plain names or `:Name` replace a type; additional arguments add entries to that
replacement list. `+Name` adds one dependency and `-Name` removes matching
entries case-insensitively. Groups accept their optional `+` identifier:
`++Group` adds and `-+Group` removes. Duplicate requested names keep the first
spelling; unrelated entries, including duplicates, remain. Group entries are
rejected in DependOnService. Bare add/remove operators and multi-entry
add/remove requests fail.

Updates use one configuration snapshot and one SCM write. Reset/removal can
retain existing names that are no longer resolvable; adding/replacing still
validates names. Failures return a nonzero command status. Concurrent edits
are not transactional, and display-name aliases are not necessarily deduplicated
before canonicalization. See [NSSM-023](fixes/NSSM-023.md) for ordering,
compatibility, resource cleanup and validation details.
