# Building and testing

## Requirements

- Windows with Visual Studio 2022 or Visual Studio 2022 Build Tools.
- The Desktop development with C++ workload, including MSVC x86/x64 tools and
  a Windows 10/11 SDK with `rc.exe` and `mc.exe`.
- CMake 3.24 or newer and Windows PowerShell 5.1 or PowerShell 7.
- Git, when building with commit-based version information.

The presets discover Visual Studio and the selected Windows SDK. A normal
PowerShell terminal is sufficient; local installation paths are not embedded in
the build scripts. Both architectures use a static C runtime (`/MT` for Release,
`/MTd` for Debug). Release builds retain PDB debugging symbols.

## Shared presets

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release
```

Use `windows-x86` to configure the 32-bit build. The build and test presets are
`windows-x64-release`, `windows-x64-debug`, `windows-x86-release` and
`windows-x86-debug`. The wrapper runs all three steps:

```powershell
./scripts/build.ps1 -Architecture x86 -Configuration Debug
```

Output is `build/windows-<architecture>/bin/<configuration>/nssm.exe`, alongside
`nssm.pdb`. Build directories contain generated Visual Studio projects and can be
recreated from the source tree. Machine-specific CMake options belong in an
ignored `CMakeUserPresets.json`.

Open the repository folder in Visual Studio to use CMake integration. For a
different supported Visual Studio installation, configure a separate directory
with its generator, then specify the configuration explicitly:

```powershell
cmake -S . -B build/custom -G 'Visual Studio 18 2026' -A x64
cmake --build build/custom --config Release --parallel
ctest --test-dir build/custom -C Release --output-on-failure
```

The shared presets and CI target Visual Studio 2022. Additional generators need
their own validation before being treated as supported configurations.

## Generated files and versions

The message compiler reads `resources/messages.mc` and writes translated message
tables, `messages.h` and `messages.rc` under the selected build's `generated/`
directory. `resources/nssm.rc` embeds those tables, the icon and version data.
Generated files are not committed.

`cmake/Version.cmake` refreshes `generated/version.h` at build time. When Git
history and version tags are available, it uses `git describe` to retain the
upstream revision format, for example `2.24-104-g2a7aa08`. Rebuilds after a new
commit refresh the revision without requiring manual reconfiguration.

Two-part tags such as `v2.24` and three-part tags such as `v2.25.0` are accepted.
`VERSION` supplies the fallback when Git metadata or suitable tags are absent,
including source ZIP downloads. Keep it aligned with the intended release base.
An archive without revision information is marked prerelease in its Windows
resource flags. Build dates use UTC and honor `SOURCE_DATE_EPOCH` when set.

The historical two-part numeric Windows version layout is
`major,minor,revision,0`. Three-part tags use `major,minor,patch,revision`.

## Checks

CTest runs nine groups:

- `nssm.smoke`: command exit status, exact version output, PE machine type and
  embedded file/product metadata.
- `nssm.environment`: empty values in every position and combination, set and
  unset operations, block termination, and environment expansion.
- `nssm.hooks`: command quoting, expansion, missing/deleted hooks, argument
  parsing, and launching a test executable whose path contains spaces.
- `nssm.startup`: slow startup independent of `AppThrottle`, checkpoint progress,
  stalled and indefinitely pending services, API failures, service exit codes,
  and clock rollover, using a simulated SCM and clock.
- `nssm.registry`: expansion capacity, registry types/termination, empty values,
  allocation/API failures and bounded retries for concurrent value changes.
- `nssm.multisz`: duplicate updates, complete-key matching, bounded formatting,
  round trips and input blocks ending at a protected memory page.
- `nssm.quoting`: zero/exact/insufficient capacities, generated escaping cases,
  dump failure output, and actual child arguments through cmd.exe and a batch
  file with delayed expansion disabled.
- `nssm.numeric`: checked DWORD parsing, prefix/complete-input contracts,
  rejected-setting preservation and AppExit numeric boundaries.
- `nssm.affinity`: range endpoints, repeated entries, mask/string round trips,
  formatter allocation failure, injected CPU masks and real hidden list-box
  row/CPU mapping.

The regression executable compiles the production implementations with test-only
wrappers for the entry point, SCM, registry queries, expansion, allocations
and dump output. CPU masks and the affinity formatter's allocation failure
are also injected only in the regression executable.
Tests do not install or control real
services. Registry access is redirected within the test process to a temporary
HKCU key, which is deleted afterwards; the hook test also creates and removes a
copy of its executable under the build directory. The quoting test creates and
removes a similar copy and an ASCII batch file there. Environment changes are local
to each test process. Affinity tests create and destroy a hidden Windows list
box; they do not display a service editor. No administrator rights are required.

The B01 [validation record](reviews/b01-validation.md) includes the observed
pre-fix failures, full build matrix, static analysis and separate
AddressSanitizer commands.
The B02 [validation record](reviews/b02-validation.md) records quoting bounds,
shell round trips, caller error handling and the existing batch-file limitations.
The B03 [validation record](reviews/b03-validation.md) covers numeric bounds,
affinity parsing, native-width masks and GUI row mapping.

The startup progress checks follow the SCM checkpoint/wait-hint contract in
[Microsoft's SERVICE_STATUS reference](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status),
with an additional total polling limit documented in [usage](usage.md).

GitHub Actions runs x86 and x64 in Debug and Release. It checks out complete Git
history and tags, then uses the same build script and CTest checks. Executables
and PDBs are attached to the workflow run as artifacts.

Pointer-size conversion diagnostics C4302, C4311 and C4312 are treated as errors
when compiling application C++ sources. Fixes to service behavior and legacy
Windows compatibility require separate changes and regression validation.
