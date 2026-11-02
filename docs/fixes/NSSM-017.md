# NSSM 017 Launch handle cleanup

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 17 in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `3982016dcd89358feafb156d19a91e4f7598c1f2` |
| Evidence | Checked API handles, launch failure injection and real repeated-child handle counts |
| Fix PR | Pending creation on branch `fix/b04-hook-ownership` |
| Implementation commit | Pending merge |
| First release | Not yet released |

Hook and application launches retained the primary-thread handle returned by
`CreateProcess`. Environment validation retained both returned handles after
terminating its suspended child. Repeated operations could grow the NSSM
process's handle count.

- [hook.cpp](../../src/hook.cpp) closes the primary-thread handle immediately;
  the worker owns the process handle.
- [service.cpp](../../src/service.cpp) closes the primary-thread handle after
  any required resume. The service keeps its process handle until normal stop
  cleanup. A failed resume closes the thread handle, reports startup failure
  with the OS error code and enters the existing stop path.
- [env.cpp](../../src/env.cpp) requests termination, waits at most five seconds
  for exit, then closes both handles. Confirmed exit returns 0; unconfirmed
  cleanup returns -1. Creation with an invalid environment still returns 1.

The audit also found that a `Start/Pre` abort returned without closing prepared
startup I/O handles. That branch now calls the existing output-handle cleanup
before stopping. This is a launch-boundary repair, not the broader logger
ownership fix planned as B05.

A failed TerminateProcess followed by confirmed exit can still validate
successfully, because the child may already have exited. If Windows refuses
termination and exit cannot be observed, validation fails and releases the
caller's handles; it cannot guarantee that the OS has removed the child.
There is no claim of forced cleanup despite arbitrary OS failures.

`nssm.launch` injects creation, termination, wait and resume failures, checks
pre-start-abort cleanup, and runs 200 simulated application start/stop cycles.
It also launches 200 real suspended validation children and compares process
handle counts before/after. `nssm.hook_live` does the same for 200 real hooks.
Tests do not install or control real services. See
[tests/launch.cpp](../../tests/launch.cpp),
[tests/launch_fixture.h](../../tests/launch_fixture.h),
[tests/hook_lifecycle.cpp](../../tests/hook_lifecycle.cpp) and
[B04 validation](../reviews/b04-validation.md).

Normal launch behavior and configuration remain unchanged. Environment
validation now waits for confirmed child exit and can report a cleanup error
where the legacy code reported success. An affinity-suspended application
whose resume fails no longer reports a successful start. These fixes apply
when running the new NSSM executable. The checked legacy launch functions
match the pre-fix continuation; other historic binaries were not tested.
