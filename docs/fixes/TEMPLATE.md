# NSSM fix record template

Copy this file to `docs/fixes/NSSM-NNN.md` when starting a fix. Replace every
placeholder and remove these instructions. Use the same ID in the plan, PR,
test description, index and changelog.

| Field | Value to record |
| --- | --- |
| ID and title | NSSM-NNN and a specific problem name |
| Priority | P1 or P2, with any reassessment explained |
| State | Planned, in progress, fixed/unreleased, released, not confirmed or deferred |
| Review source | Numbered finding or follow-up and reviewed commit |
| Affected baseline checked | Full commit or binary SHA-256 and version |
| Evidence | Runtime reproduction, injected failure/schedule, or source inspection |
| Fix PR | Actual PR URL once available |
| Implementation commit | Immutable hash after merge; pending while under review |
| First release | Not yet released, then exact public tag and release link |

## Trigger and impact

Describe the concrete input, configuration, timing or API failure that reaches
the defect and what the operator observes. State the scope of any affected
version claim. Distinguish the v2.24 tag, the 2.24-103 source snapshot, and
third-party builds.

## Old and corrected behavior

Give a small before/after example. Name the implementation functions and
explain the ownership, bounds or API-contract repair. State remaining limits
that affect users. Do not label a suspected defect fixed until confirmed.

## Regression validation

List committed test files and CTest names, test inputs, expected results and
observed results. Identify which assertion fails on the pre-fix revision and
passes on the fix. Record x86/x64 Debug/Release outcomes and any injected APIs,
heap/handle diagnostics or live integration checks. Link immutable source or
retained evidence; temporary CI logs alone are not the permanent record.

## Compatibility and upgrade

Describe effects on valid settings, malformed inputs, command exit status,
timeouts, registry data, encoding and supported Windows versions as applicable.
Say whether configuration changes or a restart are required. Avoid promising
a supported Windows version that has not been tested.

## Release inclusion

Link the first release and its build metadata/hash records when published.
List later backports or reverts explicitly. Until then, state that the fix is
unreleased and identify the exact merged implementation commit.
