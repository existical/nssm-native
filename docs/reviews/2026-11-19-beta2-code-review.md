# NSSM Native beta.2 code review

Reviewed on 19 November 2026 at `e1cc7467a3f6705ecef4d3531f0d6c6eb647871a`. The C++ sources, build scripts and tests match tag `v2.25.0-beta.2` at `2a3168c8260cba5224aac0c7cf40689d3271948e`. Only publication documentation changed between these revisions.

Two ordinary file-handling failures can lose existing log records. A third issue can leave a service stuck during startup. These three deserve attention before the next release. The other findings concern service control, configuration fidelity, package verification and error handling. Finding numbers are local to this review.

## High priority

### 1. A failed offline archive can be followed by truncation of the only log

When offline rotation cannot rename or copy the active log, `rotate_file` logs the failure and returns. Its caller then opens the same path with the configured creation disposition. A reader can allow writing while denying delete sharing, which prevents a rename and still permits `CREATE_ALWAYS` to truncate the file. In a production-code fixture, an eight-byte log became empty, no archive was created and output setup returned success. The existing tests of the rotation helper miss the subsequent open. [Rotation result](../../src/io.cpp#L398) · [Stdout setup](../../src/io.cpp#L460) · [Stderr setup](../../src/io.cpp#L495)

Return distinct outcomes for a skipped, completed or failed archive. On failure, preserve the active file and propagate the error before a destructive open. Test both stdout and stderr through `get_output_handles` with a reader denying delete sharing. Assert the original bytes, the lack of an archive and the setup result.

### 2. Two wrappers appending to the same log overwrite one another

With the default `OPEN_ALWAYS` disposition and read/write sharing, each wrapper opens its own file handle and seeks to the end once. The two handles then maintain independent file offsets. I opened stdout twice through separate service objects before either wrote. Writing `AAAA` and then `BBBB` left exactly four bytes, `BBBB`. The beta.2 fix for aliases within one service cannot coordinate separate service instances. [File open](../../src/io.cpp#L270) · [Default sharing and disposition](../../src/nssm_io.h#L7)

Use append-at-write semantics for direct disk output configured for append. Preserve the behavior of explicit truncation, devices, and the handles that rotation needs for resizing. Test alternating records from independently opened handles and verify every record appears.

### 3. Offline copy-and-truncate can hold service startup indefinitely

After a successful copy, offline rotation calls `Sleep(AppRotateDelay)` while the launcher holds the service lock. The DWORD reader accepts `4294967295`, which Windows interprets as an infinite sleep. An eligible log and copy-and-truncate mode are sufficient to reach it. Large finite values bring the same availability problem for their duration. Even if the stop event is signalled, the service main thread needs the held lock to publish `STOP_PENDING`. With a 1,000 ms setting, I signalled the stop event as soon as the archive appeared. Output setup returned success 1,000 ms after the signal and the original was truncated. The infinite case follows from the same call and the Windows API contract. [Delay read](../../src/registry.cpp#L720) · [Locked output setup](../../src/service.cpp#L1859) · [Sleep before truncation](../../src/io.cpp#L382) · [Stop processing](../../src/service.cpp#L1629) · [Windows Sleep contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)

Make the wait interruptible by shutdown and define how an existing maximum-DWORD setting is handled. On cancellation after the copy, preserve the active log. Verify a STOP during a gated finite wait and exercise the infinite sentinel with a simulated wait.

## Medium priority

### 4. A configuration dump can report success after stdout fails

The install line and setting lines ignore the return from formatted output. The command also returns success without checking the final flush. A backup can therefore be partial when its destination fills or its reader closes. In a fixture using the real setting exporter with an unwritable destination, the exporter returned zero, `fflush(stdout)` returned `EOF`, and the stream error flag was set. This undermines the user guide's advice to rely on the dump exit status. [Setting output](../../src/settings.cpp#L160) · [Install line and result](../../src/service.cpp#L1093) · [Usage guide](../usage.md#L209)

Propagate line-write errors and check the final flush before returning success. Test a destination that fails immediately and one that fails only when the last buffered output is flushed.

### 5. Distinct NTFS data streams are merged as stdout and stderr

File identity compares the volume serial number and file index. Two named data streams on the same NTFS file share those values. When both streams already existed, configuring stdout as `file:out` and stderr as `file:err` made `same_file` return true. Writing `OUT` and `ERR` produced `OUTERR` in the first stream and zero bytes in the second. [Identity test](../../src/io.cpp#L161) · [Stderr handle duplication](../../src/io.cpp#L485)

Include the data stream in the logging destination comparison. Retain alias detection for alternate spellings and hard links to the same stream. Cover distinct named streams, the default stream and a genuine alias.

### 6. A complete short line can remain invisible while the application is running

The pipe logger waits for four initial bytes to guess character width, even when timestamping and online rotation are disabled. With the production logger active, a single write of `OK\n` left the file empty after 750 ms. A fourth byte made all four visible. A service that writes a short startup acknowledgement and waits can keep that acknowledgement out of its log indefinitely. Sharing output with hooks is sufficient to select the pipe path. [Logger selection](../../src/registry.cpp#L907) · [Initial byte handling](../../src/io.cpp#L849)

Forward bytes immediately on the plain pipe path. For modes that interpret line boundaries, retain only bytes genuinely needed to resolve the encoding. Test the first line while the writer is still open and retain split UTF-16 coverage.

### 7. Export and replay change the registry type of literal launch and I/O strings

The runtime distinguishes `REG_SZ` from `REG_EXPAND_SZ`. The generic getter drops the stored type, the dump uses the setting table's declared type, and the setter always writes an expandable string. Reading and setting a literal `AppStdout` through the production setting functions changed the type from 1 to 2. A literal percent-delimited directory or argument can consequently resolve to a different value on the restored service. This happens at the registry API boundary, independently of shell quoting. [Generic setter and getter](../../src/settings.cpp#L117) · [Dump dispatch and setting types](../../src/settings.cpp#L1296) · [Runtime expansion decision](../../src/registry.cpp#L408)

Carry the original type through export and replay for the launch and I/O strings, including the install line for `Application`. Preserve existing command semantics for ordinary setters. Test with both registry types and the same percent-delimited text, and verify raw type, raw data and effective launch value.

### 8. Startup publishes RUNNING for a child already known to have exited

When the application process signals during the startup wait, `await_startup` returns zero. The caller uses the wait result only to reset throttling, then registers its callback and publishes `SERVICE_RUNNING` while holding the lock that the exit callback needs. An immediate exit can therefore produce a successful start event and a RUNNING notification before the configured exit action handles it. A failed startup wait takes the same path. [Wait result](../../src/service.cpp#L51) · [Status publication](../../src/service.cpp#L1976) · [Exit callback lock](../../src/service.cpp#L2077)

Distinguish a live child, an early exit, cancellation and a failed wait. Route early exits through the configured exit policy with one cleanup owner. Record all status transitions for a child that exits during the startup wait with `AppExit=Exit`, and cover the rapid-restart policy separately.

### 9. Power notifications can block the service-control dispatcher

The power-change and resume handlers invoke `nssm_hook` directly. Its default asynchronous mode still takes the service lock and performs environment setup and process creation before returning. A power event can wait behind a long Start/Pre or offline rotation even when no power hook is configured. On worker allocation or thread creation failure, the hook can also be awaited on the dispatching thread. Subsequent controls can time out. [Power handlers](../../src/service.cpp#L1785) · [Lock acquisition](../../src/hook.cpp#L201) · [Synchronous fallback](../../src/hook.cpp#L347) · [Windows service handler guidance](https://learn.microsoft.com/en-us/windows/win32/services/service-control-handler-function)

Queue the work with the event and service generation captured, and acknowledge the control promptly. Exercise a power event behind a gated startup hook, followed by STOP, and verify delivery order and handler return times.

### 10. Saving an unrelated GUI edit can silently drop configured CPU IDs

The affinity list contains only CPUs visible to the editor. Saving rebuilds the stored mask from those rows on every edit. With CPUs 0–3 visible and an original mask for CPUs 1 and 40, the real list-box helpers returned only CPU 1. The editor shows an availability warning, but changing just the description can still remove CPU 40 from the saved configuration. This affects configurations moved between machines and the x86 editor's smaller mask. [List population and readback](../../src/gui.cpp#L11) · [Unavailable-CPU warning](../../src/gui.cpp#L187) · [Save path](../../src/gui.cpp#L745) · [Registry write](../../src/registry.cpp#L152)

Preserve the original full-width mask until the operator edits affinity. Test a description-only save with a partly visible mask, followed by an explicit affinity change and an explicit selection of All.

### 11. A native service with its own Parameters/Application can be exported as an NSSM wrapper

The editor checks the SCM image only when loading wrapper parameters fails. A native service with a readable, nonempty string at `Parameters\Application` and otherwise compatible or absent optional settings passes that test. Dump then emits an NSSM install line for the other application's setting. A restored service can therefore run a different executable and operate under a different service model. The default list command uses the same single-value assumption. [Parameter read](../../src/registry.cpp#L816) · [Service classification](../../src/service.cpp#L1052) · [Install line](../../src/service.cpp#L1093) · [List filter](../../src/service.cpp#L2353)

Use SCM image evidence when classification succeeds as well as when it fails. Account for supported renamed or separately installed NSSM binaries and explicitly handle ambiguous cases. Test a disabled native fixture with an unrelated `Parameters\Application` alongside a real wrapper installed from another path.

### 12. Named service controls request permission to enumerate every service

The control path requests `SC_MANAGER_ENUMERATE_SERVICE` before opening the named service. This can deny an operator who has manager-connect permission and the required rights on that service. The path uses name mapping and opens one service. Using the production resolver with a connect-only manager handle, I resolved `EventLog` and queried its status successfully. The fixture restricted the handle's rights. It did not alter an SCM access-control list. [Manager access requested](../../src/service.cpp#L1364) · [Name resolution](../../src/service.cpp#L442) · [Per-service access](../../src/service.cpp#L1371)

Request connect-only access for named operations and retain enumeration rights where enumeration is actually performed. Exercise service key and display names under delegated permissions, including a denied operation on the service itself.

### 13. Package verification succeeds when all test evidence is missing

I copied the four genuine beta.2 binary ZIPs, source ZIP and manifest, omitted the evidence directory, and retained only the remaining entries in the checksum list. The repository's verifier exited zero and printed that it had verified the test evidence. It checks the supplied checksum entries and the test count embedded in each manifest, but never requires or parses the named JUnit reports. The same verifier runs in candidate CI. [Checksum loop](../../tests/package.ps1#L4) · [Manifest validation](../../tests/package.ps1#L26) · [Generated evidence names](../../scripts/package.ps1#L73) · [CI invocation](../../.github/workflows/ci.yml#L105)

Require the expected reports for each configuration, require checksum coverage for them, and compare the JUnit results with the manifest. Add negative fixtures for an omitted report and for a report that disagrees with the recorded result.

## Lower priority

### 14. An allocation failure can produce a successful partial environment dump

When per-entry allocation fails while exporting `AppEnvironment` or `AppEnvironmentExtra`, the loop prints an error and breaks. Its error counter remains zero if earlier entries succeeded, so callers accept the incomplete output. [Failure branch and return](../../src/settings.cpp#L563)

Return failure after freeing the environment block. Inject an allocation failure on the second entry and require a failing dump exit code.

### 15. Virtual-account detection dereferences a failed allocation

`virtual_account` returns null after an allocation failure. `is_virtual_account` immediately passes the result to `str_equiv`, which calls the string-length function on it. Installation and account editing both reach this path. [Allocation and caller](../../src/account.cpp#L198) · [String comparison](../../src/nssm.cpp#L18) · [Installation path](../../src/service.cpp#L1290)

Compare the account prefix, separator and service name without allocating, or propagate allocation failure before comparing. Exercise the account edit path under allocation failure and verify it makes no account changes.

### 16. The usage guide gives an incorrect result for resetting Application

The current guide says that resetting `Application` leaves an empty executable path. The setter rejects that command before touching the registry, and the live export suite explicitly checks the rejection. Someone following the guide to clear or diagnose a service would infer the wrong on-disk state. [Usage guide](../usage.md#L172) · [Setter](../../src/settings.cpp#L89) · [Live regression](../../tests/export_live.cpp#L132)

Update the guide to distinguish the required application path from optional strings and describe the error and retained value. Keep the historical fix note tied to its original revision.

## Validation and limits

| Fresh validation | Result |
| --- | --- |
| x86 Debug and Release, x64 Debug and Release | All 37 standard tests passed in each configuration, 148 executions. |
| Release-matched Windows VM fixtures | Export/import, dependencies, controls, lifecycle, and legacy → beta.2 → legacy upgrade/rollback all passed. The before/after check found zero new fixture services. |
| Additional Windows checks | Reproduced findings 1, 2, 3 (finite delay), 4, 5, 6, 7, 10 and 13 using production objects or the repository's package verifier. The GUI check exercised the real list-box helpers. The registry check exercised the getter/setter boundary. |
| Restricted SCM handle | Verified the named lookup and query in finding 12. The delegated-operator failure follows from the excessive manager access requested by the CLI. |

The review covered the service and child-process lifecycle, hooks, logging and rotation, registry and environment handling, account management, CLI and GUI configuration, build and release verification, and their tests. Remaining findings are based on tracing return values, lock ownership and callers. The review did not inject workstation-wide memory pressure, wait indefinitely on a service, or exercise a physical power transition.

The previously documented limits of batch-file percent escaping and snapshot-based descendant discovery are outside the numbered findings. The percent-sign replay check also reproduced the documented shell behavior. [Dump limitations](../usage.md#L216) · [Process-tree limitations](../usage.md#L153)
