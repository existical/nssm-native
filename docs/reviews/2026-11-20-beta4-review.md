# Beta.4 code review and repair plan

Reviewed commit `1ea9c65217a07617878e7dd1fa7c95a335af65c7`. The review covered the native service lifecycle, process and hook execution, CLI, GUI, registry settings, environment handling, logging and rotation, and the associated test and release paths.

| Priority | Finding | Observable consequence |
| --- | --- | --- |
| High | The hook editor stores its draft in process environment variables and saves every successfully loaded hook. | Saving another field can revert a concurrent hook edit or delete a newly added hook. Opening the dialog also changes inherited environment values. |
| High | The launch path treats a read error for AppDirectory as a request for a default. Several callers truncate the executable path before extracting its directory. | A service can start with a different working directory and resolve relative files against it. |
| Medium | Rotation discards formatting errors when inserting a timestamp into a long archive path. | A rename or copy can target a truncated filename and report a successful rotation. |
| Medium | Service enumeration sizes one allocation from the total outstanding entries, retries without checking the resume handle, and ignores stdout failures. | A large inventory can exceed the API buffer limit, a stalled page can loop, and an incomplete listing can return success. |

## Implementation checklist

- [ ] Store commands and registry types in the dialog, track which hooks the operator edits, and write only those hooks. Test concurrent edits and additions, inherited environment values, intentional deletion and failed-save retry.
- [ ] Reject unreadable AppDirectory values during startup. Extract a default directory from the full executable path before checking its capacity, and apply the same rule to installation. Test absent, malformed and overlong settings and exact-fit directory boundaries.
- [ ] Build the full timestamped archive name with a checked capacity. Propagate failure through offline and online rotation while preserving the active writer. Test filename placement, exact fit, insufficient space and failure handling.
- [ ] Enumerate SCM records in bounded pages, detect a stalled cursor and propagate output errors. Exercise multiple pages, an empty database, failure responses and an unwritable output handle.
- [ ] Review the final diff, run the x86 and x64 Debug and Release suites, verify the package, and exercise the Windows service integration suites. Record the results and any limits below.
