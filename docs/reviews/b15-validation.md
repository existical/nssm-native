# B15 upgrade and rollback validation

Validation date: 2026 November 3. Baseline:
`9812f2ecaf1ccf7ea8788885d0105d62db4312a7`, after PRs 20 and 19 merged.
[Master CI 41546780619](https://github.com/existical/nssm-native/actions/runs/41546780619)
passed all five jobs at that revision. All 36 identified fix tasks are merged
and unreleased. B15 adds acceptance coverage and merge-status documentation.
A later rerun exposed NSSM-037; the focused production repair in PR 22 is now
included in this acceptance branch. No release is published.

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

## Initial live acceptance evidence

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

## Startup/STOP follow-up

The documentation-only rerun at `bd94d75db99855a34f05933ef5f564ca20acb26b`
passed the upgrade test but the existing lifecycle test reported unexpected
wrapper termination during STOP while restarting in
[CI 41629829699](https://github.com/existical/nssm-native/actions/runs/41629829699).
That failure was not accepted as a successful run or bypassed by retrying.

The focused [NSSM-037 record](../fixes/NSSM-037.md) documents six deterministic
startup cancellation/wait ownership failures, repair commit
`ab23475f71d700e1705d5be1681c49eb72775466`, local four-configuration validation
and five sanitizer groups. [PR 22](https://github.com/existical/nssm-native/pull/22)
must merge before PR 21. The acceptance branch includes the repair and repeats
the real lifecycle ten consecutive times on CI, stopping at the first failure.
The earlier comparison hashes above identify the pre-NSSM-037 run, not the
updated candidate.

At combined branch revision `9e5a139784bddd59e6335f48bb48f99be171307e`,
[CI 41632720465](https://github.com/existical/nssm-native/actions/runs/41632720465)
passed 42/42 groups in x64 Release and 37/37 in each other configuration. The
upgrade/rollback test passed again in 14.26 seconds with generations 1-4 and
the same configuration/state/cleanup assertions. Ten additional consecutive
lifecycle runs passed; the downloaded repeat log contains ten passed records.
The full matrix log is retained as matrix-tests.log before CTest writes the
separate repeat log and JUnit result.

| Repaired-run comparison executable | Identity |
| --- | --- |
| Legacy rebuild | Source `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`; SHA-256 `9a0356554497b816926fa67f462d9f4dc83f722ff090da2af9b6a9d2131c8a1f`; original source files unchanged |
| Candidate matrix build | Temporary PR integration commit `ff78d37ce3f29659a947308f306c3fedc805e4f1`; version `NSSM 2.24-192-gff78d37 64-bit 2026-11-03`; SHA-256 `9b25961ad4bb7d1227452c40e421cc2ef719f4d0305689feb24a7f68866adb7d` |

The focused repair separately passed its four-configuration matrix and ten
consecutive lifecycle repetitions in
[CI 41632491156](https://github.com/existical/nssm-native/actions/runs/41632491156)
at `d995289fd939461839bbd89d464e21eeb922d204`. Source-archive packaging checks are
part of the same linked workflows; package manifests identify their exact
branch revisions separately from the temporary integration builds above.

This acceptance uses a controlled persistent PowerShell worker, not QRScanner
or a user's installed service. It exercises a currently rebuilt legacy binary;
it makes no claim that an original distributed executable was executed. Manual
GUI, domain accounts, older Windows, long-running application soak, signing and
application-specific data migrations remain outside this scenario. No tag or
public release has been created.
