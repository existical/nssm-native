# NSSM 014 Logger byte accounting and write failures

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 14, B09 |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| Fix PR | [PR 14](https://github.com/existical/nssm-native/pull/14) |
| Implementation commit | `d2c5ad91605af6ee98439cb8071cd11f9f429fea` |
| Merge commit | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

Legacy timestamping used uninitialized result/complaint values and accumulated
into an output counter that was not reset for each write. BOM/timestamp/data
bytes could be counted twice or omitted. A metadata-query failure assigned
zero to the configured rotation threshold instead of the observed file size.
These code paths also exist in the recovered legacy source. B05 had already
initialized some worker state; it did not complete this repair.

Each write now reports its own byte count. Short successful writes continue
from the remaining suffix. BOMs, timestamps and payloads are counted exactly
once. Zero-progress writes and exhausted/irrecoverable failures terminate the
logger with a write-error event, preserving the written prefix; they do not
silently continue after a broken timestamp or code unit. Quota/disk-full retries
remain bounded and cancellable. This follows the synchronous
[WriteFile contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile).
Once a logger exits, child writes to its closed pipe may fail; partial disk-full
output is not repaired or replayed. No new service exit status is introduced.

Observed size and unsigned 64-bit policy are separate. GetFileSizeEx is a
fallback if file information fails. If both fail, accounting starts at zero
for newly observed bytes and the threshold remains enabled; rotation may be
late by the unknown preexisting size. A BOM is not inserted into an unknown
existing file. Online rotation happens after a complete LF when actual bytes,
including BOM and timestamps, reach the threshold. Zero disables size rotation;
manual requests still rotate at a complete newline. A long line may exceed the
threshold. Draining/offline loggers do not perform online rotation.

Online copy/truncate now checks flush, copy, seek and truncate results while
retaining the original handle; a failed truncate restores the append position.
Failed rename reopens with OPEN_ALWAYS to preserve existing content. Successful
rotation refreshes observed size. Filename formatting now retains its local
timestamp for the entire function and bounds its path copy. Archive collisions
remain failures, not overwrites; external concurrent writers are not coordinated.
Offline rotation's metadata and post-copy failure caveats in
[NSSM-008](NSSM-008.md) still apply.

Tests cover empty writes, short/zero writes, a failure at every byte offset
0–64 (including BOM and timestamp), exact byte totals, metadata failures,
existing logs, size thresholds just below/at/above a line, a high unsigned size
word, manual/offline modes, rename and truncate failures, and copy/truncate
success. See [tests](../../tests/log_bytes_cases.h) and
[B09 validation](../reviews/b09-validation.md).
