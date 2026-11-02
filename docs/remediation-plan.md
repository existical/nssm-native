# NSSM fix implementation plan

This plan covers the review of commit
`5d7454b2cd878b66bcd4b036c33a5b53b184cf3a`. Fix the nine P1 findings first,
starting with memory bounds and ownership, then complete logging,
configuration, and service-control work. Each fix must include a regression
test and a public explanation of how behavior differs from the legacy source.

The [review](reviews/2026-11-02-code-review.txt) contains the evidence.
NSSM-001 through NSSM-024 match its numbered findings. NSSM-025 through NSSM-034
split its additional follow-ups into independent tasks. The latter ten are
follow-ups, not ten additional reproduced high-priority defects.

**Status:** B00 through B11 are merged. NSSM-001, NSSM-003, NSSM-010 and NSSM-011
are fixed but unreleased through
[PR 5](https://github.com/existical/nssm-native/pull/5); see the
[fix records](fixes/README.md) and [validation](reviews/b01-validation.md).
[NSSM-002](fixes/NSSM-002.md) is fixed but unreleased through
[PR 7](https://github.com/existical/nssm-native/pull/7).
B03 (NSSM-004, NSSM-029 and NSSM-031) is fixed but unreleased through
[PR 8](https://github.com/existical/nssm-native/pull/8).
B04 (NSSM-005, NSSM-006 and NSSM-017) is fixed but unreleased through
[PR 9](https://github.com/existical/nssm-native/pull/9).
B05 (NSSM-007) is fixed but unreleased through
[PR 10](https://github.com/existical/nssm-native/pull/10).
B06 (NSSM-008) is fixed but unreleased through
[PR 11](https://github.com/existical/nssm-native/pull/11).
B07 (NSSM-009) is fixed but unreleased through
[PR 12](https://github.com/existical/nssm-native/pull/12). All nine P1 findings
are now merged and unreleased. B08 merged in
[PR 13](https://github.com/existical/nssm-native/pull/13); see its
[validation](reviews/b08-validation.md). B09 merged in
[PR 14](https://github.com/existical/nssm-native/pull/14); see its
[validation](reviews/b09-validation.md). B10 merged in
[PR 15](https://github.com/existical/nssm-native/pull/15); see its
[validation](reviews/b10-validation.md). B11 merged in
[PR 16](https://github.com/existical/nssm-native/pull/16); see its
[validation](reviews/b11-validation.md). B12 has implementation and
[validation](reviews/b12-validation.md) in [PR 17](https://github.com/existical/nssm-native/pull/17),
pending review and merge. NSSM-034
was confirmed by its GUI round-trip test. B13 remains planned.
[Previously merged changes](fixes/implemented.md) retain separate CONT IDs.

The recovered legacy development baseline is
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, described by Git as
`v2.24-103-gdee49fc`. It is distinct from the `v2.24` release.
Before calling a finding a legacy bug, inspect the relevant function at that
baseline and record the result. A reproduction against the reviewed
continuation does not establish that every historic binary is affected.
Use the [build comparison guide](comparing-builds.md) for those distinctions.

## Delivery order

Each row is a focused change group; split it into smaller PRs if the tests or
ownership changes become difficult to review. Include the tests in the first
fix that needs them, so a separate test-framework rewrite does not delay P1
work. P2 tasks share an early group only when they support the same repair.

| Order | Scope | Issue IDs | Prerequisites and completion |
| --- | --- | --- | --- |
| B00 | Publish review, plan, fix records and comparison rules | Documentation only | Merged in PR 4; CONT-001 through CONT-005 backfilled |
| B01 | Registry and environment bounds | 001, 003, 010, 011 | Merged in PR 5; regression, sanitizer and four-configuration checks passed |
| B02 | Safe command quoting | 002 | Merged in PR 7; bounds, dump failures and shell round trips validated |
| B03 | Affinity and numeric parsing | 004, 029, 031 | Merged in PR 8; boundary, registry and sparse GUI-list tests pass |
| B04 | Hook ownership and launch handles | 005, 006, 017 | Merged in PR 9; forced schedules/failures and 200-launch handle tests pass |
| B05 | Logger lifetime | 007 | Merged in PR 10; EOF, cancellation, failure and restart ownership tests pass |
| B06 | Offline rotation | 008 | Merged in PR 11; size/age/delay, mode/stream and failure tests pass |
| B07 | Process identity during cleanup | 009 | Merged in PR 12; PID-reuse, signalling, failure and owned child-tree tests pass |
| B08 | Primitive ownership and error paths | 015, 018, 019, 021, 028, 032 | Merged in PR 13; six focused implementation commits, failure tests and full matrix pass |
| B09 | Redirection and logging correctness | 012, 013, 014, 022 | Merged in PR 14; stdin, encoding, byte counts and typed event insertions validated |
| B10 | Configuration export and exit actions | 016, 030, 033 | Merged in PR 15; AppExit round trips, strict fallback values and real query-only export validated |
| B11 | GUI and accounts | 020, 025, 026, 027, 034 | Merged in PR 16; five confirmed fixes, GUI retries, account failures and hook round trips validated |
| B12 | Dependencies | 023 | In progress; typed resets/merges, bounded queries, validation cleanup and SCM round-trip tests implemented |
| B13 | Remaining service control waits | 024 | B08; zero-time and bounded-polling semantics established |
| B14 | Candidate release and comparison records | All completed IDs | All P1 tasks closed, release criteria below satisfied |

## Shared implementation and test rules

- Extend the existing C++11/MSVC setup and preserve source encodings. Put
  refactoring needed by a fix in its own small commit; avoid unrelated moves.
- Formalize the relevant review probes in `tests/`, using production functions.
  Keep injected clocks, allocators, SCM/process APIs, and failure switches in
  test translation units. They must not enter the shipped executable.
- For each defect, first demonstrate a failing assertion against the reviewed
  source, or a deterministic failing API-contract assertion when executing
  the unsafe behavior would crash. Preserve the test after the fix.
- Use guarded allocations or isolated subprocesses for corruption tests.
  Use fake clocks and forced schedules for timeout/race cases. A flaky timing
  test does not establish that the race was repaired.
- Run targeted tests during development, then all four existing
  x86/x64 Debug/Release build and CTest configurations for each behavior PR.
  Give every new CTest group a timeout and name it in the fix record.
- Run focused `/analyze` checks on changed translation units. Use available
  heap/handle diagnostics or AddressSanitizer in a separate supported build
  configuration; record tool versions and unsupported configurations.
  Do not equate the analyzer's warning count with a defect count.
- Add actual service tests as an opt-in suite on a disposable Windows runner,
  using unique service names and temporary directories, with cleanup in
  failure paths. Ordinary tests must remain usable without elevation.
  Never target existing user services.
- Document compatibility changes beside each fix: accepted inputs, exit
  status, waiting behavior, log bytes, or registry representation. Preserve
  valid legacy settings wherever possible; reject malformed input explicitly.

## High priority implementation tasks

### NSSM 001

**Registry expansion bounds — P1, B01.** Review finding 1; reproduced.
Touch `src/registry.cpp:get_string`, its declarations and callers.

1. Keep the public capacity convention explicitly in bytes, audit every
   caller, and derive a checked character capacity for Windows string APIs.
2. Validate the stored string using NSSM-010, preflight expansion length,
   reject overflow, and terminate successful output. Handle expansion failure
   without exposing a partial result.
3. Retain current expand/sanitise choices, especially the hook-quoting fix.

**Tests:** empty, exact fit, one character too large, greatly expanded
variables, missing variables, REG_SZ versus REG_EXPAND_SZ, and canaries around
destinations of multiple sizes. Retain the 64-character/80-character probe.
**Done:** no writes outside capacity; oversized values return a documented
failure. Record that oversized expansions previously could corrupt memory.

### NSSM 002

**Command quoting bounds — P1, B02.** Review finding 2; reproduced.
Touch `src/nssm.cpp:quote` and dump tests.

**Implementation status:** merged in PR 7, unreleased. See the
[fix record](fixes/NSSM-002.md) for results and shell-mode limitations.

1. Use a checked output builder, or one escape traversal shared by counting
   and writing, with overflow-safe length arithmetic.
2. Define zero-capacity and insufficient-capacity results and guarantee NUL
   termination on success. Keep the output's intended cmd.exe and argument
   parsing semantics.
3. Audit callers for handling a quoting failure instead of emitting a
   partially valid script.

**Tests:** the four-ampersand reproduction, zero/one/exact capacities, spaces,
quotes, percent signs, carets, ampersands, backslash runs and trailing
backslashes; boundary combinations and bounded generated input. Execute
harmless dumped commands through cmd.exe with the delayed-expansion mode
documented and compare the child's actual arguments.
**Done:** length accounting and parsing agree; no overwrite or silent
truncation. Document any shell-mode limitation revealed by round trips.

### NSSM 003

**Duplicate environment entries — P1, B01.** Review finding 3; reproduced.
Touch `src/registry.cpp:append_to_double_null` and environment consumers.

1. Match complete keys using the caller's case-sensitivity convention.
2. Write a replacement once, at the first matching position, skip subsequent
   matches, and preserve unrelated entry order. Calculate the allocation
   from exactly that output policy.
3. Validate input length/termination before walking it and handle length
   arithmetic overflow.

**Tests:** exact and case-variant duplicates, longer/shorter replacements,
first/middle/last/consecutive matches, empty values, nonmatching prefixes,
and append/remove combinations with guards.
**Done:** one replacement entry, valid double NUL, no overwrite. Document
duplicate normalization and verify the empty-value behavior from CONT-003.

### NSSM 004

**Affinity array and range bounds — P1, B03.** Review finding 4; endpoint
acceptance reproduced, array overflow source-traced.
Touch `src/service.cpp:affinity_string_to_mask`.

**Implementation status:** merged in PR 8, unreleased; see [NSSM-004](fixes/NSSM-004.md).

1. Parse validated tokens directly into an unsigned 64-bit mask, or bound
   every temporary-array access before writing. Prefer eliminating the array.
2. Check both endpoints, ordering, conversion overflow, and delimiters before
   shifting; shift only by 0 through 63.
3. Preserve valid duplicate entries as idempotent input if parsing directly
   into a mask; reject malformed ranges with the existing error mechanism.
   Use NSSM-031's checked numeric conversion.

**Tests:** 64/65/repeated entries, trailing commas, empty tokens, 0 and 63,
0-64, reversed ranges, negative/huge numbers, and x86 behavior.
**Done:** bounded execution with no array overflow or undefined shifts.
Document accepted syntax and treatment of CPUs outside the active group.

### NSSM 005

**Hook completion ownership — P1, B04.** Review finding 5; reproduced by
forcing completion before CreateThread returns.
Touch `src/hook.cpp:nssm_hook`, `await_hook`, and thread-list ownership.

**Implementation status:** merged in PR 9, unreleased; see [NSSM-005](fixes/NSSM-005.md).

1. Define which object owns the hook name, process handle, worker handle and
   completion result. Keep caller metadata alive independently of worker data.
2. Copy or transfer the caller's required name before publishing the worker;
   remove all access to worker-owned storage after it may finish.
3. Make thread-list insertion/allocation failure clean up or join the worker
   safely. Apply the same ownership model to synchronous waiting.

**Tests:** immediate completion, delayed completion, async/sync paths,
nonzero exit, timeout, thread-list insertion failure and concurrent shutdown.
**Done:** exactly one free per allocation and no access after release under
all forced schedules. Preserve existing hook result codes.

### NSSM 006

**Hook thread-creation fallback — P1, B04.** Review finding 6; two duplicate
frees reproduced. Touch `src/hook.cpp:nssm_hook`.

**Implementation status:** merged in PR 9, unreleased; see [NSSM-006](fixes/NSSM-006.md).

1. Let synchronous `await_hook` retain sole ownership of its cleanup when
   CreateThread fails, removing the caller's duplicate frees.
2. Return the fallback's actual completion result and preserve the original
   thread-creation diagnostic.
3. Check every resource acquired before thread creation against NSSM-005's
   ownership model.

**Tests:** forced creation failure followed by success, failed hook, abort,
and timeout; allocation/free and handle counts.
**Done:** cleanup occurs once and the returned status describes the executed
hook. Document the corrected fallback result.

### NSSM 007

**Logger shutdown ownership — P1, B05.** Review finding 7; invalid wait
reproduced. Touch `src/io.cpp:cleanup_loggers`, `close_handle`,
`create_logging_thread`, `log_and_rotate`, and shutdown/restart calls.

**Implementation status:** merged in PR 10, unreleased; see [NSSM-007](fixes/NSSM-007.md).

1. Write down ownership of each file, pipe end and thread handle. Close all
   parent-held writer ends, retain a live thread handle while joining, and
   release it only after the wait result is handled.
2. Give the read pipe and output file a single closer. Remove duplicate-close
   behavior and raw references from a surviving logger into freed service
   state.
3. Define the deadline path: request supported I/O cancellation or transfer
   cleanup to worker-owned state. Never free live worker state merely because
   a wait expired. Prevent an old logger from touching a new restart's state.

**Tests:** ordinary EOF, shared stdout/stderr, blocked read/write, child-held
writer, timeout, WAIT_FAILED, rapid restart and output while stopping.
Assert close/wait ordering and detect reused-handle operations.
**Done:** no waits on closed handles, duplicate closes or surviving dangling
pointers; expected trailing output is drained. Document deadline behavior and
any output loss remaining under a forced timeout.

### NSSM 008

**Offline rotation parameter order — P1, B06.** Review finding 8; reproduced
with the long sleep intercepted. Touch `src/io.cpp:rotate_file` and both callers.

**Implementation status:** merged in PR 11, unreleased; see [NSSM-008](fixes/NSSM-008.md).

The B06 audit leaves two filesystem-error follow-ups for B09's logging
correctness work: decide whether a metadata-query failure should bypass
configured limits, and propagate reopen/seek/truncate failures after a copy.
Add injected failures before changing either policy; these are outside the
parameter-order repair and are recorded in its fix record.

1. Correct delay versus low/high size argument order.
2. Replace adjacent indistinguishable arguments with named rotation options
   if that can remain a small local change; assemble 64-bit sizes explicitly.
3. Verify the same policy for stdout/stderr and copy/truncate/rename modes.

**Tests:** the 4-byte/1-MiB/zero-delay reproduction, exact threshold, below
threshold, nonzero high size word, age checks, zero/nonzero delay and copy
failure. Record sleeps with an injected clock.
**Done:** the configured threshold controls rotation and only the configured
delay controls waiting. Explain possible changes for configurations that
previously appeared to work because of the swapped arguments.

### NSSM 009

**Stable process identity during tree cleanup — P1, B07.** Review finding 9;
source-traced, not a demonstrated unrelated-process kill.
Touch `src/process.cpp:check_parent`, `walk_process_tree`, termination
callbacks, and `src/service.cpp:end_service`.

**Implementation status:** merged in PR 12, unreleased;
see the [fix record](fixes/NSSM-009.md) for legacy comparison, identity checks,
failure policy and Windows signalling limitations. PID reuse is now reproduced
with simulated API schedules; live tests target only fixture-owned descendants.

1. Preserve the root's process identity/handle through cleanup.
2. Acquire each child handle once with required rights, validate its
   creation time and parent relationship, then act through that same handle.
   Validate child times against the correct parent at each depth.
3. Treat disappeared, reused or unverifiable processes as unsafe to target;
   skip them with an appropriate result/diagnostic. Release retained handles
   on every branch, including access denial.
4. Audit PID-based console/window/thread operations for the same lifetime
   gap. Keep any Job Object migration as a separately reviewed design because
   it changes breakaway and compatibility behavior.

**Tests:** simulated root/child PID reuse between enumeration and open,
same-PID/different-creation-time results, vanished/access-denied processes,
deep trees, sibling exit, and benign child-tree integration tests.
**Done:** a substitute process is never signalled or terminated in the
deterministic tests; intended descendants are handled and all handles close.

## Remaining numbered findings

### NSSM 010

**Bounded registry strings — P2, B01.** Review finding 10; MULTI_SZ overread
reproduced. Touch registry string/environment readers.

1. Validate type, byte alignment, size and terminators after the actual query;
   account for size/type changes between queries with bounded retries.
2. Reject malformed MULTI_SZ and odd-byte Unicode values. For simple strings,
   choose and document a bounded missing-terminator policy; do not pass
   unvalidated buffers to path or expansion APIs.
3. Return initialized empty outputs on failure and use checked extra capacity
   when adding terminators.

**Tests:** no/one/two NULs, empty values, embedded NULs, odd bytes, wrong types,
changing sizes/types, maximum size, allocation/query failure.
**Done:** scanners never exceed returned bytes; failures cannot expose stale
data. Keep valid registry representations compatible.

### NSSM 011

**MULTI_SZ formatting boundary — P2, B01.** Review finding 11; reproduced.
Touch `format_double_null` and its formatting callers.

1. Stop scanning at the double NUL within the declared length and guard every
   lookahead. Compute output size from bounded entries.
2. Define whether returned length includes the final terminator and update
   callers consistently. Guarantee a terminated formatted result.

**Tests:** empty/single/multiple entries, allocation exactly at a guarded page
boundary, nonzero following bytes, and format/unformat round trips.
**Done:** no out-of-bounds read or spurious CRLF; valid values round-trip.

### NSSM 012

**Implementation status:** merged in PR 14, unreleased;
see [NSSM-012](fixes/NSSM-012.md).

**Inheritable stdin — P2, B09.** Review finding 12; missing handle flag
reproduced. Touch `get_output_handles` and launch cleanup.

1. Open or duplicate stdin with intentional inheritance, recording the owner
   of each handle. Restrict inheritance to the launch period where practical.
2. Handle duplication/open failure consistently with stdout/stderr and close
   parent copies on both CreateProcess outcomes.

**Tests:** child reads known bytes and EOF from AppStdin, missing input,
access denial, failed child creation, and repeated launches.
**Done:** actual child input succeeds with stable handle counts. Retain
existing file-sharing and disposition options.

### NSSM 013

**Implementation status:** merged in PR 14, unreleased;
see [NSSM-013](fixes/NSSM-013.md).

**UTF-16 timestamp output — P2, B09.** Review finding 13; invalid pointer
reproduced, newline splitting source-traced.

1. Pass the converted buffer address to the writer.
2. Scan line endings in code units for UTF-16, and retain partial units across
   pipe reads. Keep BOM/encoding state consistent across rotation.
3. Define handling of incomplete final input and verify encoding detection;
   avoid silently inventing a different encoding policy in this repair.

**Tests:** exact bytes for UTF-8 and UTF-16LE, LF/CRLF, multiple lines,
one-byte chunks, split newlines/code units, empty lines, and rotation.
**Done:** output remains decodable and timestamps occur only at line starts.

### NSSM 014

**Implementation status:** merged in PR 14, unreleased;
see [NSSM-014](fixes/NSSM-014.md).

B05 initializes basic read/write outputs and the initial observed file size
as part of safe shutdown, but this task remains planned for the complete
timestamp, threshold, partial-write and byte-accounting repair.

**Initialized logger accounting — P2, B09.** Review finding 14; source-traced
and analyzer-supported. Touch timestamp, write and rotation helpers.

1. Initialize local flags/results and reset bytes-written per call.
2. Separate actual file size from configured threshold. Initialize actual
   size on query failure without overwriting policy.
3. Count bytes actually written once, including BOM/timestamps, and propagate
   partial writes and timestamp errors.

**Tests:** empty ASCII and Unicode files, multiple chunks, failed metadata
query, short/failed writes, and expected byte totals around rotation.
**Done:** counters equal observed bytes and no uninitialized read remains.

### NSSM 015

**Owned setting values during reset — P2, B08.** Review finding 15; default
pointer alias reproduced. Touch `setting_set_string` and CLI ownership.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-015](fixes/NSSM-015.md) for reset semantics and failure coverage.

1. Keep borrowed defaults in a local pointer or make an owned copy; never
   replace a caller-owned output with a string literal.
2. Define null-value behavior and audit similar setter callbacks.

**Tests:** reset Application/AppParameters/AppDirectory and other string
settings with null, empty and nonempty defaults; inject allocation and
registry failures under heap diagnostics.
**Done:** each allocation has one owner and static strings are never freed.
Document the actual default written/deleted for each reset.

### NSSM 016

**Implementation status:** merged in PR 15, unreleased;
see [NSSM-016](fixes/NSSM-016.md).

**Correct AppExit export — P2, B10.** Review finding 16; lost numeric rule
reproduced, nonprogressing enumeration source-traced.

1. Select the default from an empty name rather than the terminator after
   scanning a numeric name; require all digits with checked conversion.
2. Resize enumeration buffers within limits or fail with a diagnostic;
   never retry the same error indefinitely.
3. Pass the correct default string from setting metadata to the getter.
   Close the registry key and free each retrieved value.

**Tests:** default plus multiple numeric rules, maximum exit code, long or
invalid names, enumeration failure, and export/import equality. Preserve the
Default=Ignore/42=Exit reproduction.
**Done:** every explicit rule is preserved or export reports failure.
Depends on NSSM-002 and NSSM-033; retain CONT-002's typed dump helper.

The B03 caller audit also found that the AppExit setter retains the original
numeric key spelling while lookup formats the parsed code as decimal. Add
round-trip cases for leading zeros, hexadecimal, whitespace and plus signs,
then define canonicalization/migration behavior. This mismatch is
source-inspected; B03's AppExit integration tests cover canonical decimal IDs.

### NSSM 017

**Launch handle cleanup — P2, B04.** Review finding 17; hook leak reproduced.
Touch service/hook CreateProcess paths and `env.cpp:test_environment`.

**Implementation status:** merged in PR 9, unreleased; see [NSSM-017](fixes/NSSM-017.md).

1. Close primary-thread handles after any required ResumeThread operation.
2. Close process and thread handles from environment-validation children,
   including termination failure and any required bounded wait.
3. Audit startup failures after process creation so a child or handle cannot
   be abandoned.

**Tests:** successful, suspended, failed and repeated launches; hook
completion/fallback; environment validation. Use at least 200 iterations in
a bounded stress test with stable post-cleanup handle counts.
**Done:** ownership accounting balances on every branch; no long-term growth.

### NSSM 018

**Correct administrator-check output type — P2, B08.** Review finding 18;
four-byte write reproduced.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-018](fixes/NSSM-018.md) for guarded API-contract evidence.

Use a local BOOL initialized to FALSE for CheckTokenMembership, check its
return value, and assign a Boolean result to `is_admin`. Keep SID cleanup
balanced. Test member/nonmember/API failure and guard adjacent storage.
**Done:** no write beyond the bool object; failure has a defined result.

### NSSM 019

**Defined zero-duration waits — P2, B08.** Review finding 19; source-traced.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-019](fixes/NSSM-019.md) for the reproduced missing poll and defined results.

Make `await_single_handle` perform a nonblocking wait when timeout is zero,
mapping WAIT_OBJECT_0, WAIT_TIMEOUT and WAIT_FAILED to the documented result.
Keep status/checkpoint updates appropriate for actual waits. Test signaled,
unsignaled and invalid handles at zero and positive timeouts, including the
AppThrottle and stop-delay callers.
**Done:** no uninitialized result and no unintended sleep at zero.

### NSSM 020

**Implementation status:** merged in PR 16, unreleased;
see [NSSM-020](fixes/NSSM-020.md) and [B11 validation](reviews/b11-validation.md).

**GUI borrowed service handle — P2, B11.** Review finding 20; source-traced.

1. Separate temporary configuration from owned SCM handles, or obtain a
   distinct handle for the temporary object. Avoid copying owning handles.
2. Audit configure/edit/install cleanup paths and the transfer on success.

**Tests:** invalid environment or missing field, correction and retry in the
same dialog, save failure, success and cancel; count CloseServiceHandle calls
in a fixture and perform an interactive smoke check.
**Done:** validation cannot invalidate the original dialog's handle.

### NSSM 021

**Caller-owned install object — P2, B08.** Review finding 21; source-traced.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-021](fixes/NSSM-021.md) for CLI and hidden-GUI reproduction/coverage.

Keep `install_service` from destroying its caller's service object. Release
only internally acquired resources, leave object cleanup to CLI/GUI owners,
and audit each return path. Inject SCM-open denial and subsequent install
failures through both callers.
**Done:** one destruction per object and the original error is reported.

### NSSM 022

**Implementation status:** merged in PR 14, unreleased;
see [NSSM-022](fixes/NSSM-022.md).

**Safe error-log insertions — P2, B09.** Review finding 22; source-traced.

B03 includes the missing sentinel on the invalid-affinity warning, needed for
its stricter rejection path. B05 adds the missing sentinel on the pipe-creation
failure message while repairing that setup path. The remaining calls and typed wrapper below are
merged in B09 through PR 14, unreleased.

1. Pass the formatted return-code string and add missing null terminators at
   the identified log_event calls. Audit all calls against message resources.
2. Add a small typed or counted insertion wrapper to prevent recurrence,
   keeping event IDs, translations and resource encodings intact.

**Tests:** force startup, pipe-creation and affinity failures; capture the
event ID and exact insertion strings. Test zero and multiple insertions.
**Done:** error reporting never reads an integer or unrelated vararg as text.
The broader wrapper can be a follow-up commit after the direct fixes.

### NSSM 023

**Implementation status:** implementation and tests present, pending merge;
see [NSSM-023](fixes/NSSM-023.md) and [B12 validation](reviews/b12-validation.md).

**Dependency reset and merge semantics — P2, B12.** Review finding 23;
source-traced against the SCM API contract.

1. Represent no-change separately from an explicitly empty dependency list.
2. Parse service/group entries, merge the intended remaining entries, and
   construct one valid MULTI_SZ without a leading empty entry.
3. Preserve group prefixes and existing dependency order where possible.

**Tests:** reset each type with the other absent/present, remove the final
service/group, duplicates, empty lists and failed updates. Verify buffers in
unit tests and query a disposable service after real SCM updates.
**Done:** only requested dependencies change; an explicit reset really clears.

### NSSM 024

**Bounded stop, pause and continue waits — P2, B13.** Review finding 24;
source-traced.

1. Extract or reuse progress-aware polling with separate action deadlines.
   Preserve the already-fixed START contract.
2. Calculate a finite stop allowance that accounts for configured stop
   methods and hook waits, using overflow-safe arithmetic; use a documented
   finite policy for native services whose shutdown budget is unknown.
3. Distinguish completed, failed, query-error, stalled and total-timeout
   results. Specify exact defaults in usage documentation in this PR before
   merging; do not silently apply START's 60-second limit to every shutdown.

**Tests:** unchanged pending state, progressing checkpoints, changing wait
hints, terminal errors, query failure, clock rollover, unusually long valid
shutdown, and a total limit despite continuous progress.
**Done:** each command terminates within its documented bound and reports
timeout as failure, without regressing startup tests.

## Additional review follow-ups

### NSSM 025

**Implementation status:** merged in PR 16, unreleased;
see [NSSM-025](fixes/NSSM-025.md) and [B11 validation](reviews/b11-validation.md).

**Password-confirmation cleanup — P2, B11.** Review follow-up A.

Use a scoped secure buffer or centralized cleanup for the GUI confirmation
password. Wipe the allocated region before freeing on success and failure,
and keep service-password ownership separate. Test equal/unequal passwords,
dialog read failure and allocation failure with a test allocator verifying
zeroing before free. Done when no confirmation allocation survives a dialog
attempt. Describe local memory retention without claiming a demonstrated leak
to another user or process.

### NSSM 026

**Implementation status:** merged in PR 16, unreleased;
see [NSSM-026](fixes/NSSM-026.md) and [B11 validation](reviews/b11-validation.md).

**Account lookup resource cleanup — P2, B11.** First part of follow-up B.

Trace ownership of the policy, SID, translated names/domains and conversion
buffers in `canonicalise_username`. Use matching Windows deallocators and a
single cleanup path or small scoped owners. Initialize all outputs before
failure-prone API calls. Inject failure at each stage; repeat local-account
lookup and record domain-account coverage separately. Done when allocations
and handles balance on every path.

### NSSM 027

**Implementation status:** merged in PR 16, unreleased;
see [NSSM-027](fixes/NSSM-027.md) and [B11 validation](reviews/b11-validation.md).

**Counted LSA right comparison — P2, B11.** Second part of follow-up B.

Validate equal, aligned byte lengths and compare exactly Length divided by
sizeof(wchar_t), without assuming an LSA string has a NUL. Remove use of
MaximumLength as a character count. Test nonterminated guarded strings,
case variants, prefixes, wrong lengths and unrelated rights. Done when
comparison never reads beyond either counted string and recognition is exact.

### NSSM 028

**Debug-token failure handling — P2, B08.** Follow-up C.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-028](fixes/NSSM-028.md) for scoped ownership and fatal restoration failures.

Initialize the token handle, check ImpersonateSelf and the retrying
OpenThreadToken call, and track whether this function established
impersonation. Balance that change on the correct paths without discarding
a caller's pre-existing impersonation. Check privilege-adjustment outcomes
and required GetLastError semantics. Test no token, access denial,
impersonation/retry failure and privilege failure with injected APIs.
Done when failures use no uninitialized handle and leave identity state
consistent.

### NSSM 029

**CPU detection boundaries — P2, B03.** Follow-up D.

**Implementation status:** merged in PR 8, unreleased; see [NSSM-029](fixes/NSSM-029.md).

Bound shifts before evaluation, count set bits correctly, and audit callers
that assume CPU indices are dense. Use the actual allowed mask in affinity
validation/UI where necessary. Test zero/sparse/full masks, bit 63, x86 masks
and API failure. Keep processor-group support explicit; a multi-group redesign
requires its own compatibility decision and hardware validation. Done when
supported masks are handled without undefined shifts or phantom CPU choices.

### NSSM 030

**Implementation status:** merged in PR 15, unreleased;
see [NSSM-030](fixes/NSSM-030.md).

**Read-only configuration export access — P2, B10.** Follow-up E.

Assign requested SCM/service/registry rights by operation. Remove change
rights from MODE_DUMPING while retaining rights genuinely needed for query
and enumeration. Test the requested masks and export using a disposable
service with query-only permission. Verify setting still requires change
rights. Done when export succeeds with the documented minimum access.

### NSSM 031

**Checked unsigned numeric parsing — P2, B03.** Follow-up F.

**Implementation status:** merged in PR 8, unreleased; see [NSSM-031](fixes/NSSM-031.md).
The implementation retains legacy base-zero parsing (decimal, octal and hex),
leading whitespace and an optional plus sign, while rejecting negatives,
missing digits, overflow and standalone suffixes.

Define accepted decimal syntax, detect no digits, sign misuse and range
overflow, and check conversion errors. Preserve a prefix-parsing mode for
affinity tokens that intentionally return an end pointer; require complete
consumption for standalone settings. Audit all callers rather than globally
rejecting delimiters needed by one caller. Test empty/whitespace/sign-only
input, negatives, 0, maximum value, overflow, suffixes, ranges and delimiters.
Done when each caller enforces its range and documents any newly rejected input.

### NSSM 032

**Error-message buffer units — P2, B08.** Follow-up G; capacity mismatch
confirmed, long system-message overflow not reproduced.

**Implementation status:** merged in PR 13, unreleased;
see [NSSM-032](fixes/NSSM-032.md) for guarded capacity tests and automatic
thread-local buffer ownership replacing manual TLS-slot setup.

Use FormatMessage allocation mode with matching LocalFree, or allocate an
explicit TCHAR count and pass that count. Check TLS replacement/fallback
ownership and correct fallback capacities too. Inject a long formatted
message and allocation/formatting failures. Done when capacities are correct
and repeated error_string calls retain no stale pointer or extra allocation.

### NSSM 033

**Implementation status:** merged in PR 15, unreleased;
see [NSSM-033](fixes/NSSM-033.md).

**Initialized AppExit fallback — P2, B10.** First part of follow-up H.

Initialize action/default outputs before registry access. Return a documented
default or a clear failure for absent, malformed and oversized values;
distinguish a missing numeric override from a missing default.
Test missing key/default/override, each valid action, wrong type, invalid
action and query failure. Done when no successful result exposes uninitialized
text; validate alongside NSSM-016's export round trip.

### NSSM 034

**Implementation status:** merged in PR 16, unreleased;
see [NSSM-034](fixes/NSSM-034.md) and [B11 validation](reviews/b11-validation.md).

**Literal hook environment references — confirmed P2, B11.**
Second part of follow-up H; GUI edit-round-trip mutation reproduced in B11.

1. Reproduce editing an unrelated GUI setting when a hook command contains
   literal %VARIABLE% references and quoted arguments.
2. If the stored command changes, give configuration editing a raw-read path
   while keeping expansion at execution time. Preserve CONT-004's quoting
   behavior and REG_SZ/REG_EXPAND_SZ semantics.
3. If it already round-trips correctly, close this task as not confirmed with
   the test and evidence; do not invent a fixed-bug claim.

**Tests:** view/cancel, unrelated save, explicit hook edit, changing the
environment after saving, and actual hook launch.
**Done:** editing preserves stored expressions and execution expands them at
the intended time, or a recorded test establishes that no repair is needed.

## Documentation required with every fix

Use the [fix-record template](fixes/TEMPLATE.md) to create
`docs/fixes/NSSM-NNN.md`. Link it from the [fix index](fixes/README.md) and
the Unreleased changelog in the same PR as the implementation. Each record
must contain:

- Trigger and user-visible impact; old and corrected behavior.
- Exact baseline checked and whether evidence is runtime, injected, or source
  inspection. Keep v2.24, the 2.24-103 source snapshot, and third-party binaries
  separate.
- Source locations, test names, observed results, compatibility/upgrade notes,
  PR URL, and immutable implementation commit when known.
- State and first release: planned, in progress, fixed but unreleased, released,
  not confirmed, or explicitly deferred with a reason.

A PR may link its record by PR number while its own commit hash is not final.
Add the merged implementation hash in the next documentation/release update;
do not attempt to embed a commit's own hash inside itself. Mark "fixed" only
after merge and required checks. Mark "released" only when the public tag and
matching artifacts exist.

Existing CONT records are backfilled now. They remain **fixed but unreleased**
for code fixes, and **implemented but unreleased** for build infrastructure.
Review tasks remain planned or in progress until their own implementation lands.

## Release and comparison acceptance

1. Close all nine P1 tasks with deterministic regression coverage and relevant
   integration checks. Explicitly list remaining P2 tasks in release notes;
   never imply that a partial release completes this plan.
2. Run the full build matrix and targeted memory/handle diagnostics. Run
   opt-in SCM, logger drain/restart, child-tree, stdin and GUI retry checks
   for affected features, recording OS/toolchain and outcomes.
3. Maintain `docs/releases/<tag>.md` with included fix IDs, known issues,
   tested Windows versions, compatibility changes and upgrade guidance.
   Update each included record's first-release field during release
   preparation, then verify it against the published tag.
4. Build from a clean, pinned commit and publish per-artifact metadata:
   full commit, tag/version output, architecture/configuration, legacy
   baseline, toolchain/SDK versions, build workflow, test results and SHA-256.
   A source ZIP must carry equivalent revision metadata. Add packaging/
   manifest automation in a dedicated implementation PR; it does not exist
   merely because this plan specifies it.
5. Preserve original tags. Choose a distinct unused continuation release tag
   supported by version generation; identify artifacts as NSSM Native.
   Verify generated version output, source-archive identity and dirty-tree
   rejection before publishing. No tag/version is assigned by this plan.
6. Follow the [comparison guide](comparing-builds.md) for renamed source paths
   and baseline selection. Link release notes to Git records; CI artifact
   retention alone is insufficient for the permanent comparison history.

The record is complete when another maintainer can determine which exact
build includes a fix, what behavior changed, and which test demonstrates it
without relying on this conversation or a developer's ignored build folder.
