# B14 release preparation validation

Validation date: 2026 November 3. Baseline:
`0f856a4dedb7e5c9dc08fc010fb69bf90398a05d` (PR 18 merged).
This work prepares candidates; no continuation tag or release is published.

Scope: real NSSM-wrapped application lifecycle on a disposable elevated host;
clean pinned source packaging; legacy/prerelease version generation; source
archive identity; four binary/symbol ZIPs, manifests, checksums and test evidence.

Initial checks passed for legacy/stable/beta/RC/post-tag version generation,
source archive/header equivalence and rejection of malformed identity. Packaging
rejection cases passed for wrong revision, untracked, unstaged and staged
changes. All test tags exist only in disposable local fixtures; upstream tags
were not changed.

The lifecycle fixture uses one uniquely created wrapper service and an owned
temporary directory. Its helper reads stdin and checks an environment value
after an empty entry, spawns a descendant, crashes once, restarts, then completes
two stop/start cycles. Start/Post, Stop/Pre and Exit/Post commands use a helper
path containing spaces and a final quoted argument. It checks final stdout/
stderr, hook output and retained process handles after shutdown. Cleanup touches
only owned objects; children have 45-second watchdogs. No existing local service
is created, reconfigured, started or stopped by unprivileged tests.

## Acceptance findings and repairs

The first fixture failure was a setup error: the fixture needed to establish
its Application value before using the public configuration commands. After
that correction, real wrapper execution exposed two release blockers:

- [NSSM-035](../fixes/NSSM-035.md): startup stopped with service-specific code 2
  because standard-I/O configuration passed a character count to a byte-capacity
  API. B01's stricter registry validation exposed this continuation regression.
  New configuration-reader cases failed 28 checks before the correction in
  `3146ceca98065907f5f4b5d999139e287065b2ed`.
- [NSSM-036](../fixes/NSSM-036.md): STOP during automatic restart could publish
  STOPPED before concurrent exit cleanup completed. The live fixture observed a
  surviving descendant and missing hook output in
  [CI 41542921251](https://github.com/existical/nssm-native/actions/runs/41542921251).
  A forced-overlap test failed two checks before the cleanup ownership repair in
  `024bfdb59d5ad60f0b2a7d716c77330ee16260c4`. Legacy source contains the same
  unsynchronized early-return pattern; an original executable was not run.

Both repairs are isolated in [PR 20](https://github.com/existical/nssm-native/pull/20),
merged as `aff12fa057227f93453cf014a0d931f2ea2c930b` before
[PR 19](https://github.com/existical/nssm-native/pull/19), merged as
`9812f2ecaf1ccf7ea8788885d0105d62db4312a7`.
At `4a03eeecbaf59059df3e1e6d82700700a28429da`,
[CI 41544366647](https://github.com/existical/nssm-native/actions/runs/41544366647)
passed all four repair-branch jobs: 38 groups in x64 Release and 35 in each other
configuration. Local x64/x86 Debug/Release also passed all 35 standard groups.
AddressSanitizer passed launch, hook_lifecycle, logger, stdin and control_waits
(5/5). MSVC /analyze completed with existing warnings; no analyzer-clean claim
is made. Detailed reproduction and attribution remain in the individual records.

## Package and lifecycle results

The complete release-preparation revision
`6bd6b99cf32090b839bd418cb827a69cead01671` passed
[CI 41544419568](https://github.com/existical/nssm-native/actions/runs/41544419568):

| Check | Result |
| --- | --- |
| Matrix x64 Release | 41/41 groups, including all four isolated SCM suites and the wrapped lifecycle |
| Matrix x64 Debug, x86 Release, x86 Debug | 37/37 groups in each configuration |
| Fresh source archive package build | All four configurations built and tested again; 41/41 in x64 Release, 37/37 in each other configuration |
| CI package verification | All checksums, executable/resource versions, symbols and source identity passed |
| Downloaded CI artifact verification | Passed locally with tests/package.ps1; full commit, 36 fix IDs, four configurations and zero failures confirmed |

The downloadable `nssm-native-candidate-packages` artifact contains four binary/
symbol ZIPs, one source ZIP, manifests, SHA256SUMS.txt and build/JUnit evidence.
Its source identity is the pinned revision above, not GitHub's temporary PR
merge revision. CI used Visual Studio 2022, MSVC 19.44.35229.0, Windows SDK
10.0.26100.0 and CMake 3.31.6 on Windows Server 2022 (10.0.20348.0).
Artifacts and raw CI logs can expire; this committed record preserves the
observations, while an eventual release must publish permanent matching assets.

A local clean-source package run at
`f741c807c04bce5e7169c30b881f29a6db786a31` also passed all 37 standard groups in
each of the four configurations and passed package verification under Windows
PowerShell 5.1. That earlier revision included NSSM-035; the complete CI revision
above additionally includes NSSM-036. Windows PowerShell 5.1 checks also cover
the final fixture cleanup, which removes only each successful test's generated
Git metadata to avoid nested repositories in editor discovery. SCM tests were
not run on the local development host.

All 36 tasks and release preparation are now merged and unreleased.
[Post-merge CI 41546780619](https://github.com/existical/nssm-native/actions/runs/41546780619)
passed all five jobs at `9812f2ecaf1ccf7ea8788885d0105d62db4312a7`. No tag or release has
been created by this validation. Local diagnostic logs and downloaded packages
stay under ignored build/dist directories.

Known limits: the fixture is a small synthetic application, not a production
soak or exhaustive interleaving test. Manual GUI, domain accounts, older Windows,
signing, installed-system upgrade/rollback and large process/hook populations
remain release acceptance considerations. SOURCE_DATE_EPOCH controls version
dates, not every compiler/PDB/archive timestamp; bitwise reproducibility is not
claimed. No historical executable is executed by B14.
