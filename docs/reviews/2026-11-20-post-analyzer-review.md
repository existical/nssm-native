# Code review after the MSVC warning repairs

Reviewed baseline `fede1ec` on `review/beta4-followup`. The review covered service management and callbacks, child and hook supervision, logging, configuration, the GUI and CLI, and the build and packaging checks. The following findings have observable failure paths. Source locations refer to the baseline.

| Priority | Location | Finding |
| --- | --- | --- |
| High | `src/io.cpp:452` | Offline rotation discards both age and size thresholds when opening the file or reading its metadata fails. A rename or copy can then proceed on a file whose eligibility was never established. |
| High | `src/gui.cpp:754`, `src/gui.cpp:854` | Missing controls appear empty. A short nonzero read also passes validation. Saving can erase dependencies or replace the service environment with a partial list. Credential and display-name reads have related gaps. |
| Medium | `src/env.cpp:146`, `src/settings.cpp:522` | Checking a configuration starts and then terminates a suspended wrapper. A termination failure can leave a suspended process behind. Process creation also does not enforce the editor's required `KEY=VALUE` syntax for every entry. |
| Medium | `src/nssm.cpp:273`, `src/service.cpp:1326` | Startup ignores module-path lookup errors and truncation. Installation copies a shell-quoted path with truncation enabled. The resulting registered image can differ from the running wrapper. |
| Medium | `src/service.cpp:1499` | The CLI includes the service identifier among the arguments supplied to the SCM. Check what a native service actually receives on Windows for plain start, extra arguments and restart. |
| Medium | `src/nssm.cpp:287`, `src/service.cpp:1219` | Several read-only CLI commands return success when stdout fails. Scripts can accept missing version, setting, status or process-list data. The existing list and dump commands already detect this case. |
| Medium | `tests/package.ps1:81` | The verifier checks the inner source commit but ignores the source identity in each outer build record. Conflicting provenance can pass the release check even when all individual file hashes match. |

## Implementation plan

- [x] Stop offline rotation on metadata errors. Retain the append fallback and protect destructive dispositions. Test both archive modes and exact source bytes.
- [x] Read GUI lists and credential fields using checked lengths and complete copies. Preserve deliberately empty input. Exercise short reads and missing controls through the editor.
- [x] Validate every configured environment entry directly before saving. Preserve empty blocks, empty values, embedded equals in values and existing runtime handling of older configurations. Replace process-based validation tests with input and persistence checks.
- [x] Read the wrapper image once, check bounds and construct the SCM image string with explicit capacity. Verify ordinary, spaced, long and unrepresentable paths. [Windows module path contract](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew)
- [x] Verify argument forwarding using a native service fixture. Pass only caller-supplied startup arguments, including on restart, if the guest confirms the extra name.
- [x] Make version, get, status and processes report stdout write or flush errors. Preserve the numeric contract of statuscode. Exercise a failing redirected destination.
- [x] Compare the package's source identity across the top-level manifest, outer build records, inner manifests and source archive. Exercise inconsistent records and verify a real clean-source package.
- [x] Review the final diff, run the x86/x64 Debug and Release suites, production static analysis, x64 AddressSanitizer and live Windows integration including upgrade and rollback. Record the tested revision and limits.

## Verification

Tested implementation `a64a36c1cf4ed0c06e44effb79155851dabdb6e2`. The Windows 11 x64 guest verified the packaged Release executable's SHA-256 as `c4e27b489ad8fd1856e07e7791aa9625ece3406023432f537ecce5c49fe686e3`.

| Check | Result |
| --- | --- |
| Clean-source build and regression matrix | 37 of 37 test groups passed in each x86 and x64 Debug and Release configuration, for 148 passes. |
| MSVC production static analysis | Fresh x86 and x64 Release builds each reported zero warnings. |
| x64 Release with AddressSanitizer | All 37 test groups passed. |
| Package verification | Verified checksums, binary and symbol archives, source identities and test evidence. The preceding clean candidate also passed. A conflicting source commit in an outer build record failed verification. |
| Windows 11 x64 service integration | Export, dependencies, controls, lifecycle and legacy upgrade/rollback all passed. Three additional consecutive lifecycle runs passed. The guest reported zero leftover fixture services. |
| Windows service arguments | The same native fixture received two arguments on a plain start from the previous build. The revised build delivered just the SCM-provided name on plain start and preserved both caller-supplied arguments on start and restart. |
| Windows CLI and configuration | Unwritable stdout produced failure for version, get, status and processes. Statuscode still returned the service state. A malformed replacement environment left the original value readable. |

GUI failure cases used hidden Windows controls. Rotation tests used real files and injected a metadata error, then checked that the active file still contained the exact original bytes. Long wrapper paths were checked at the formatting boundary. Live acceptance used one Windows 11 guest. The local logs and JUnit reports are under `build/post-analyzer-acceptance/`.
