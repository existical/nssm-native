# B04 hook ownership and launch validation

Validation date: 2026 November 2. Scope:
[NSSM-005](../fixes/NSSM-005.md),
[NSSM-006](../fixes/NSSM-006.md) and
[NSSM-017](../fixes/NSSM-017.md).
Fix records track merge and release status.

The pre-fix source was `3982016dcd89358feafb156d19a91e4f7598c1f2`,
after B03 merged in PR 8. Its complete hook source, `test_environment` and
`start_service` match the checked
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` source snapshot.

| Scenario | Pre-fix observation | Corrected result |
| --- | --- | --- |
| Worker completes before CreateThread returns | Caller reads released name storage in async/sync paths | Caller uses metadata copied before publication |
| Worker creation fails | Name/state freed twice; fallback result discarded | One cleanup, actual completion result |
| Hook/application CreateProcess succeeds | Primary-thread handle remains open | Closed once, after resume if required |
| Environment validation succeeds | Both handles remain open; no exit wait | Both closed after bounded exit confirmation |
| Start/Pre abort | Prepared startup I/O handles not closed | Existing close helper called before stop |
| ResumeThread failure | Source did not check failure | Startup fails and service cleanup runs |

The initial hook regression failed eight assertions. The initial launch
regression failed thirteen. Those counts include repeated ownership checks,
not independent defect counts. Quarantined allocations allowed the pre-fix
hook test to report duplicate frees and stale reads without corrupting its
own heap. Simulated handles detect invalid waits, missing closes and duplicate
closes. Later boundary/failure cases were added after repair.

The final tests separate three types of evidence:

- `nssm.hook_lifecycle` runs production hook code with controlled scheduling,
  allocation and API failures. It covers immediate/deferred completion,
  success/failure/abort/timeout, missing hooks, failed process/thread creation,
  tracking reservation failure, wait/query failure, extreme deadlines,
  retained entries and compaction without allocation.
- `nssm.launch` replaces service/SCM/process/I/O boundaries for exact ownership
  checks, including resume failure and pre-start abort. It runs 200 simulated
  service starts/stops, then 200 real suspended validation children.
- `nssm.hook_live` uses actual CreateProcess/CreateThread and production hook
  registration/waiting for 200 hooks. A gated worker, cleanup thread and new
  launch exercise concurrent cleanup/registration under the hook critical
  section. Tree cleanup is limited to the fixture child, which has no descendants;
  the separately reviewed process-tree walker is not validated by this test.

In an observed local x64 Release run, process handle counts were 100 before
and after 200 validator launches, and 101 before and after 200 hook launches.
Each stress test warms up first, waits for completion, closes its handles and
requires an unchanged count. Counts need not be identical between hosts.
Registry writes use the temporary redirected HKCU fixture; no real service
is installed, edited, started or stopped. No visible window is launched.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All twelve CTest groups passed |
| x64 Debug | All twelve CTest groups passed |
| x86 Release | All twelve CTest groups passed |
| x86 Debug | All twelve CTest groups passed |
| x64 Release AddressSanitizer | `nssm.hook_lifecycle`, `nssm.launch` and `nssm.hook_live` passed |
| MSVC `/analyze` Release | Build completed; retained diagnostics described below |

MSVC still reports C6001 at the hook-list handle wait and C6262 for the large
`nssm_hook` stack frame. The initialization/compaction invariant was inspected:
only registered entries count toward `num_threads`, reservation precedes
publication, and compaction writes at or before the current index. The tests
did not reproduce an uninitialized handle read after repair. The diagnostic
is retained, not suppressed; this is not an analyzer-clean claim. Other legacy
diagnostics remain elsewhere. The previous hook/environment primary-thread
leak diagnostics no longer appear in the changed launch paths.

Run the matrix using [building instructions](../building.md). For the targeted
sanitizer checks:

```powershell
cmake -S . -B build/b04-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b04-asan --config Release --parallel
# Use a VS developer shell or add the selected toolset's x64 ASan DLL
# directory to PATH before running:
ctest --test-dir build/b04-asan -C Release -R 'nssm.(hook_lifecycle|launch|hook_live)' --output-on-failure
```

Static analysis uses `-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze` in a
separate Release build. Simulated allocations are quarantined, so their
ownership assertions supplement ASan; live worker tests use real frees.

Microsoft documents that a [new thread can run before CreateThread returns](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread)
and that both [PROCESS_INFORMATION handles need closing](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_information).
These tests exercise those ownership contracts. They do not establish that
process-tree cleanup succeeds despite arbitrary OS failures, or close the
separate logger and process-identity review tasks.
