# Upgrade and rollback rehearsal

The B15 acceptance test rehearses replacing the executable of an existing
service, without reinstalling it or rewriting its settings. It runs only on an
explicitly enabled disposable elevated Windows host. It does not upgrade any
user application or existing service on the development machine.

The comparison baseline is the preserved `2.24-103-gdee49fc` source at
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a`. tests/build-legacy.ps1 archives that
commit and uses a separate CMake adapter to build x64 Release with the current
MSVC/Windows SDK. It hashes every original file before and after building and
requires an unchanged source tree. Version/architecture/resource smoke checks
run before the comparison. build-info.json records the source and executable
hashes, commit, version and toolchain.

This is a test-only rebuild, not a downloaded official legacy executable. The
adapter supplies generated messages/version metadata and current build flags;
compiler/runtime differences mean this cannot establish behavior of every
historically distributed NSSM binary. It does not add the flat source root to
the compiler include path, which would shadow the CRT's io.h.

The scenario uses one uniquely named NSSM-Upgrade-Test service, one temporary
directory, and a PowerShell background worker with a persistent counter:

1. Configure the disabled service with the legacy CLI. Use a stable wrapper
   path containing spaces, a script path containing spaces, an input data file,
   an environment value containing a space, stdout/stderr logs and a graceful
   stop hook. Enable only this owned fixture and export its configuration.
2. Start legacy, verify application readiness and advancing heartbeats, then
   stop it. Wait for both the application and wrapper process handles to exit.
3. Replace only the stopped fixture's executable with the candidate. Verify
   its actual version, retained application state and advancing heartbeats.
   Crash the worker once and verify automatic restart with retained state.
4. Stop, wait for both process exits, restore the legacy executable at the same
   path, and run the worker again. No reinstall or settings migration occurs.
5. After each phase compare all Parameters keys/value bytes/types plus the
   SCM service type, start mode, error mode, executable path, account, display
   name, load-order group and dependencies. Verify persistent state and final
   stdout/stderr output. Delete only the fixture service and owned files.

The worker has a 60-second watchdog; service cleanup and readiness waits are
bounded and CTest allows 180 seconds. PID files are used only to retain process
observer handles. The test never terminates an arbitrary PID. The graceful
stop hook ends with an unquoted marker so the baseline does not hit its known
hook quote-stripping defect. The worker reads an input file directly: legacy
AppStdin is not inherited correctly ([NSSM-012](../fixes/NSSM-012.md)), so requiring
it would reject the known baseline before any upgrade. Empty environment entries,
stdin inheritance and other corrected behavior belong to the separate candidate
lifecycle/regression suites. Baseline limitations are not weakened expectations
for the candidate's existing regression tests.

On a disposable elevated test host, after a complete-history checkout:

```powershell
./tests/build-legacy.ps1 -OutputDirectory build/legacy-baseline
$legacy = (Resolve-Path build/legacy-baseline/build/bin/Release/nssm.exe).Path
cmake --preset windows-x64 -DNSSM_ENABLE_SCM_TESTS=ON "-DNSSM_LEGACY_EXECUTABLE=$legacy"
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release -R '^nssm.upgrade_live$'
```

The output directory must not already exist. Ordinary builds keep SCM tests
disabled. In CI, only matrix x64 Release supplies the legacy executable and
runs this additional group (42 groups total). Source-archive packaging retains
41 groups for x64 Release because it carries no historical Git objects or
legacy executable. All other configurations run 37 groups. CI uploads the
legacy build metadata/logs, candidate identity and CTest output as
`nssm-upgrade-evidence`; candidate release packages never include the legacy
executable. The existing crash/restart/STOP lifecycle also repeats ten times
after the matrix suite; its logs/JUnit report are retained separately as
nssm-lifecycle-repeat-evidence. matrix-tests.log preserves the full suite before
the repeat overwrites CTest's LastTest.log.

See [B15 validation](../reviews/b15-validation.md) for actual outcomes. Passing
this controlled PowerShell scenario is not a QRScanner deployment, production
soak, database migration, domain-account test or a guarantee of rollback for
every third-party application's data format. Before deployment, retain the
actual installed binary/hash, exported service settings and application data;
rehearse the same replacement with that application's readiness and data checks.
