# B05 logger lifetime validation

Validation date: 2026 November 2. Scope: [NSSM-007](../fixes/NSSM-007.md).
The fix record tracks merge and release status.

The pre-fix commit was `7fa9ca9e99e2a862b04b524b3ed312d81b987630`, after
PR 9 merged. Its `src/io.cpp` and the legacy snapshot's `io.cpp` have the
same Git blob ID, `0ba7e1854f3b88865baef1d5ee08fa1362adecf7`. The legacy
commit checked was `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`.

The first regression used signaled kernel events as stand-ins for completed
thread handles. It observed two `WAIT_FAILED` results after cleanup had
closed those handles, and one wait before both writers had been closed.
All three assertions failed on the pre-fix source and pass after repair.

The maintained `nssm.logger` group compiles production I/O code with test-only
wrappers. The shipped executable contains none of these substitutions.

| Scenario | Evidence |
| --- | --- |
| Ordinary EOF, separate or shared stdout/stderr | Real pipes, workers and temporary files; exact trailing bytes and completed worker verified |
| Output during shutdown | A producer writes 128 KiB while cleanup drains; exact final byte count |
| Child-held writer | Hidden test child inherits a writer; normal trailing output, cancellation and delayed child exit exercised |
| Blocked output | Real output pipe deliberately left unread; worker cancellation and producer completion checked |
| Failed/unavailable cancellation | Injected API result/export absence; worker keeps its reference and completes after service disposal |
| Timeout / WAIT_FAILED | Deferred worker and injected wait outcomes; no wait/close on invalid handles, no premature snapshot free |
| Restart before old worker executes | Current state is rotated/drained independently; old worker runs later and new output remains intact |
| Completion before thread creation returns | Forced schedule; service reference remains valid until cleanup |
| Allocation/event/pipe/thread creation failure | Direct-file fallback writes expected output, with no retained logger allocation |
| File/duplication failure after partial setup | Setup fails, handles are cleared and logger cleanup remains possible |
| Hook output duplication failure | Partial duplicate is closed and cleared; subsequent cleanup is safe |
| Service disposal | Logger references released before deleting the service critical section |
| Repeated lifecycle | 100 additional setup/write/cleanup cycles, alternating shared and separate streams; exact handle-count equality |

In an observed local x64 Release run, the process had 101 handles before and
after the 100-cycle loop. Counts may differ across hosts; each test compares
its own before/after values. Snapshot allocations are counted and freed for
real, including in the service-disposal test; AddressSanitizer does not rely
on quarantined service storage here. Invalid closes are checked in the test
wrapper. Worker waits, child waits and the CTest group itself are bounded.

The delayed-worker tests use injected schedules and wait failures. The blocked
read/write and child-writer cases use actual Windows I/O. One failed-cancel
child case shortens only the initial drain wait to 20 ms. No installed service
is created, edited or controlled. The tests use and remove their own temporary
files; helpers use `CREATE_NO_WINDOW`. They do not prove full SCM callback
synchronization, lossless output after forced cancellation, or behavior of
every device/network driver.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All thirteen CTest groups passed |
| x64 Debug | All thirteen CTest groups passed |
| x86 Release | All thirteen CTest groups passed |
| x86 Debug | All thirteen CTest groups passed |
| x64 AddressSanitizer | `nssm.logger`, `nssm.launch` and `nssm.hook_live` passed |
| MSVC `/analyze` Release | Build completed; retained diagnostics described below |

Static analysis still reports the legacy timestamp return-value warning
(C6001), rotated-filename termination warning (C6054), and large stack frames
(C6262) in I/O code. The B04 hook diagnostics also remain. These are not
suppressed and are not an analyzer-clean claim. A missing-module guard was
added to the new optional cancellation lookup after analysis identified it.
The complete timestamp and rotation repairs remain in their planned batches.

Use the [building instructions](../building.md) for the four configurations.
The sanitizer configuration is reproducible with:

```powershell
cmake -S . -B build/b05-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b05-asan --config Release --parallel
# Run in a VS developer shell, or add the toolset's x64 ASan DLL directory to PATH.
ctest --test-dir build/b05-asan -C Release -R '^nssm.(logger|launch|hook_live)$' --output-on-failure
```

The separate analysis build uses `-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`.
Source files retain CRLF; no message-resource IDs or translations changed.

The shutdown policy follows Microsoft's contracts for
[anonymous-pipe operations](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations)
and [I/O cancellation](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations):
EOF depends on writers closing, and requesting cancellation does not establish
completion. Ownership remains valid even when a cancellation request fails.
