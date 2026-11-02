# NSSM 005 Hook completion ownership

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 5 in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `3982016dcd89358feafb156d19a91e4f7598c1f2` |
| Evidence | Forced completion before thread creation returns; quarantined allocations, checked handles and real concurrency tests |
| Fix PR | [PR 9](https://github.com/existical/nssm-native/pull/9) |
| Implementation commit | `e406363257cb6edd553006c55cd518b77f139876` (merge pending) |
| First release | Not yet released |

The hook worker could finish and free its state before `CreateThread` returned.
The caller then read `hook->name` to register the worker or label a synchronous
wait. Both paths accessed released storage. A separate hook-name allocation
could also fail, leaving a null source for the old registration copy.

[hook.cpp](../../src/hook.cpp) now embeds the name in the worker snapshot and
copies caller metadata before publishing the worker. Nothing in the caller
reads the snapshot after successful thread creation.

| Resource | Owner and release |
| --- | --- |
| Hook snapshot, inline name, process handle | Worker; released once at completion |
| Caller wait/registration name | Caller stack copy, then thread-list entry |
| Primary process thread handle | Launching caller; closed after CreateProcess succeeds |
| Worker thread handle | Service thread list; closed after a confirmed completion |
| Thread-list storage | Caller under `service->hook_section`; freed when empty |

Tracking storage is reserved before creating the worker. If reservation fails,
or no list is supplied, the hook runs synchronously using the same cleanup.
List compaction now needs no allocation. Timeout and failed waits retain the
entry instead of closing a potentially live handle. Synchronous wait timeout
returns 102 rather than a raw `STILL_ACTIVE` result; wait or exit-query failure
returns 100. Adding the cleanup allowance to a deadline cannot wrap to zero
or the Windows infinite-wait sentinel.

Workers retain no pointers into service state. Registration and cleanup are
serialized by the existing hook critical section. A late worker can finish
its own cleanup even after a caller's wait expires; its tracking handle is
collected by a later successful list wait. This does not change the separately
reviewed process-tree termination algorithm.

`nssm.hook_lifecycle` exercises immediate/deferred completion, allocation
failure, missing commands, failed launches, wait/exit-query failures, timeout,
retained workers and allocation-free list compaction.
`nssm.hook_live` runs 200 actual hooks with stable handle counts, then races a
new registration against a cleanup thread while a worker is gated.
See [tests/hook_lifecycle.cpp](../../tests/hook_lifecycle.cpp) and
[B04 validation](../reviews/b04-validation.md), including retained analyzer
diagnostics and the distinction between injected and live tests.

Valid hook commands and result-code meanings are preserved. Rare tracking
failures now make an asynchronous request block until synchronous completion.
Use the new NSSM executable for subsequent service runs; no hook configuration
migration is required. The checked legacy hook source is identical to the
pre-fix continuation; other historic binaries were not tested separately.
