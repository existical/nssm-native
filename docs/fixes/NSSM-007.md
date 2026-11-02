# NSSM 007 Logger shutdown ownership

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 7 in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `7fa9ca9e99e2a862b04b524b3ed312d81b987630` |
| Evidence | Invalid waits reproduced; controlled failure schedules and real pipe/worker/child tests |
| Fix PR | Pending creation on branch `fix/b05-logger-lifetime` |
| Implementation commit | Pending merge |
| First release | Not yet released |

The legacy cleanup closed the logger's thread handle, remembered its numeric
value and then waited on that closed handle. Cleanup also closed the read end
of a pipe which the worker still owned and would close again. A surviving
worker held pointers into the service's name, path and rotation flag, so a
restart or service disposal could invalidate its state. Shared stdout/stderr
also kept one parent writer open while attempting to join the shared reader.

The complete pre-fix `io.cpp` matches the checked legacy source. The initial
regression failed three checks: both waits used closed handles, and the first
wait began before both parent writers were closed. This is source comparison
and targeted reproduction, not a claim that every historic binary was tested.

The [implementation](../../src/io.cpp) now assigns ownership explicitly:

| Resource | Owner and release |
| --- | --- |
| Parent stdout/stderr writers | Service; both closed before any logger join |
| Duplicated launch/hook writers | Caller until launch cleanup; inherited child copies live until the child closes them |
| Logger thread handle | Service; closed after processing the wait result |
| Read pipe and current output file | Worker after successful creation; only the worker closes them |
| Name, path, rotation state and stop event | Logger snapshot; reference counted between service and worker |

Logger registration, hook-handle duplication, rotation requests and cleanup
use the existing hook critical section. Rotation requests use atomic state
within the current logger snapshot. Worker completion, including completion
before `CreateThread` returns, releases only its own reference. Cleanup removes
the service's reference; whichever owner finishes last frees the snapshot.
Workers retain no service pointers. Service disposal also invokes cleanup.

Parent pipe writers are no longer inheritable. The separate `STARTUPINFO`
duplicates remain inheritable, including those supplied to shared-output
hooks. This prevents children from receiving unnecessary parent pipe copies.
Creation failures preserve the existing direct-file fallback. A later output
handle duplication failure now fails setup and retains the worker for normal
cleanup instead of closing its thread handle prematurely. Closed startup
handles are cleared, making repeated cleanup safe.
The pipe-creation failure diagnostic also receives its missing null sentinel;
the full insertion-argument audit in NSSM-022 remains planned.

Normal shutdown drains trailing output after closing the writers. Each worker
gets the existing 1500 ms drain wait. On timeout or failed wait, cleanup signals
the worker's stop event, requests `CancelSynchronousIo` when available, and
waits another 100 ms. Retry and rotation delays respond to the stop event.
This bounds these waits; lock acquisition and OS calls are separate costs.

Cancellation is not guaranteed. If it is absent, fails, races with a new I/O
call, or cannot cancel the underlying operation, the worker retains its own
snapshot and I/O handles until it can exit. Cleanup never closes another
thread's active I/O handles or terminates its thread. A stalled OS operation
can therefore retain resources until it returns or NSSM exits. In-flight I/O
or an already-started rotation may finish late; unread or unwritten trailing
output can be lost after the deadline. There is no lossless forced-stop claim.

No configuration migration is required. Manual rotation still targets the
current logger, and restart state cannot be changed by an older worker.
UTF-16 timestamp formatting, complete byte accounting (NSSM-013/014), offline
rotation arguments (NSSM-008), and broader service callback races remain
separate tasks. This repair initializes the basic read/write counters and
initial observed size needed by the shutdown paths; it does not complete
NSSM-014's timestamp/partial-write accounting work.

See [logger regressions](../../tests/logger.cpp) and
[B05 validation](../reviews/b05-validation.md) for injected versus live
coverage, sanitizer results, retained diagnostics and test limitations.
