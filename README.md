# NSSM Native

Native C++ Windows service manager, continued from the original
[NSSM project](https://nssm.cc/). NSSM runs an application as a Windows service,
monitors it and can restart it when it exits. It includes a graphical installer
and a command-line interface for managing services.

This repository preserves the upstream commit history and historical release
tags. The continuation provides a maintained source layout, repeatable builds,
automated checks and targeted fixes. See [upstream provenance](docs/upstream.md).

See the [fix history](docs/fixes/README.md) for changes already merged,
the [implementation plan](docs/remediation-plan.md) for open review findings,
and [comparing builds](docs/comparing-builds.md) for exact legacy baselines,
behavior differences and release identification.

All 37 recorded review and acceptance fixes are merged and unreleased. The
first beta, `v2.25.0-beta.1`, is being prepared as an unpublished candidate.
See the [beta notes](docs/releases/v2.25.0-beta.1.md) for included changes,
package identities and remaining acceptance work, and the
[QRScanner checklist](docs/releases/qrscanner-beta-acceptance.md) for application
testing before publication. The [completed QRScanner trial](docs/releases/qrscanner-beta-results.md)
passed 24 monitored hours, reboot and exact rollback on Windows 11 x64/LocalSystem;
the report records the test-only helper exception and remaining production
adoption work.

## Build

Install Visual Studio 2022 or its Build Tools with **Desktop development with C++**,
a Windows 10/11 SDK, CMake 3.24 or newer and PowerShell. Git is recommended for
revision-based version information.

From PowerShell in the repository:

```powershell
./scripts/build.ps1
```

This builds and checks the x64 Release executable at
`build/windows-x64/bin/Release/nssm.exe`.

```powershell
# 32-bit Release
./scripts/build.ps1 -Architecture x86

# 64-bit Debug
./scripts/build.ps1 -Architecture x64 -Configuration Debug
```

The same CMake presets work in Visual Studio and in
[GitHub Actions](https://github.com/existical/nssm-native/actions).
See [building and testing](docs/building.md) for commands and requirements,
and [release preparation](docs/releases/README.md) for candidate packages,
checksums and source-archive metadata.

## Use

After building, use the executable from its output directory:

```powershell
./nssm.exe version
./nssm.exe install MyService 'C:\Apps\my-application.exe'
./nssm.exe start MyService
./nssm.exe status MyService
./nssm.exe stop MyService
```

Installing or changing services generally requires an elevated terminal.
See [usage examples](docs/usage.md) and the preserved
[upstream manual](docs/upstream/README.txt) for configuration, log rotation,
environment variables and hooks.

## Project layout

| Directory | Contents |
| --- | --- |
| `src/` | C++ implementation and private headers |
| `resources/` | Windows dialogs, icon, resource IDs and translated messages |
| `cmake/` | Version generation |
| `scripts/` | Build and packaging entry points |
| `tests/` | Regression tests and opt-in live-service/upgrade checks |
| `docs/` | Build instructions, usage, fix history, plans and upstream records |
| `.github/workflows/` | x86/x64 Debug/Release tests, lifecycle acceptance and packaging |
| `build/` | Ignored generated projects, resources, symbols and executables |
| `dist/` | Ignored candidate archives, manifests, checksums and test evidence |

The current presets use MSVC and the Windows SDK. Runtime compatibility with
older Windows versions has not been revalidated with this toolchain; the legacy
compatibility claims in the upstream manual describe the original builds.

## Contributing and license

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
NSSM is public domain; see [LICENSE](LICENSE). Original author and contributor
credits are retained in the [upstream manual](docs/upstream/README.txt).
