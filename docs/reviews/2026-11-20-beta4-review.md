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
- [ ] Abort editing if existing dependency or environment entries cannot be displayed in full. Exercise formatting and control failures and verify that the editor exits before saving.
- [ ] Review the final diff, run the x86 and x64 Debug and Release suites, verify the package, and exercise the Windows service integration suites. Record the results and any limits below.
