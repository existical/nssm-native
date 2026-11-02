# B11 GUI and account validation

Validation date: 2026 November 2. Scope: NSSM-020, NSSM-025, NSSM-026,
NSSM-027 and NSSM-034. B11 is implemented in
[PR 16](https://github.com/existical/nssm-native/pull/16), pending review and
merge, and unreleased. The [fix index](../fixes/README.md) records each issue's state.

Baseline: `f28a43141a576033713190fe9b716fb503ac227a`, after PR 15 merged.
Implementation: `b91392150c23ecec4299c4eec75a7fcb65f867fb`.
The relevant GUI ownership/password/hook-load paths and account lookup/right
comparison were also inspected in recovered legacy source
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`). The
problematic patterns are present there; no historic binary was executed.

| Fix | Initial failing evidence | Final coverage |
| --- | --- | --- |
| [020](../fixes/NSSM-020.md) | Five GUI retry ownership assertions: validation/save cleanup closed the borrowed handle | Missing name/path, invalid environment, correction/retry, denied/successful saves, install validation, allocation failures, cancel, failed dialog creation and counted service closes |
| [025](../fixes/NSSM-025.md) | One retained confirmation allocation after matching passwords | Equal/unequal/missing passwords, length mismatch, both read failures, credential allocation failures, whole-buffer zeroing before free, canonical account replacement |
| [026](../fixes/NSSM-026.md) | Seven resource-balance failures across successful and failed lookups | Policy/name/SID/conversion/computer-name/initializer/allocation failures, initialized outputs, borrowed policies, invalid metadata/indices, rejected groups, large combined name, SID equality and 30 real local lookups |
| [027](../fixes/NSSM-027.md) | One excessive comparison count and four mismatched SID deallocator calls | Case/length/prefix/suffix/unrelated rights, malformed metadata, no rights, enumeration/addition errors and nonterminated text at a protected page boundary |
| [034](../fixes/NSSM-034.md) | Two literal-text checks: GUI display and unrelated save expanded the reference | View/cancel, unrelated save, explicit edits of both registry types, malformed-entry preservation, new-dialog isolation and real hook launch after changing the environment |

The red LSA comparison wrapper asserts the requested count and caps it to the
declared length, preventing an unsafe legacy read during reproduction. The
guarded-page case also uses this wrapper. No production test substitutions
enter nssm.exe. LSA right grants are always intercepted in these tests.
The real account test only resolves the current account name; it performs one
warm-up lookup before checking handle stability over 30 more calls. Windows'
initial LSA/RPC caching affected an earlier cold-start count, so the fixture
was corrected to measure repeated lookups after initialization.

GUI tests create real hidden Windows dialogs and drive their controls. Popups
and SCM saves are substituted. A save-stub error in the allocation-failure
case was corrected to return the real function's null-input failure. Final
tests check no retained tracked allocations, confirmation/service-password
zeroing and the original handle's ownership through retry and cancellation.
They do not constitute a visible/manual GUI acceptance test or an edit of a
real installed service. Domain-account responses are injected; live domain
coverage remains unverified.

Registry writes use the existing process-local HKLM-to-disposable-HKCU fixture.
The hook test saves a variable referencing a nonexistent program, changes it
to the test executable, and calls the production hook launcher with the final
quoted argument. Only fixture-owned child processes are launched.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All 33 standard CTest groups passed |
| x64 Debug | All 33 standard CTest groups passed |
| x86 Release | All 33 standard CTest groups passed |
| x86 Debug | All 33 standard CTest groups passed |
| x64 AddressSanitizer | All nine selected groups passed: account_lookup, account_rights, gui_retry, gui_password, gui_hooks, install_ownership, exit_export, hooks and registry |
| MSVC `/analyze` Release | Build completed; remaining diagnostics retained |
| GitHub Windows matrix | All four jobs passed; x64 Release passed all 34 groups including real SCM export, others all 33 |

All four jobs passed at commit `a57216b12a6c5e7e65dcc707473399f973f5a2cf`
in [CI run 41526740827](https://github.com/existical/nssm-native/actions/runs/41526740827).
The x64 Release log records `nssm.export_live` passing as test 34 of 34.

No account.cpp analysis warnings remain in this build. Explicit GUI allocation
guards remove the GUI null-object diagnostics; the old uninitialized action
selection is initialized. Large stack-frame and local-shadowing warnings in
GUI code remain, as do previously recorded diagnostics elsewhere, including
the registry MULTI_SZ analysis warning and ignored API results. No warning
was suppressed; this is not an analyzer-clean claim.

Use `scripts/build.ps1 -Architecture x64|x86 -Configuration Release|Debug`
for the full standard matrix. Each new B11 group has a 20-second timeout.
The existing opt-in SCM export test remains CI-only in x64 Release and brings
that job to 34 groups; it does not exercise GUI edits or service lifecycle.
To reproduce the memory checks:

```powershell
cmake -S . -B build/b11-asan -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' `
  '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b11-asan --config Release --parallel
# Add the matching MSVC ASan runtime directory to PATH.
ctest --test-dir build/b11-asan -C Release `
  -R 'nssm.(account_lookup|account_rights|gui_retry|gui_password|gui_hooks|install_ownership|exit_export|hooks|registry)$' `
  --output-on-failure
```

No release artifact or continuation tag is published by this batch. Manual
GUI acceptance, live domain accounts, SCM lifecycle acceptance, legacy Windows
testing and the remaining B12/B13 work remain separate release considerations.
