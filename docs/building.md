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

CTest runs `tests/smoke.ps1`. It checks the command's exit status and exact version
output, the PE machine type, and the embedded file/product metadata. These checks
run without installing services or changing registry configuration.

GitHub Actions runs x86 and x64 in Debug and Release. It checks out complete Git
history and tags, then uses the same build script and CTest checks. Executables
and PDBs are attached to the workflow run as artifacts.

Existing upstream pointer conversion warnings in `settings.cpp` remain visible.
Fixes to service behavior and legacy Windows compatibility require separate
changes and regression validation.
