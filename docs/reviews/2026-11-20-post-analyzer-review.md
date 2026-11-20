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

- [ ] Stop offline rotation on metadata errors. Retain the append fallback and protect destructive dispositions. Test both archive modes and exact source bytes.
- [ ] Read GUI lists and credential fields using checked lengths and complete copies. Preserve deliberately empty input. Exercise short reads and missing controls through the editor.
- [ ] Validate every configured environment entry directly before saving. Preserve empty blocks, empty values, embedded equals in values and existing runtime handling of older configurations. Replace process-based validation tests with input and persistence checks.
- [ ] Read the wrapper image once, check bounds and construct the SCM image string with explicit capacity. Verify ordinary, spaced, long and unrepresentable paths. [Windows module path contract](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew)
- [ ] Verify argument forwarding using a native service fixture. Pass only caller-supplied startup arguments, including on restart, if the guest confirms the extra name.
- [ ] Make version, get, status and processes report stdout write or flush errors. Preserve the numeric contract of statuscode. Exercise a failing redirected destination.
- [ ] Compare the package's source identity across the top-level manifest, outer build records, inner manifests and source archive. Exercise inconsistent records and verify a real clean-source package.
- [ ] Review the final diff, run the x86/x64 Debug and Release suites, production static analysis, x64 AddressSanitizer and live Windows integration including upgrade and rollback. Record the tested revision and limits.
