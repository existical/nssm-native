# B12 dependency validation

Validation date: 2026 November 2. Scope: [NSSM-023](../fixes/NSSM-023.md).
B12 is implemented in [PR 17](https://github.com/existical/nssm-native/pull/17),
pending review and merge, and unreleased. The
[fix index](../fixes/README.md) records implementation and release status.

Baseline: `f969dd985039a8d2fe77a0ed0a24e077894c2bef`, after PR 16 merged.
Implementation: `856abf321fc56f890ef0e6ea17b7def04e02c53a`.
Legacy inspection used `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`
(`v2.24-103-gdee49fc`). Its settings.cpp contains the null reset pointers and
service/group concatenation offset; service.cpp contains the extra-character
dependency copy and the unclosed dependency-service handle. No historic binary
was executed.

The initial `nssm.dependencies` regression produced six failed checks against
the baseline: two null-pointer SCM-contract assertions, three incorrect lists,
and one service-handle imbalance. It executed production setting callbacks,
dependency reads and validation, with SCM boundaries substituted and registry
writes redirected within the process to a disposable HKCU key.

Final unit coverage includes:

- Reset with neither/either/both types present; removing the final service or
  group while retaining the other; empty values, forced replacement and
  one-character names.
- Add/remove/replace operators, CRLF/LF and blank entries, case-insensitive
  requested duplicates, unchanged other-type duplicates, prefixes and order.
- Missing/unresolvable retained names on reset/remove, denied query/write,
  failed manager/service/name lookup, every tracked allocation stage, missing
  and wrong-type group metadata, oversized names and unchanged input buffers.
- Dependency pointers outside/inside the fixed header, misalignment and missing
  final terminators; exact returned MULTI_SZ counts and initialized failures.
- Failed dependency dump allocation, counted SCM/service cleanup and 50 rejected
  group updates with stable process handle counts.

The extra-character copy and adjacent validation failures were source-traced
and covered by final bounds/failure tests; the six-check red result does not
claim to have safely executed every unsafe legacy branch.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All 34 standard CTest groups passed |
| x64 Debug | All 34 standard CTest groups passed |
| x86 Release | All 34 standard CTest groups passed |
| x86 Debug | All 34 standard CTest groups passed |
| x64 AddressSanitizer | All seven selected groups passed: dependencies, registry, multisz, exit_export, export_access, install_ownership and gui_retry |
| MSVC `/analyze` Release | Build completed; remaining diagnostics retained |
| GitHub Windows/real SCM checks | All four jobs passed; x64 Release passed both real SCM groups, 36 total, and each other job passed all 34 |

All four jobs passed at commit `82870f60e176146b34f19db7eb853c64f90cd422`
in [CI run 41532556274](https://github.com/existical/nssm-native/actions/runs/41532556274).
The x64 Release log records `nssm.export_live` and `nssm.dependencies_live`
passing as groups 35 and 36.

The new merge/validation/getter bodies have no analysis diagnostics in this
build. Existing warnings remain, including ignored sizing-query and other
service API returns, stack sizes, GUI shadowing, GetTickCount guidance and
the registry MULTI_SZ analysis warning. No diagnostic was suppressed; this is
not an analyzer-clean claim.

The local token is not elevated, so the SCM cases were compiled locally but
not executed there. CI opts in with `NSSM_ENABLE_SCM_TESTS=ON` only on its
disposable x64 Release runner. That configuration has 36 groups: 34 standard
groups, the existing export/ACL integration test and the new dependency test.

`nssm.dependencies_live` creates four uniquely named disabled services and
reads two existing service-group names without modifying global group data.
It invokes the actual NSSM executable, queries SCM after changes, tests
service/group resets, final-entry removals, empty and duplicate operations,
missing names, circular-dependency rejection and denied access, then replays
only dependency setters from a dump into its owned destination fixture.
It never starts a service or runs a dumped install command. All four fixtures
are deleted with retained handles; cleanup failures fail the test. Each child
has a ten-second wait and the overall test has a 120-second CTest timeout.
Only owned subprocesses are terminated on timeout.

Use `scripts/build.ps1 -Architecture x64|x86 -Configuration Release|Debug`
for the full standard matrix. The dependency unit group has a 20-second
timeout. See [building](../building.md) for SCM opt-in. To reproduce the memory
checks:

```powershell
cmake -S . -B build/b12-asan -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' `
  '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b12-asan --config Release --parallel
# Add the matching MSVC ASan runtime directory to PATH.
ctest --test-dir build/b12-asan -C Release `
  -R 'nssm.(dependencies|registry|multisz|exit_export|export_access|install_ownership|gui_retry)$' `
  --output-on-failure
```

This does not certify service startup ordering, driver dependencies, remote
SCM hosts, concurrent administrator edits or legacy Windows. Service lifecycle
acceptance, remaining B13 waits and release packaging remain separate work.
