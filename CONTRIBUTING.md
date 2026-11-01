# Contributing

Use a feature branch and open a pull request against `master`. Explain the
problem, the resulting behavior and how the change was checked.

Install the tools listed in [docs/building.md](docs/building.md), then run:

```powershell
./scripts/build.ps1 -Architecture x64 -Configuration Release
./scripts/build.ps1 -Architecture x64 -Configuration Debug
./scripts/build.ps1 -Architecture x86 -Configuration Release
./scripts/build.ps1 -Architecture x86 -Configuration Debug
```

Keep behavior changes separate from large moves or formatting updates. Preserve
the existing source encodings and line endings; `messages.mc` and `nssm.rc`
contain UTF-16 Windows resources and translations. The private logging header is
named `nssm_io.h` to distinguish it from the Microsoft C runtime's `io.h`.

Add a regression check when fixing behavior, and describe any tests that require
administrator rights. Generated headers, projects, executables and debugging
symbols belong in ignored build directories. See [upstream provenance](docs/upstream.md)
for the project's original history and credits.
