# Preparing NSSM Native releases

The first public continuation release is [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1), published as a beta on
5 November 2026. All 37 identified fixes and CONT-001 through CONT-005 are
included. The original tag and packages retain tested commit
`739b5feabdc768a4524fe28bad2c000b05ee5e07`.
See the [release notes](v2.25.0-beta.1.md),
[verified publication record](v2.25.0-beta.1-publication.json),
[candidate history](candidate.md), [fix index](../fixes/README.md) and
[QRScanner acceptance checklist](qrscanner-beta-acceptance.md).

PR 23 merged at `b15004563498180d9f544ddbfbbecb7d98289656`;
[post-merge CI](https://github.com/existical/nssm-native/actions/runs/42001713362)
passed all five jobs before publication. Subsequent documentation changes
record the release without changing the tested package bytes.

The [completed QRScanner record](qrscanner-beta-results.md) now includes a passed
24-hour x64/LocalSystem trial, reboot and exact legacy rollback. Its test-only
helper exception and separate production adoption requirement remain explicit.
The candidate tag and packages retain their original tested identities.

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
nssm.exe, nssm.pdb, license, README, changelog, contributing guide, documentation
and their own build-info.json. The
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
authentication; the first beta executables and manifests are unsigned.

Preserve all original upstream tags. Prepare docs/releases/<tag>.md and commit
the actual included IDs, compatibility changes, evidence and known limits
before tagging. Candidate preparation may use an unpublished local annotated
tag on that clean commit so the actual beta version can be built and tested.
The first candidate uses `v2.25.0-beta.1`; later RC/stable names remain unassigned.

```powershell
# Run only at the candidate commit, after the local tag has been prepared.
$revision = git rev-parse HEAD
./scripts/package.ps1 -Commit $revision -Tag v2.25.0-beta.1
./tests/package.ps1 -PackageDirectory dist/v2.25.0-beta.1 -WorkDirectory build
```

The script verifies that tag and HEAD agree and version generation selects it.
Local packages run 37 unprivileged groups in each configuration. Ordinary PR
CI sees only remote tags, so its commit-labelled artifacts can have a different
version from the locally tagged beta at the same source commit. Keep the two
identities and their validation evidence distinct. Supplied source archives
retain their own exact identity and do not require the local tag to rebuild.

Review the concrete candidate and application results before publishing the
tag or release. Keep its tag on the reviewed source commit when merging the
preparation PR; do not move it to a different merge commit. If the candidate
source changes, use a new candidate version and rebuild it. Later documentation
commits may add acceptance results without moving that tag or rebuilding its
archives; provide the follow-up record with the release notes. Public publication
must upload the archives matching that exact tag, not ordinary PR artifacts.
Do not claim a first release in fix records until the tag and matching assets
are published. Upload the complete package set, then verify public checksums
and links before updating first-release fields.

Before a beta: review the automated lifecycle results, the known limitations,
and the [upgrade/rollback rehearsal](upgrade-rehearsal.md). Before a stable release: exercise representative
real applications, interactive GUI/account configuration and the Windows
versions being advertised. Current automated coverage alone does not establish
support for every historical Windows version or domain configuration.

For the first beta, the complete `nssm-native-v2.25.0-beta.1-candidate.zip`
retains the original 25 checksummed files and `SHA256SUMS.txt`, including the
`evidence/` directory. The release also provides individual archives and the
manifest. `RELEASE-ASSETS-SHA256SUMS.txt` covers the separately uploaded assets,
including supplemental release notes and sanitized acceptance summaries.
All 13 assets were downloaded anonymously and their hashes verified after
publication. Original archives retain the earlier preparation documentation;
the supplemental acceptance and publication records state the later outcomes.
