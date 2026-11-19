# Beta.2 review implementation plan

Baseline `e1cc746`. The numbers in parentheses refer to the [beta.2 review](2026-11-19-beta2-code-review.md). Check each step after implementing its change and verifying its acceptance criteria.

## 1. Protect logs during startup

- [ ] **Archive outcome (1).** Return explicit skipped, completed, cancelled and failed outcomes from offline rotation. Check the result before opening either output. Exercise a denied rename with a destructive creation disposition on stdout and stderr and verify the original bytes survive.
- [ ] **Interruptible delay (3).** Pass the service stop event to the offline copy/truncate wait. Preserve the original on cancellation or wait failure. Specify the maximum DWORD behavior and test cancellation after the copy.
- [ ] **Independent writers (2).** Open append-mode disk output with per-write append semantics. Retain device, explicit truncation and logger rotation behavior. Alternate writes from two independent opens and compare complete records.
- [ ] **Stream identity (5).** Compare NTFS stream names as well as base-file identity. Exercise separate named streams, the default stream and real aliases.
- [ ] **Short writes (6).** Bypass encoding detection for transparent pipe forwarding. Check a short line before EOF, plus existing split UTF-16 and rotation cases.

## 2. Make configuration backups faithful

- [ ] **Output errors (4).** Check each emitted command and the final stdout flush. Verify export signals failure when its output cannot be written.
- [ ] **Environment errors (14).** Propagate an entry-allocation failure after releasing the environment block. Exercise an incomplete multi-entry dump.
- [ ] **Literal strings (7).** Preserve the stored type for Application, AppParameters, AppDirectory and all three I/O paths through dump and replay. Keep the existing default setter behavior. Test literal and expandable values and document how to replay the new syntax.
- [ ] **Reset guide (16).** Correct the description of resetting Application and distinguish optional setting behavior. Cross-check against the CLI and live export fixture.

## 3. Preserve service and editor state

- [ ] **Startup outcomes (8).** Distinguish early exit, stop, wait failure and a surviving application. Dispatch the exit action once without announcing a known-dead application. Verify early exit, restart and a concurrent stop.
- [ ] **Power events (9).** Move power-hook work off the dispatcher, retain notification order and discard stale work during shutdown or restart. Exercise blocked hook processing and subsequent control delivery.
- [ ] **Affinity edits (10).** Preserve the complete stored mask on unrelated GUI saves and honor explicit affinity changes. Cover a mask with unrepresented CPU IDs.
- [ ] **Wrapper identification (11).** Check the configured SCM executable before interpreting Parameters as NSSM settings. Share classification between management and listing, and support renamed and separately installed NSSM executables. Test a native service with its own Application value and a valid wrapper copy.
- [ ] **Named operations (12).** Request manager-connect access for operations on known services. Preserve service-specific access checks and actual enumeration. Check key-name and display-name resolution.
- [ ] **Virtual accounts (15).** Remove the allocation from virtual-account matching. Check exact, case-insensitive matching, domain boundaries and mismatched names.

## 4. Verify release evidence and close the review

- [ ] **Package evidence (13).** Require checksum-covered JUnit reports for all four builds. Validate their identities, counts and results against the manifests. Exercise missing and contradictory evidence and a valid package.
- [ ] **Acceptance.** Run x86 and x64 Debug and Release suites, exercise the current x64 build on the disposable Windows VM, including upgrade/rollback, and review the complete diff against the sixteen findings. Record results, any limits and the final checklist state here.
