# B15 upgrade and rollback validation

Validation date: 2026 November 3. Baseline:
`9812f2ecaf1ccf7ea8788885d0105d62db4312a7`, after PRs 20 and 19 merged.
[Master CI 41546780619](https://github.com/existical/nssm-native/actions/runs/41546780619)
passed all five jobs at that revision. All 36 identified fix tasks are merged
and unreleased; B15 adds acceptance coverage and merge-status documentation,
without changing production C++ behavior or publishing a release.

The [upgrade rehearsal](../releases/upgrade-rehearsal.md) records the exact
scenario and safety boundaries. The legacy baseline is a fresh build of
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, with original source files verified
unchanged. Locally, the x64 Release rebuild passed version, PE architecture and
embedded resource checks under Windows PowerShell 5.1, using MSVC 19.44.35229.0,
SDK 10.0.26100.0 and CMake 4.4.3. Every extracted original file retained its
SHA-256. The acceptance helper compiled with local SCM execution disabled.
Two local worker generations passed file input/environment, persistent state,
readiness and graceful stdout/stderr checks without installing a service.

The test is in [PR 21](https://github.com/existical/nssm-native/pull/21), initially
implemented in `9e26a95f79937fc92f3bec9225390f020a69af76`. Fixture corrections
normalize Windows paths for CI's CMake, pass the complete AppParameters string
as one CLI argument with its inner quotes preserved, and verify working-directory
identity with a unique relative marker. A local probe confirmed that PowerShell
reports the long path when started with an equivalent short path, making direct
text comparison unsuitable. These are test setup corrections, not new NSSM fixes.

The initial stdin dependency was removed because legacy has the already-recorded
[NSSM-012](../fixes/NSSM-012.md) inheritance defect. The worker uses a data file
directly; the candidate's separate stdin regression/lifecycle expectations are
unchanged.

## Live acceptance evidence

At branch revision `681b8f3b4d4635be5c10bc9ddc26714b8e262354`,
[CI 41629118946](https://github.com/existical/nssm-native/actions/runs/41629118946)
passed all four matrix jobs. x64 Release passed 42/42 groups, including the new
upgrade test and all four existing SCM groups. x64 Debug, x86 Release and x86
Debug each passed 37/37. The upgrade test completed in 11.75 seconds on the
disposable Windows Server 2022 runner using MSVC 19.44.35229.0, Windows SDK
10.0.26100.0 and CMake 3.31.6.

The downloaded nssm-upgrade-evidence artifact confirms the following sequence:

| Phase | Observed result |
| --- | --- |
| Legacy, generation 1 | Worker ready/progressing; clean stop; state and log tails retained |
| Candidate, generation 2 | Same installed path and configuration; state retained |
| Candidate crash/restart, generation 3 | Original worker exited; replacement ready/progressing; state retained; clean stop |
| Rollback to legacy, generation 4 | Original version restored; state/configuration retained; clean stop and fixture removal |

All configuration comparisons and process-exit assertions passed. The source
files used to rebuild legacy were unchanged. The candidate exercised by the
matrix identifies GitHub's temporary PR integration commit; source-archive
packaging separately pins the branch head. These are distinct build identities:

| Comparison executable | Source/version and SHA-256 |
| --- | --- |
| Legacy rebuild | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`; `NSSM 2.24-103-gdee49fc 64-bit 2017-05-16`; SHA-256 `01aaf00b07d60fb554cc0cae1f940d75d40c3f3054cc163db22026c68362581e` |
| Candidate matrix build | `dfa604c9b7fe2a40bfab149b4c00ba53f02ba258`; `NSSM 2.24-188-gdfa604c 64-bit 2026-11-03`; SHA-256 `743cbd5b07ee85b87e4b3e3e3ddac92f3dbc91eeab16cab2cebaf94f8a28014d` |

The artifact retains legacy configure/build logs, both identity manifests and
the CTest log. CI artifact retention is temporary; this committed record retains
the observed identities and outcomes, but does not replace permanent release
assets. No bitwise reproducibility is claimed for the historical rebuild.

This acceptance uses a controlled persistent PowerShell worker, not QRScanner
or a user's installed service. It exercises a currently rebuilt legacy binary;
it makes no claim that an original distributed executable was executed. Manual
GUI, domain accounts, older Windows, long-running application soak, signing and
application-specific data migrations remain outside this scenario. No tag or
public release has been created.
