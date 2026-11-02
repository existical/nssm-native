# B10 configuration-export validation

Validation date: 2026 November 2. Scope: NSSM-016, NSSM-030 and NSSM-033.
The [fix index](../fixes/README.md) distinguishes implementation, merge and
release status. B10 is not merged or released yet.

Baseline: `0497d8a4d37881e89c3762c3957d1abf775a2190`, after PR 14 merged.
Implementation: `fc029e716391d3aa9ff3399cd388b428da8e4561`.
The relevant AppExit dump, setter, reader and dump access expression were
also checked in recovered legacy source
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`).
Those defects are present there; no historic binary was executed.

| Fix | Initial failure | Final standard regression coverage |
| --- | --- | --- |
| [016](../fixes/NSSM-016.md) | `Default=Ignore` exported but `42=Exit` was lost; enumeration failure returned success and retried the same index; three failed checks | All actions, explicit/default rules, maximum DWORD, invalid/long/alias names, query type failure, bounded enumeration failure, emitted-argument replay/re-export, new key canonicalization, renamed hooks/native environment, 100 dumps with balanced allocations and handles |
| [030](../fixes/NSSM-030.md) | Dump requested SERVICE_CHANGE_CONFIG; one failed mask check | Exact SCM/service rights for dump/get/set/reset/edit, service allocation failure, early cleanup and retained setter change rights |
| [033](../fixes/NSSM-033.md) | Missing key/default left stale text and a wrong-type numeric rule succeeded; five failed checks | Missing key/default/override, Restart/Ignore/Exit/Suicide, mixed case, raw size/type/termination errors, query/open denial, overlong paths, allocation failure and null default handling |

The enumeration fixture stops the legacy loop after three injected errors
and a synthetic end marker, so the red test does not hang. The production
fix stops after the first enumeration error. Registry tests redirect HKLM
within the test process to a unique disposable HKCU key. They do not modify
installed services. Export replay parses the actual emitted command lines
and calls the production AppExit setter, then compares the new dump exactly.

The initial sanitizer run exposed an unreliable unterminated-string fixture,
not a sanitizer memory report: adjacent zero-filled storage could yield a
valid stored string. The final fixture uses a nonzero adjacent guard and
asserts the stored byte count before exercising the reader. It passes in
all four configurations and under AddressSanitizer.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All 28 standard CTest groups passed |
| x64 Debug | All 28 standard CTest groups passed |
| x86 Release | All 28 standard CTest groups passed |
| x86 Debug | All 28 standard CTest groups passed |
| x64 AddressSanitizer | exit_export, exit_fallback, export_access, registry, numeric, quoting, setting_ownership and install_ownership passed |
| MSVC `/analyze` Release | Build completed; remaining diagnostics retained |
| Real SCM integration | Enabled for the disposable x64 Release CI runner; result pending |

The local session is not elevated, so the real SCM test was compiled but not
run locally. It is disabled by default. CI explicitly opts in to
`NSSM_ENABLE_SCM_TESTS=ON` only in x64 Release, adding a 29th group with a
120-second timeout. The standard B10 groups retain 20-second timeouts.

The integration test creates two unique, disabled services. A source DACL
denies SERVICE_CHANGE_CONFIG to everyone, including the administrator test
caller, while allowing SERVICE_QUERY_CONFIG. It checks real `nssm dump` and
`get`, verifies `set` is denied and leaves AppParameters unchanged, replays
only AppExit set commands into the writable fixture, then re-exports all
four rules. No emitted install command runs and neither service is started.
Owned handles allow both services to be deleted even after the DACL changes.
Subprocesses have bounded waits and only owned child processes are terminated
on timeout. Cleanup failure makes the test fail.

The former AppExit uninitialized/termination diagnostics are absent. Large
stack frames, GUI/account diagnostics, ignored service API results, hook
tracking warnings and the registry MULTI_SZ analysis diagnostic remain.
No warning was suppressed; this is not an analyzer-clean claim.

Use `scripts/build.ps1 -Architecture x64|x86 -Configuration Release|Debug`
for the full standard matrix. To reproduce sanitizer checks:

```powershell
cmake -S . -B build/b10-asan -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' `
  '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b10-asan --config Release --parallel
# Add the matching MSVC ASan runtime directory to PATH.
ctest --test-dir build/b10-asan -C Release `
  -R 'nssm.(exit_export|exit_fallback|export_access|registry|numeric|quoting|setting_ownership|install_ownership)$' `
  --output-on-failure
```

See [building](../building.md) for the explicit SCM opt-in. The three fix
records document canonical-key migration limits, strict action validation,
partial dump output, query-only access and display-name restrictions.
This does not certify concurrent configuration changes, arbitrary shell
round trips, interactive GUI/account scenarios or legacy Windows support.
