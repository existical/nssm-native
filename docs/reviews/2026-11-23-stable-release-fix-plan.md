# Stable release fixes and verification for NSSM Native 2.25.0

The review found three defects in beta.5 at commit `5534e26`. This checklist tracks their implementation and the Windows 11 acceptance run. [Original findings and reproductions](2026-11-23-stable-release-review.md)

## 1. Preserve launch settings changed while the editor is open

- [x] Add the two-writer case to the hidden-dialog and isolated-registry tests. Open the editor on executable A, change only the description, write executable B through the second writer, then save. Assert that the save succeeds, the description changes and the stored executable is B. Repeat for `AppParameters` and `AppDirectory`.
- [x] At dialog load, retain the original value, registry type and whether each launch setting existed. At save, compare each field with its original display value and submit only effective user edits. A field edited and then reverted counts as untouched. Keep the existing installation defaults and the distinction between a missing directory and an explicitly stored empty directory.
- [x] Before any configuration write, re-read each launch setting the operator changed. If storage still matches the original, apply the requested edit. If storage already matches the requested result, skip the write. Otherwise identify the conflicting field, keep the draft open and let the operator reload and resolve it. Include changes to the registry type in the comparison because they affect variable expansion.
- [x] Test a conflict on the same field, a type-only external change, an explicit clear, a reverted edit and a successful retry after reload. On a disposable service, start it after the unrelated GUI save and verify that executable B actually runs. The comparison detects changes visible at save time. A registry writer outside NSSM can still race between a read and a write.

Implementation entry points are the dialog's load and collection code and the existing launch-parameter backup helper. [GUI](../../src/gui.cpp#L178) · [Registry save](../../src/registry.cpp#L91) · [GUI tests](../../tests/install_gui.cpp)

## 2. Recover an existing service after a failed multi-setting edit

- [x] Promote the review's fault-injection case into the service-edit suite. Start with manual startup and a known dependency and display name. Request disabled startup and replacement values, then fail the parameter save. Assert the returned error **and** the final SCM and registry state. Add a case where the second of two hook writes fails.
- [x] Collect the complete edit before writing. Validate dependencies, account inputs, hook drafts and launch-field conflicts. For each setting the edit will write, read its current state and obtain the required access. Capture presence, type and raw bytes for registry values, including the default `AppExit` value and edited hooks. Abort before the first write if a required snapshot cannot be read.
- [x] Give the existing-service save routine ownership of the edited hooks as well as the parameters and description. Record each successful reversible write and stop on the first failure. Apply these writes first. Preserve the delayed-start helper's ordering and rollback behavior, then make the combined SCM update for startup type, dependencies, display name, service type and any requested credentials the **last required write**. An existing service password cannot be read back for rollback. Keep the install path's delete-on-failure behavior covered separately.
- [x] For an account change, validate the account early and defer any required logon-right grant until immediately before the SCM update. Distinguish a pre-existing right from a newly granted one. If the SCM rejects the credentials after a grant, report any new grant that remains. A right belongs to the account and can be used by another service. Keep passwords out of snapshots, diagnostics and test output.
- [x] On failure, undo completed writes in reverse order. Restore deleted values with their original bytes and types, and remove values introduced by the edit. Before undoing a write, check that it still contains this edit's result. If restoration fails or a concurrent writer changed the value, report the original failure and the specific settings requiring reconciliation. Refresh from storage before retrying a partially restored edit.
- [x] Inject failures at the first and a later parameter write, the description, each edited hook, the delayed-start flag and the final SCM call. Also force one restore failure. Verify clean rollback, actionable partial-rollback diagnostics, and a subsequent successful retry. Repeat a denied registry write and a rejected SCM change against an owned disposable Windows service and query its actual configuration afterward.

In beta.5 the GUI saved hooks after `edit_service()` returns. Moving only the startup write inside that function would still leave a failure window. [Service edit](../../src/service.cpp#L1438) · [GUI hook save](../../src/gui.cpp#L1081) · [Dependency and edit tests](../../tests/dependencies.cpp)

## 3. Match Windows environment-key comparisons

- [x] Add the observed pair `NSSM_Ä=first`, `NSSM_ä=last` to the environment tests. Query both spellings and compare the NSSM result with `GetEnvironmentVariableW` after applying the same block in a test process.
- [x] Use one bounded, Windows ordinal case-insensitive comparison for the name before the first `=`. Apply it to named lookup, append/replacement, removal and the sort used for duplicate detection. Keep value matching case-sensitive for `-KEY=value`. The generic multi-string helper also serves dependency edits, so keep this change specific to environment keys.
- [x] Exercise `AppEnvironment`, `AppEnvironmentExtra` and the native `Environment` setting through the CLI. Cover mixed-case Unicode duplicates, different keys with the same prefix, empty values and values containing `=`. Dump and replay an ordered block with an intervening reference to the first assignment. Compare the stored entry order and verify the values seen by a child process.

In beta.5 the named getter and duplicate detector used separate C runtime comparisons. [Settings](../../src/settings.cpp#L545) · [Append and removal](../../src/env.cpp#L180) · [Live CLI/export tests](../../tests/export_live.cpp)

## 4. Verify the candidate and make the release decision

- [x] Run x86 and x64 Debug and Release tests. Run all five elevated x64 service suites, including the legacy upgrade and rollback, and the repeated lifecycle check. Rebuild the candidate with the release packager. Verify archive checksums, executable identities and the test reports for those exact binaries.
- [x] Correct the upgrade guide's packaged-test count and legacy-build description against the new manifest. Record the candidate commit, executable SHA-256, Windows builds and account types tested. [Upgrade guide](../releases/upgrade-rehearsal.md#L63)
- [x] Exercise a representative deployed application and interactive GUI/account editing on the Windows versions proposed for stable support. Check application readiness, the executable actually launched, restart, stop and rollback. Record any remaining gap against the intended support scope before deciding whether to publish. [Stable release criteria](../releases/README.md#L90)

## Windows 11 candidate results, 24 November 2026

The release packager built commit `edf25ac5c605c9bb01c6a0a67f94523eddc1011a` from a clean checkout on Windows 11 build
26300. The package audit verified the source archive, executables, symbols, SHA-256
manifests and test reports. The four builds used MSVC 19.44.35229 and Windows SDK
10.0.26100.0.

| Build | Standard tests passed | Executable SHA-256 |
| --- | ---: | --- |
| x64 Release | 37 | `2d5fbfd2022933f07f29f06074ef6b0a50e0fb2ad86e7b85ebb7e1a6d910ea5c` |
| x64 Debug | 37 | `602b091eadb45876f43fe1b232a502c90230ef7bb75b53b6cf51245cdfd9960e` |
| x86 Release | 37 | `de51fde4883bb3a2ae550aeb8c2be38e8d4cd0794e38cd899d9127c2b8ea8d1f` |
| x86 Debug | 37 | `d473e5a2f627fc6b31389b45e5d493d3f83f8ff9da6338081c8ffdfb7b380f25` |

The five elevated service suites ran separately on a Windows 11 build 26300 VM
using the executable extracted from the x64 Release archive. Export and replay,
dependencies, controls, the wrapped application lifecycle, and the legacy
upgrade and rollback all passed. The PowerShell worker retained its state and
resumed its heartbeat across the upgrade and rollback. Ten additional consecutive
lifecycle runs passed. The VM reported zero remaining test services.

On the logged-in Windows desktop, the test drove the packaged editor against
disposable services. It checked the stored configuration after a denied registry
write and a rejected SCM change, selected a virtual service account and then
LocalSystem, and saved a description while a second writer replaced the executable.
The replacement started, restarted after a crash and stopped with its descendants.

Windows 11 was the requested scope. Windows PowerShell running the repository's
stateful worker served as the application workload. Customer deployment settings
and domain service accounts were outside this run. Rollback restores registry
values. Creating a new hook or AppExit subkey can leave an empty key after failure.

All three review findings passed their Windows 11 release checks on this candidate.
