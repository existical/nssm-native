# Preparing NSSM Native releases

Release preparation now has automated checks; publication is a separate step.
No continuation tag or public release has been created. All 34 review fixes
are merged and unreleased. Acceptance additionally found NSSM-035 and NSSM-036, repaired in
[PR 20](https://github.com/existical/nssm-native/pull/20), now merged along with
release preparation in [PR 19](https://github.com/existical/nssm-native/pull/19).
All 36 tasks are fixed and unreleased. B15 then identified the startup/STOP
race [NSSM-037](../fixes/NSSM-037.md), repaired in PR 22 and pending merge. See the [candidate notes](candidate.md) and
[fix index](../fixes/README.md).

From a clean checkout with full history and tags:

```powershell
$revision = git rev-parse HEAD
./scripts/package.ps1 -Commit $revision
./tests/package.ps1 -PackageDirectory "dist/candidate-$($revision.Substring(0,12))" -WorkDirectory build
```

The script rejects a different HEAD, tracked/staged changes, untracked files,
shallow history, and an existing output directory. Ignored build output is
allowed. It archives the pinned commit, adds source-info.json, and builds all
four configurations from that fresh source archive. It never packages existing
local executables. It runs the full standard CTest suite for each build and
retains logs and JUnit results. SOURCE_DATE_EPOCH is set to the commit timestamp
for version dates. This does not promise byte-for-byte reproducible binaries.

Use `-EnableScmTests` only on an elevated disposable Windows host. It enables
the four owned-service suites for x64 Release: export, dependencies, native
controls and the NSSM-wrapped application lifecycle. Other configurations stay
unprivileged. The CI `Release packaging` job runs this mode after the four
ordinary matrix jobs pass, then verifies all checksums and source identity.
CI produces five required jobs for this workflow. Failed builds/tests do not
produce a completed package set; diagnostic working directories remain under
build/package-*.

Outputs include four ZIPs (x64/x86 Release/Debug), a source ZIP, an external
build-info.json, SHA256SUMS.txt, and build/test evidence. Binary ZIPs contain
nssm.exe, nssm.pdb, license, documentation and their own build-info.json. The
external manifest hashes the completed archives; it is not embedded in them.
The checksum file covers all other output files, including evidence and the
external manifest. Metadata records the full commit, description/tag, legacy
baseline, actual executable/resource versions, static CRT, compiler, SDK,
CMake, OS, timestamps, CI identity, tests and included fix IDs.

Source ZIPs carry the same full commit and git-describe string in
source-info.json. CMake validates this file and restores the version and
commit-based build date without .git. An ordinary GitHub source ZIP without
this file still uses VERSION as a prerelease fallback and does not identify
an exact continuation revision. Hashes establish file identity, not publisher
authentication; these candidate executables and manifests are unsigned.

For a later release, choose a distinct unused continuation tag. The supported
format includes v2.25.0-beta.1, v2.25.0-rc.1 and v2.25.0; these are examples,
not assigned tags. Preserve all original upstream tags. Prepare docs/releases/<tag>.md from candidate.md and commit the actual
included IDs, compatibility changes, evidence and known limits before tagging.
After review and approval of that concrete candidate, create the tag on the
chosen clean commit. Build with `-Tag <tag>`;
the script verifies that tag and HEAD agree and version generation selects it.
Do not claim a first release in fix records until the tag and matching assets
are published. Upload the complete package set, then verify public checksums
and links before updating first-release fields.

Before a beta: review the automated lifecycle results, the known limitations,
and upgrade instructions below. Before a stable release: exercise representative
real applications, interactive GUI/account configuration and the Windows
versions being advertised. Current automated coverage alone does not establish
support for every historical Windows version or domain configuration.
