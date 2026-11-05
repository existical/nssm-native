# NSSM 019 Zero-duration handle waits

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 19, B08 |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `2f6a43a1856045d1bba2d3b2253b1aefba376f16` |
| Merge commit | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

`await_single_handle` entered its loop only while elapsed time was less than
the timeout. With zero timeout it never queried the handle and returned an
uninitialized result. Its pre-fix function matches the checked legacy source.

Zero now performs exactly one `WaitForSingleObject(handle, 0)` call, before
allocating formatting buffers or publishing status/checkpoint progress.

| Windows outcome | Helper result |
| --- | --- |
| WAIT_OBJECT_0 | 0: signalled |
| WAIT_TIMEOUT | 1: still unsignalled |
| WAIT_FAILED or another unexpected result | -1: failed wait |

Positive waits retain the existing 20-second interval cap and progress
updates. This follows the [Windows nonblocking wait contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).

The initial run failed nine checks, including the deterministic assertion that
the wait API must be called at zero. Exact values from the old uninitialized
return are not a stable contract. Final coverage includes injected outcomes,
real signalled/unsignalled events, invalid handles, zero/positive delays, a
multi-interval timeout, and application startup with AppThrottle=0.

Zero stop delays now use this defined poll result without adding a grace wait.
This does not change the separately documented SCM startup polling limit.
See [wait tests](../../tests/startup.cpp), [startup caller coverage](../../tests/launch.cpp)
and [B08 validation](../reviews/b08-validation.md).
