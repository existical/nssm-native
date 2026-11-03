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
embedded resource checks under Windows PowerShell 5.1. The acceptance helper
compiled with local SCM execution disabled. Real service results and the
immutable implementation/CI revisions will be recorded after disposable CI.

This acceptance uses a controlled persistent PowerShell worker, not QRScanner
or a user's installed service. It exercises a currently rebuilt legacy binary;
it makes no claim that an original distributed executable was executed. Manual
GUI, domain accounts, older Windows, long-running application soak, signing and
application-specific data migrations remain outside this scenario. No tag or
public release has been created.
