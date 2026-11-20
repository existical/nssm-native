# Beta.4 code review and repair plan

Reviewed commit `1ea9c65217a07617878e7dd1fa7c95a335af65c7`. The review covered the native service lifecycle, process and hook execution, CLI, GUI, registry settings, environment handling, logging and rotation, and the associated test and release paths.

| Priority | Finding | Observable consequence |
| --- | --- | --- |
| High | The hook editor stores its draft in process environment variables and saves every successfully loaded hook. | Saving another field can revert a concurrent hook edit or delete a newly added hook. Opening the dialog also changes inherited environment values. |
| High | The launch path treats a read error for AppDirectory as a request for a default. Several callers truncate the executable path before extracting its directory. | A service can start with a different working directory and resolve relative files against it. |
| Medium | Rotation discards formatting errors when inserting a timestamp into a long archive path. | A rename or copy can target a truncated filename and report a successful rotation. |
| Medium | Service enumeration sizes one allocation from the total outstanding entries, retries without checking the resume handle, and ignores stdout failures. | A large inventory can exceed the API buffer limit, a stalled page can loop, and an incomplete listing can return success. |
| Medium | Numeric GUI fields accept a prefix or silently ignore conversion failures. | The dialog can report success after saving a different timeout or rotation threshold from the one the operator entered. |
| Low | The main dialog loads two private icon handles without releasing them. | Opening the dialog consumes USER resources for the rest of the process lifetime. |
| High | A failure to format or populate existing dependency or environment entries leaves the editor open with an empty field. | Saving an unrelated edit can clear the stored entries. |

## Implementation checklist

- [x] Store commands and registry types in the dialog, track which hooks the operator edits, and write only those hooks. Test concurrent edits and additions, inherited environment values, intentional deletion and failed-save retry.
- [x] Reject unreadable AppDirectory values during startup. Extract a default directory from the full executable path before checking its capacity, and apply the same rule to installation. Test absent, malformed and overlong settings and exact-fit directory boundaries.
- [x] Build the full timestamped archive name with a checked capacity. Propagate failure through offline and online rotation while preserving the active writer. Test filename placement, exact fit, insufficient space and failure handling.
- [x] Enumerate SCM records in bounded pages, detect a stalled cursor and propagate output errors. Exercise multiple pages, an empty database, failure responses and an unwritable output handle.
- [x] Validate complete unsigned decimal values in GUI numeric fields before saving. Identify the invalid setting and keep the existing advanced rotation threshold when its field is untouched. Exercise invalid input, zero and the maximum DWORD.
- [x] Give the dialog icons explicit ownership and release both sizes after the window closes.
- [x] Abort editing if existing dependency or environment entries cannot be displayed in full. Exercise formatting and control failures and verify that the editor exits before saving.
- [x] Review the final diff, run the x86 and x64 Debug and Release suites, verify the package, and exercise the Windows service integration suites. Record the results and any limits below.

## Verification

Tested implementation commit `8fa08eff110d731d1c3a08405559b29d0016d711`. The Windows 11 guest used the packaged x64 Release executable with SHA-256 `5ff55a7d2c5c17ffd62b3c03bf42b8a32c59e8416d8d97d1dc96ada88f14c17f`.

| Check | Result |
| --- | --- |
| Clean-source x86 and x64, Debug and Release | 37 of 37 test groups passed in each configuration, 148 passes total. |
| x64 Release with MSVC AddressSanitizer | All 37 test groups passed. |
| Package verification | Verified the source archive, four binary and symbol archives, file inventories, checksums and test evidence. |
| Elevated Windows 11 service tests | Export, dependencies, controls, wrapped lifecycle and upgrade/rollback passed. |
| Repeated lifecycle and cleanup | Three further consecutive lifecycle runs passed. The guest reported zero leftover fixture services. |

On the guest, the SCM rejected startup with a DWORD AppDirectory and the service reached Running after its configuration was restored. The package and sanitizer JUnit reports and the guest transcript are in the local `build/beta4-review-acceptance` directory.

The GUI regressions use real hidden Windows controls. They check concurrent hook edits, deliberate clearing, save retries, full-range numeric input and destruction of both icons. For both dependencies and environment entries, malformed data, a control write failure and a short control write all closed the editor before a save. [GUI implementation](../../src/gui.cpp) · [GUI regressions](../../tests/install_gui.cpp)

The parameter reader rejected an explicitly malformed or oversized working directory. Missing and empty values retained their fallback behavior. The rotation tests checked an exact-fit buffer and an archive name one character too long. An online rotation failure signaled the caller and the original writer still accepted data. [Startup directory](../../src/registry.cpp) · [Rotation](../../src/io.cpp) · [Registry tests](../../tests/registry.cpp) · [Logger tests](../../tests/logger.cpp)

Both beta.4 and the reviewed program listed 318 services on the test workstation with writable stdout. With stdout opened read-only, beta.4 returned 0 and the reviewed binary returned 5. A simulated SCM exercised empty, multipage, stalled and access-denied results. The implementation uses a 64 KiB page within the documented buffer limits. [Service listing](../../src/service.cpp) · [SCM tests](../../tests/startup.cpp) · [Microsoft API reference](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-enumservicesstatusexw)

MSVC static analysis completed with 36 warnings, including 17 large-stack-frame warnings, 11 ignored-return warnings and two possible uninitialized-handle warnings in hook cleanup. These diagnostics remain open. Live acceptance covered one Windows 11 guest. The near-limit rotation paths used synthetic names and a real open log writer.
