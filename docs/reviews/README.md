# Archived NSSM code review

The [2026 November 2 review](2026-11-02-code-review.txt) describes commit
`5d7454b2cd878b66bcd4b036c33a5b53b184cf3a`. It records 24 numbered findings
and additional follow-ups. The [implementation plan](../remediation-plan.md)
maps them to stable IDs and the [fix index](../fixes/README.md) records later
implementation and release status. The archive's findings describe the
reviewed commit, not necessarily a later checkout.

[Probe output](2026-11-02-probe-results.txt) is preserved with normalized line
endings. The
probes used guarded allocations, quarantined frees and selected Windows API
substitutions to observe bounds and ownership defects. No real services were
installed, changed, stopped or terminated by these probes. In the UTF-16
timestamp result, `error=0` is a stale last-error observation after the helper;
the failure return and zero bytes written are the relevant evidence.

The report's `build/` paths describe local review artifacts, which are
ignored and not included in a fresh clone. Raw compiler/build logs and the
ad hoc diagnostic probe harness are not committed. The captured results
record what was observed; each implementation task must add maintainable
regression tests under `tests/` before being marked fixed.

The existing x86/x64 Debug/Release builds each passed four CTest groups at
the reviewed commit. These tests did not cover the new findings. Live SCM,
interactive GUI, domain-account and long-running concurrency scenarios are
identified as integration-test gaps in the review and plan.

Implementation evidence is retained separately in the
[B01 registry validation](b01-validation.md) and
[B02 quoting validation](b02-validation.md) records.
