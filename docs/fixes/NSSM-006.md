# NSSM 006 Hook worker-creation fallback

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 6 in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `3982016dcd89358feafb156d19a91e4f7598c1f2` |
| Evidence | Injected CreateThread failure and duplicate-free detection |
| Fix PR | Pending creation on branch `fix/b04-hook-ownership` |
| Implementation commit | Pending merge |
| First release | Not yet released |

When worker creation failed, `nssm_hook` called `await_hook` synchronously.
That function freed both the hook name and state; the caller freed them again.
The caller also discarded the completion result, leaving the result as
`NOTRUN` even though the hook had executed.

[hook.cpp](../../src/hook.cpp) now lets `await_hook` perform the sole cleanup
and returns its actual result. The original thread-creation failure diagnostic
is retained. A successful command returns 0, a command requesting abort returns
99, a timed-out command returns 102, and another nonzero command exit returns
111. Wait/query errors return 100. Tracking-allocation failure uses the same
single-owner fallback, as described in [NSSM-005](NSSM-005.md).

This matters for `Start/Pre`: an abort request now reaches the existing startup
abort branch even if a worker thread could not be created. No configuration
change is needed. The behavior difference occurs on the resource-failure path.

`nssm.hook_lifecycle` forces thread creation failure with successful, failed,
abort and timeout outcomes. Allocation and handle accounting checks that each
resource is released once, and the test observes the creation-failure event ID.
The initial pre-fix reproduction detected both duplicate frees and the wrong
fallback result. See [tests/hook_lifecycle.cpp](../../tests/hook_lifecycle.cpp)
and [B04 validation](../reviews/b04-validation.md).

The affected hook source matches the checked 2.24-103 snapshot. This is not a
claim that every legacy binary was tested. The change remains unreleased until
a tagged build includes its merged implementation.
