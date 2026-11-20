# Configuration and packaging review

Reviewed baseline `eab2a7a` on `review/beta4-followup`. This pass followed service configuration through the CLI, registry, GUI, runtime and export paths, and examined supervision, logging, build and distribution code.

| Impact | Location at baseline | Finding |
| --- | --- | --- |
| High | `src/gui.cpp:606`, `:777`, `:810` | Saving with a missing selection or incomplete combo box can enable a disabled service or change its process priority or exit policy. |
| High | `src/nssm.cpp:135` | Dump can emit a physical line break inside a command and report success. The resulting script cannot reliably restore the setting. |
| Medium | `src/settings.cpp:674` | A priority DWORD with unsupported bits can export as a valid high priority even though startup uses normal priority. |
| Medium | `src/service.cpp:1247`, `src/settings.cpp:471` | A prefixed environment edit with multiple arguments can merge two assignments or silently fail to remove the requested entry. Adding to an absent block also parses the same input differently. |
| Medium | `scripts/package.ps1:115` | Copy or hash failures leave a partially populated candidate directory at the requested output path and prevent a clean retry. |
| Medium | `src/gui.cpp:127` | The line-based list editor cannot distinguish a line break inside an existing registry entry from the boundary between entries. Saving can split one environment variable into two. |

## Implementation plan

- [x] Check the item count and selected index for startup, priority and exit action before accepting the GUI draft. Exercise missing selections, controls and entries with real Windows combo boxes.
- [x] Reject CR and LF in dump arguments before writing the affected command. Cover both line endings and explain how to handle an incomplete dump.
- [x] Validate the complete AppPriority DWORD using the runtime's conversion rules. Cover valid values, absent settings and unsupported bits through the settings and dump paths.
- [x] Distinguish multiple command-line arguments from one environment edit. Preserve whole-block replacement, single-variable changes, empty values and literal line breaks in a single add. Check stored bytes and failed edits.
- [x] Assemble candidate contents and checksums in a unique sibling directory, then rename the complete directory into place. Exercise successful publication, failed assembly and a destination conflict.
- [x] Check that stored list entries can be represented before opening the line-based editor. Exercise the dialog with an embedded line break and verify that no save occurs.
- [x] Review the implementation diff, build and test x86 and x64 in Release and Debug, run the package verifier and record the results and coverage limits.

## Verification

Tested implementation `0d6eb2f653a021eff5916ceace076877a29ac27b`. The clean-source x64 Release executable used on the guest had SHA-256 `aad0a660c873e5399bf0de5d35aa52145f91b10b4118c412d7e626f679f24859`.

| Check | Result |
| --- | --- |
| Clean-source x86 and x64, Debug and Release | 37 of 37 test groups passed in each configuration, for 148 passes. |
| MSVC production static analysis | Zero warnings in fresh x86 and x64 Release builds. |
| x64 Release with AddressSanitizer and debug symbols | 37 of 37 test groups passed. |
| Local package publication | Injected write failure left the public path absent. Retry succeeded. Publishing again preserved the first candidate and removed the staging directory. The repository verifier checked the real candidate's hashes, archives, test evidence and source identity. |
| Windows 11 x64 service integration | Export, dependencies, controls, lifecycle and legacy upgrade/rollback passed. The packaged CLI rejected the malformed priority, multiline dump and ambiguous environment update. Three additional lifecycle runs passed. The guest reported zero leftover fixture services. |

GUI cases used hidden Windows dialogs and actual controls. The service integration tests ran on one Windows 11 x64 guest. Build logs, JUnit reports and the local candidate are under `build/config-review-acceptance/`.
