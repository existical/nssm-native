# NSSM Native

Native C++ Windows service manager, continued from the original
[NSSM project](https://nssm.cc/). NSSM runs an application as a Windows service,
monitors it and can restart it when it exits. It includes a graphical installer
and a command-line interface for managing services.

This repository preserves the upstream commit history and historical release
tags. The continuation currently establishes a maintained source layout,
repeatable builds and automated checks. See [upstream provenance](docs/upstream.md).

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
See [building and testing](docs/building.md) for commands and requirements.

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
| `scripts/` | Build entry point |
| `tests/` | Executable smoke checks |
| `docs/` | Build instructions, usage and upstream records |
| `.github/workflows/` | x86/x64 Debug/Release builds and checks |
| `build/` | Ignored generated projects, resources, symbols and executables |

The current presets use MSVC and the Windows SDK. Runtime compatibility with
older Windows versions has not been revalidated with this toolchain; the legacy
compatibility claims in the upstream manual describe the original builds.

## Contributing and license

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
NSSM is public domain; see [LICENSE](LICENSE). Original author and contributor
credits are retained in the [upstream manual](docs/upstream/README.txt).
