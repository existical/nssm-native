# Post-remediation code review and implementation plan

Review baseline `e5edea8`, 19 November 2026. Reviewed the service lifecycle and controls, logging, hooks, configuration and GUI persistence, process handling, account handling, packaging, and CI. All 148 standard test executions and all five elevated SCM suites passed at this revision.

| Priority | Finding and evidence |
| --- | --- |
| High | With online copy/truncate enabled, the logger opens a positional write handle. Before any rotation, writing `EXTERNAL\n` and then `LOCAL\n` via the logger left `LOCAL\nAL\n`. [Handle selection](../../src/io.cpp#L292) |
| High | A writer may append after the archive copy and before truncation. The two operations have no shared exclusion or check for intervening changes. Earlier rotation documentation mentions external writers. [Offline rotation](../../src/io.cpp#L427) · [Online rotation](../../src/io.cpp#L855) |
| High | A 17,000-character AppParameters setting is accepted and exported. The runtime reads it into a 16,383-character buffer, then starts the child with zero arguments. Confirmed through the production setter and runtime parameter reader. [Runtime read](../../src/registry.cpp#L827) |
| High | Under a console-less launcher, dumping a Unicode value exited successfully but its UTF-8 text was missing. The CLI only configures Unicode output when it detects a console window. [CLI initialization](../../src/nssm.cpp#L242) |
| Medium | A startup copy delay can exceed the last SCM wait hint without increasing its checkpoint. A client can report failed startup while the wrapper continues. [Delay](../../src/io.cpp#L435) · [Client wait](../../src/service.cpp#L175) |
| Medium | A missing or empty AppDirectory is synthesized while loading the GUI and persisted on save. With an environment variable containing the executable path, an unchanged save switched the effective working directory from the application directory to the Windows directory. [Fallback](../../src/registry.cpp#L832) · [GUI](../../src/gui.cpp#L524) |
| Medium | A present, empty AppEnvironment intentionally clears inherited variables. Dump emits no command, and an unrelated GUI save can delete that replacement. Confirmed that the production environment setter removed an inherited marker and live export omitted the setting. [Dump](../../src/settings.cpp#L581) · [GUI](../../src/gui.cpp#L818) |
| Medium | Removal elevates unconditionally for non-administrators even when the service grants DELETE. Several entry points silently truncate service names and the remove path omits allocation and early-return cleanup. [Dispatch](../../src/nssm.cpp#L293) · [Removal](../../src/service.cpp#L1262) |
| Medium | An unreadable hook subkey is treated as a successful reset. The startup setter also returns success when the delayed-start update fails, and its query helper leaks a buffer on a failed second read. [Hook deletion](../../src/registry.cpp#L1038) · [Delayed start](../../src/settings.cpp#L1140) |
| Medium | The hook display name is populated only for administrators, even though the lookup needs manager-connect access. [Service initialization](../../src/service.cpp#L1646) |
| Medium | Pull-request upgrade evidence is generated from the synthetic merge revision while packaging selects the head revision. The package job omits the legacy fixture and the verifier does not require symbol-file entries. [Workflow](../../.github/workflows/ci.yml) · [GitHub checkout behavior](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#how-the-merge-branch-affects-your-workflow) |

## Implementation checklist

- [x] **Log writers.** Use append-at-write handles for ordinary logger writes in copy/truncate mode. Protect truncation against concurrent changes and retain both the active data and the archive on a conflict. Test two independent writers and the rotation failure path.
- [x] **Startup progress.** Report checkpoints during configured offline waits and retain prompt stop cancellation. Verify with a delay longer than the initial wait hint.
- [x] **Launch configuration.** Align argument capacity across set, get, dump and launch. Fail startup on unreadable configured arguments. Preserve dynamic working-directory fallback on unrelated editor saves.
- [x] **Portable backups.** Configure redirected Unicode output for headless callers. Give empty replacement environments a replayable representation and preserve them through the GUI. Exercise Unicode and empty-value cases.
- [x] **Service management.** Validate service names before storage, honor delegated remove rights, handle allocation and cleanup on removal, and populate hook display names for standard service accounts.
- [x] **Configuration errors.** Propagate hook deletion and delayed-start failures, and release the delayed-start query buffer on every path.
- [x] **Release evidence.** Use a consistent revision for pull-request CI, run upgrade/rollback on the opted-in package binary and require its evidence and executable/symbol identities. Preserve verification of existing package formats.
- [x] **Dialog name boundaries.** The final inspection found that the install, edit and remove dialogs read service names into a fixed buffer before checking their lengths. An oversized input can select its shorter prefix, and the buffer also excludes the last character of a full-length display name. Allocate room for the terminator, reject oversized input before any service operation, and test both boundaries. [Windows service and display name limits](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew)
- [x] **Acceptance.** Review the complete diff, run x86 and x64 Debug and Release, verify candidate packages, and run elevated Windows configuration, control, lifecycle and upgrade/rollback checks plus the targeted regressions. Record the tested revision and results.

## Acceptance results

Tested source commit `3beb9a70c5bdfc0d47e487680a3e3a649f741c62` with MSVC 19.44.35229.0 and Windows SDK 10.0.26100.0. The x64 Release executable used on the VM had SHA-256 `a34fff7e7a755ea55b3b034e4932e7f4efcfd07d82306b0ee92a0c5e3fcce3e5`.

| Check | Result |
| --- | --- |
| Four clean-source configurations | x86 and x64, Debug and Release, each passed all 37 CTest groups. The four JUnit reports recorded 148 passes and zero failures or disabled cases. |
| Package verification | Verified binary and PDB hashes, archive identity, checksums and test evidence for all four builds. An existing schema-1 candidate also passed the new verifier. The opted-in CMake configuration registered 42 tests, including upgrade/rollback. |
| Live Service Control Manager tests | Windows 11 build 26300 passed all five suites against the packaged x64 executable. They exercised export and replay, dependencies, controls, lifecycle, and upgrade and rollback with a fresh build of pinned legacy revision `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`. |
| Original data-loss cases | Independent production-object checks preserved both writers’ bytes and the effective working directory after an unchanged configuration save. All 17,000 argument characters reached the runtime parameter reader. The VM preserved Unicode and restored an explicitly empty replacement environment. |
| Rotation and permissions | The start command returned success after an eight-second offline rotation delay. A write flushed during the delay survived in the active file, and the archive retained its original contents. A LocalService hook received all 256 characters of the configured display name and the account successfully removed its delegated test service. The fixture inventory matched its starting state. |

The hosted GitHub Actions workflow was reviewed locally. The clean-source package matrix ran on the workstation and the privileged tests ran separately on the disposable VM. Copy/truncate now reads both the original data and the archive before clearing a matching file, which increases I/O for large logs.
