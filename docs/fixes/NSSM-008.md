# NSSM 008 Offline rotation parameters

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 8 in the 2026 November 2 review |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `b41b26e4c304b0dfd5202e1bb3fd99286766f178` |
| Evidence | Real temporary log rotation through both output setup paths; sleep requests intercepted |
| Fix PR | [PR 11](https://github.com/existical/nssm-native/pull/11) |
| Implementation commit | `f3459073f215025c7292c28f38b00d9c1f3f97b1`; merged at `9a5a14549b1837ba518ed577905aaafe04236d66` |
| First release | Not yet released |

The offline rotation function expected seconds, delay, low size word and high
size word. Both callers passed seconds, low size word, high size word and
delay. The rotation function and those calls match the checked legacy source.
This is source comparison plus a reproduction against the continuation, not
a claim that every historic binary was executed.

Let `L` be AppRotateBytes, `H` AppRotateBytesHigh and `D` AppRotateDelay:

| Value used by offline rotation | Legacy caller mapping | Corrected mapping |
| --- | --- | --- |
| Minimum size in bytes | `(D * 2^32) + H` | `(H * 2^32) + L` |
| Copy/truncate delay in milliseconds | `L` | `D` |
| Minimum age | AppRotateSeconds | AppRotateSeconds |

With a 4-byte log, AppRotateBytes=1048576, AppRotateBytesHigh=0 and
AppRotateDelay=0, the old code copied and truncated the file despite its
1 MiB threshold. It requested a 1048576 ms sleep, or 17 minutes 28.576 seconds.
The regression observed this for stdout and stderr without actually sleeping.
The corrected code leaves the file unchanged and requests no sleep.

[io.cpp](../../src/io.cpp) now constructs a named `rotation_options_t` with
seconds, delay in milliseconds, an unsigned 64-bit minimum size and the copy
mode. Both callers use the same construction, with their own copy/truncate
setting. The API no longer accepts four adjacent unsigned numeric arguments.
[nssm_io.h](../../src/nssm_io.h) defines the internal options type.

When file metadata is available, nonzero age and size limits must both pass;
equality qualifies for rotation. Zero disables that individual limit. The
delay applies after a successful copy and before truncation. Rename rotation
retains its existing no-sleep behavior. A failed copy or rename, including an
existing archive name, leaves the original bytes intact in the tested paths.
Shared stdout/stderr paths continue to rotate once using stdout's policy.

Registry names and stored values are unchanged; no migration is performed.
Review settings chosen to work around the old parameter swap. A nonzero delay
previously became the high size word and could suppress ordinary rotation;
after this repair, that value controls the copy/truncate wait. Conversely,
byte thresholds no longer become unintended waits. The repair affects rotation
during output setup before an application launch, including restarts; it does
not change online/on-demand rotation's parameter plumbing.

This is a parameter repair, not a rewrite of all rotation failure handling.
The existing metadata-query failure fallback can bypass age/size thresholds.
The copy-success path still does not reliably report later reopen, seek or
truncate failures; successful rotation events alone do not prove truncation
succeeded. Those paths, concurrent external writers, filename edge cases and
timestamp accounting remain follow-up work. The tests do not claim complete
recovery from arbitrary filesystem failures.

See [rotation regressions](../../tests/rotation.cpp), their
[test wrappers](../../tests/rotation_fixture.h) and
[B06 validation](../reviews/b06-validation.md) for exact coverage, simulated
large-file metadata and retained analyzer diagnostics.
