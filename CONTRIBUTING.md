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

For behavior fixes, use the stable ID from the
[implementation plan](docs/remediation-plan.md), or assign a new ID for a new
finding. Include a record using the [fix template](docs/fixes/TEMPLATE.md),
link it from the [fix index](docs/fixes/README.md), and add a changelog entry
in the same PR. Explain the triggering case, old and corrected behavior,
exact baseline checked, regression tests, compatibility changes and release
status. Mark source inspection, injected tests and live reproduction accurately.

The [comparison guide](docs/comparing-builds.md) distinguishes the original
v2.24 tag from the later 2.24-103 development snapshot. A current-source defect
does not establish that every legacy binary is affected. Existing fixes are
documented in [implemented changes](docs/fixes/implemented.md).

Use a PR link until the implementation commit is final; record the immutable
hash after merge in the next documentation or release update. Do not mark an
open PR fixed, or an untagged build released. Release notes must list included
fix IDs and remaining known issues, with metadata tying published binaries to
their source and test results.
