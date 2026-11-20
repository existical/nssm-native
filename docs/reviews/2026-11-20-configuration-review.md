# Configuration and packaging review

Reviewed baseline `eab2a7a` on `review/beta4-followup`. This pass followed service configuration through the CLI, registry, GUI, runtime and export paths, and examined supervision, logging, build and distribution code.

| Impact | Location at baseline | Finding |
| --- | --- | --- |
| High | `src/gui.cpp:606`, `:777`, `:810` | Saving with a missing selection or incomplete combo box can enable a disabled service or change its process priority or exit policy. |
| High | `src/nssm.cpp:135` | Dump can emit a physical line break inside a command and report success. The resulting script cannot reliably restore the setting. |
| Medium | `src/settings.cpp:674` | A priority DWORD with unsupported bits can export as a valid high priority even though startup uses normal priority. |
| Medium | `src/service.cpp:1247`, `src/settings.cpp:471` | A prefixed environment edit with multiple arguments can merge two assignments or silently fail to remove the requested entry. Adding to an absent block also parses the same input differently. |
| Medium | `scripts/package.ps1:115` | Copy or hash failures leave a partially populated candidate directory at the requested output path and prevent a clean retry. |

## Implementation plan

- [ ] Check the item count and selected index for startup, priority and exit action before accepting the GUI draft. Exercise missing selections, controls and entries with real Windows combo boxes.
- [ ] Reject CR and LF in dump arguments before writing the affected command. Cover both line endings and explain how to handle an incomplete dump.
- [ ] Validate the complete AppPriority DWORD using the runtime's conversion rules. Cover valid values, absent settings and unsupported bits through the settings and dump paths.
- [ ] Distinguish multiple command-line arguments from one environment edit. Preserve whole-block replacement, single-variable changes, empty values and literal line breaks in a single add. Check stored bytes and failed edits.
- [ ] Assemble candidate contents and checksums in a unique sibling directory, then rename the complete directory into place. Exercise successful publication, failed assembly and a destination conflict.
- [ ] Review the implementation diff, build and test x86 and x64 in Release and Debug, run the package verifier and record the results and coverage limits.
