# MSVC warning investigation and repair plan

Baseline `1c3b15e` on `review/beta4-followup`. The previous x64 production analysis recorded 36 diagnostics. Line numbers below refer to the baseline.

| Diagnostic | Sites | Investigation |
| --- | --- | --- |
| C6262 (17) | event 81, gui 1141, hook 178/274, io 367/439/902, process 460, service 934/967/1918, settings 123/136/169/312/322/895 | Fixed local arrays reserve 32–257 KiB per frame. Dump and launch paths can nest multiple such frames. Long paths and command lines still need their full capacities. |
| C6001 (2) | hook 166 | The live prefix of the array contains successfully created worker handles. The analyzer loses track of initialization across allocation, copying, publication and compaction. A timed-out worker must remain owned. |
| C6031 (11), C28020 (1) | service 509/723/765, 866/2091/2125, 1115/1613/1615, utf8 12/13 | SCM probes use last error without checking success. The description probe also violates an SDK annotation. Canonical name lookups can leave invalid output. Wait cancellation needs different completion rules in callbacks and object destruction. UTF-8 stream setup discards CRT errors. |
| C28159 (4) | service 147/173, io 934/1015 | Short relative waits currently use wrapping DWORD arithmetic. A logger can remain idle across an entire clock cycle. |
| C28125 (1) | service 1696 | Hook-lock initialization has no recoverable failure path on systems where resource exhaustion can raise an exception. Startup failure reporting must work before a hook lock exists. |

## Implementation checklist

- [x] Replace large transient stack arrays in GUI, event reporting, process display, registry settings, exports, hooks, rotation and launch. Allocate only on paths that need them. Preserve character/byte capacities, command quoting, archive integrity and cleanup on allocation failure.
- [x] Make initialized hook records and worker ownership explicit. Verify immediate worker completion, failed thread creation, a timed-out wait, compaction and eventual handle release.
- [x] Check SCM query results, validate configuration buffer lengths, read fixed-size delayed-start data directly, and resolve canonical names once without damaging the input name. Exercise empty descriptions, long descriptions and failing lookups.
- [x] Check wait cancellation, distinguish pending callbacks from hard errors, and ensure final destruction waits for the callback before releasing its context. Exercise restart, stop, registration failure and cancellation failure.
- [x] Use monotonic 64-bit elapsed time for SCM budgets and logger retries. Exercise the former DWORD boundary and a long idle interval. Report hook-lock initialization failure without entering an uninitialized lock. Handle UTF-8 setup errors on attached streams while supporting detached services.
- [x] Run fresh production static analysis, review any remaining or newly surfaced diagnostics, then run the clean-source x86/x64 Debug/Release matrix, x64 AddressSanitizer, package verification and live Windows service tests. Record the tested revision and results.

## Verification

Implementation `3289d6820f4caed2727b8da83f799e1161384797` was built with MSVC 19.44.35229 and Windows SDK 10.0.26100.0. The release packages were built from a clean source archive of that commit.

| Check | Result |
| --- | --- |
| MSVC `/analyze` on the production executable | Fresh x86 and x64 Release builds each compiled 14 C++ translation units and reported zero warnings. The changes introduce no warning suppressions. |
| Regression suite | All 37 test groups passed in each x86 and x64 Debug and Release build, for 148 passes. |
| x64 Release with AddressSanitizer | All 37 test groups passed. |
| Package integrity | The package verifier checked all checksums, all four executable and symbol packages, their test results and the source archive identity. |
| Windows 11 x64, build 26300 | Export, dependency, control, lifecycle and legacy upgrade/rollback suites all passed. The lifecycle suite passed three more consecutive runs. |
| Additional live checks | Long and Unicode settings round-tripped, invalid settings produced the expected errors, copy/truncate rotation preserved concurrent appends, and a LocalService hook removed a delegated test service. Zero new fixture services remained. |

Focused regressions exercise an SCM configuration that grows between reads, empty and oversized descriptions, failed name lookup, cancellation with a callback still pending, and failure to cancel a wait. The pending-callback test checks that the destructor blocks until the completion event is signaled. Logger tests advance a simulated clock through a full 32-bit tick cycle.

The guest verified the packaged x64 Release executable before running it. Its SHA-256 was `9342f803b4a003b8c6f33f4b024f3716c5f2ae48e6a6ab5f20632a651c8e9047`. Local analyzer logs, package manifests and test reports are under `build/msvc-warning-acceptance/`.
