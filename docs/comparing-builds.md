# Comparing NSSM Native with legacy NSSM

Use an exact source revision and build identity when comparing behavior.
The [fix index](fixes/README.md) lists implemented changes, the
[implementation plan](remediation-plan.md) lists open work, and the
[changelog](../CHANGELOG.md) summarizes what changed. A planned fix is not
included merely because a build was produced after the review.

## Select the legacy baseline

| Baseline | Source identity | Meaning |
| --- | --- | --- |
| Original v2.24 release tag | `220d2b0e952fe81ebf0acfad96371dce46eedf93` | Commit resolved from the preserved annotated `v2.24` tag |
| Recovered development snapshot | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` | `v2.24-103-gdee49fc`, the source from which this continuation began |
| Reviewed continuation | `5d7454b2cd878b66bcd4b036c33a5b53b184cf3a` | Includes CONT-001 through CONT-005; the full review's code baseline |
| A distributed executable | Version output plus SHA-256, architecture and provenance | Its label alone does not prove that its source matches one of these commits |

Historical tags were recovered as described in [upstream provenance](upstream.md).
Keep those tags unchanged. This repository does not certify the contents of
arbitrary third-party executables named `nssm.exe`.

Fix records must state whether a legacy version was inspected as source,
rebuilt with a specified toolchain, or tested as a particular distributed
binary. Record SHA-256 and origin for a tested binary. Do not extrapolate one
binary's results to all builds called 2.24-103.

## Compare source despite directory moves

These PowerShell commands use existing Git objects and do not modify services
or the checkout:

```powershell
$legacy = 'dee49fc1a0804f06687299b8e9612a3fce1c5e9a'
$candidate = '5d7454b2cd878b66bcd4b036c33a5b53b184cf3a'

git show -s --format='%H %s' $legacy $candidate
git describe --tags --long $legacy
git rev-parse 'v2.24^{commit}'
git log --oneline ($legacy + '..' + $candidate)
git diff --find-renames=40% --stat $legacy $candidate

# Original files were at the repository root; current code is under src/.
git diff ($legacy + ':registry.cpp') ($candidate + ':src/registry.cpp')
git diff ($legacy + ':env.cpp') ($candidate + ':src/env.cpp')
git diff ($legacy + ':service.cpp') ($candidate + ':src/service.cpp')
```

Substitute the candidate's full commit hash when evaluating a newer build.
The threshold helps Git recognize moves; explicit old/new paths work even
when a heavily edited file is no longer detected as a rename. Avoid a
`-- src/`-only comparison when trying to see moves from the original layout.

The [GitHub commit comparison](https://github.com/existical/nssm-native/compare/dee49fc1a0804f06687299b8e9612a3fce1c5e9a...5d7454b2cd878b66bcd4b036c33a5b53b184cf3a)
is convenient for history. Use the explicit file comparisons for details and
the fix records for user-visible consequences.

To check whether a candidate contains a recorded implementation:

```powershell
$fixCommit = '8fede4b7421525cea125ef1930a3b53faf123065'
git merge-base --is-ancestor $fixCommit $candidate
$LASTEXITCODE # 0: ancestor, 1: not an ancestor, other: command error
```

Ancestry establishes inclusion of the commit, not proof that no later commit
reverted or changed its behavior. Read subsequent history and run the named
regression test when that distinction matters.

## Compare behavior

1. Pick a fix ID and use its record to identify the triggering configuration
   and expected before/after behavior.
2. Pin both source revisions or both executable hashes. Record architecture,
   configuration, Windows version, compiler/SDK, runtime linkage, environment
   and relevant registry values.
3. Run the same bounded test scenario against each candidate. Use an isolated
   temporary registry fixture for pure configuration tests and disposable
   Windows services/VMs for service-control tests. Capture exit status, log
   bytes, service states and elapsed time as applicable.
4. Store a concise comparison result in the fix record: baseline outcome,
   corrected outcome, test name and limitations. Label API injection or
   source-only inspection accurately.

If building legacy source requires a modern build adapter, keep that
adapter separate, record its patch and toolchain, and verify that it contains
no runtime fixes. Describe the result as a rebuilt legacy source comparison.
It is not a byte-for-byte test of the historic release executable.

The current regression harness is part of this continuation. A harness or
compiler adjustment needed to exercise an old revision must be documented,
not silently counted as a legacy code fix.

## Identify binaries and releases

For a local executable, retain its version output and file hash:

```powershell
$binary = '.\build\windows-x64\bin\Release\nssm.exe'
& $binary version
Get-FileHash -LiteralPath $binary -Algorithm SHA256
git rev-parse HEAD
git status --porcelain
```

Current generated version strings include Git revision information when
available, but version text alone does not establish a clean checkout.
The hash identifies the exact file; a release manifest must tie it to source
and build evidence. Different compiler versions or build timestamps can
change hashes without changing the intended behavior.

The [packaging script](../scripts/package.ps1) emits `build-info.json` and
`SHA256SUMS.txt` with the candidate archives. See [release preparation](releases/README.md)
for clean-commit requirements, source identity, tests and publication steps.
The ordinary build script still produces local executables only.
The manifest must contain:

| Field | Required content |
| --- | --- |
| Source | Repository URL, full commit, tag, clean-tree status and legacy baseline |
| Version | Actual `nssm.exe version` output and embedded file/product versions |
| Build | Architecture, Debug/Release, CRT mode, compiler, SDK, CMake and build OS |
| Provenance | UTC build date, SOURCE_DATE_EPOCH if used, workflow/run identity |
| Artifacts | Names and SHA-256 for executable, symbols and archive |
| Validation | Build/test matrix results and relevant integration/diagnostic results |
| Changes | Included fix IDs, first-release assignments and known open issues |

Hash the finished archive in the external checksum file/manifest; do not
attempt to embed an archive's own hash inside itself. Generate metadata from
the clean tagged checkout, with revision metadata also supplied for source
archive builds. Retain release notes in `docs/releases/<tag>.md` and publish
matching release assets. CI artifacts can expire; the explanation of fixes
must remain in Git.

No continuation release tag is assigned by this documentation change.
Version generation, source-archive identity, dirty-tree checks and packaging
automation are covered by B14; permanent release publication remains separate.
