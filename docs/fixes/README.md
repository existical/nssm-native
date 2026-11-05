# Fix history and status

This index connects behavior changes to their implementation, tests and first
released build. Read [comparing builds](../comparing-builds.md) to select the
correct legacy baseline. The [changelog](../../CHANGELOG.md) summarizes changes;
the records here explain them.

All 37 review/acceptance fixes and the five continuation changes below first
shipped in [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) on 5 November 2026.
The public tag, original packages and all 13 uploaded assets were verified; see
the [publication record](../releases/v2.25.0-beta.1-publication.json).
This is a beta release with the acceptance limits in its release notes.

| ID | Change | State | Implementation | First release |
| --- | --- | --- | --- | --- |
| [CONT-001](implemented.md#cont-001) | Source layout, CMake and Windows CI | Released | [PR 1](https://github.com/existical/nssm-native/pull/1), `486e81f` | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [CONT-002](implemented.md#cont-002) | Typed registry values in settings dump helper | Released | [PR 2](https://github.com/existical/nssm-native/pull/2), `40b8969` | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [CONT-003](implemented.md#cont-003) | Empty environment values no longer skip entries | Released | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [CONT-004](implemented.md#cont-004) | Complete hook command quoting is preserved | Released | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [CONT-005](implemented.md#cont-005) | Startup waits independently of restart throttling | Released | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

The B01 implementation was merged in
[PR 5](https://github.com/existical/nssm-native/pull/5) at
[bdeca1c](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b).
All four Windows CI configurations passed. These fixes are included in the first beta:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-001](NSSM-001.md) | Bound registry environment expansion | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-003](NSSM-003.md) | Replace duplicate environment keys once | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-010](NSSM-010.md) | Validate registry strings and environment blocks | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-011](NSSM-011.md) | Bound MULTI_SZ formatting | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B02 merged in [PR 7](https://github.com/existical/nssm-native/pull/7), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-002](NSSM-002.md) | Bound command-quoting output | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B03 merged in [PR 8](https://github.com/existical/nssm-native/pull/8), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-004](NSSM-004.md) | Bound affinity tokens and CPU ranges | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-029](NSSM-029.md) | Count sparse masks and map GUI rows to CPU IDs | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-031](NSSM-031.md) | Reject invalid and overflowing unsigned numbers | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B04 merged in [PR 9](https://github.com/existical/nssm-native/pull/9), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-005](NSSM-005.md) | Separate hook caller and worker ownership | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-006](NSSM-006.md) | Return fallback results without duplicate cleanup | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-017](NSSM-017.md) | Close launch handles and validate cleanup | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B05 merged in [PR 10](https://github.com/existical/nssm-native/pull/10), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-007](NSSM-007.md) | Own logger state and join before releasing thread handles | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B06 merged in [PR 11](https://github.com/existical/nssm-native/pull/11), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-008](NSSM-008.md) | Separate offline rotation size, age and delay options | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B07 merged in [PR 12](https://github.com/existical/nssm-native/pull/12), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-009](NSSM-009.md) | Retain validated process identities through tree cleanup | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B08 merged in [PR 13](https://github.com/existical/nssm-native/pull/13), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-015](NSSM-015.md) | Preserve caller ownership during string resets | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-018](NSSM-018.md) | Use BOOL storage for administrator membership | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-019](NSSM-019.md) | Define zero-duration handle waits | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-021](NSSM-021.md) | Leave install-object cleanup to its caller | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-028](NSSM-028.md) | Restore scoped debug privilege and impersonation state | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-032](NSSM-032.md) | Correct message capacities and thread-local ownership | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B09 merged in [PR 14](https://github.com/existical/nssm-native/pull/14), with all
four Windows CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-012](NSSM-012.md) | Inherit configured stdin in the child | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-013](NSSM-013.md) | Preserve UTF-16 units and timestamp bytes | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-014](NSSM-014.md) | Account for actual log writes and rotation | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-022](NSSM-022.md) | Use counted string-only event insertions | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B10 merged in [PR 15](https://github.com/existical/nssm-native/pull/15), with all
four Windows CI configurations passing, including the real SCM export test:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-016](NSSM-016.md) | Preserve explicit AppExit export rules | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-030](NSSM-030.md) | Export with read-only service access | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-033](NSSM-033.md) | Initialize and validate AppExit fallbacks | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B11 merged in [PR 16](https://github.com/existical/nssm-native/pull/16), with all
four Windows CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-020](NSSM-020.md) | Retain GUI service-handle ownership across retries | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-025](NSSM-025.md) | Wipe and free password confirmations | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-026](NSSM-026.md) | Balance account lookup resources and initialize outputs | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-027](NSSM-027.md) | Bound LSA right comparisons by counted characters | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-034](NSSM-034.md) | Preserve literal hook references and registry types in GUI edits | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B12 merged in [PR 17](https://github.com/existical/nssm-native/pull/17),
with all four Windows CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-023](NSSM-023.md) | Correct dependency resets, typed merges and validation cleanup | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B13 merged in [PR 18](https://github.com/existical/nssm-native/pull/18),
with all four Windows CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-024](NSSM-024.md) | Bound stop, pause and continue polling and preserve failure details | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B14 acceptance found two release blockers beyond the original review:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-035](NSSM-035.md) | Pass byte capacity to standard-I/O registry readers | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |
| [NSSM-036](NSSM-036.md) | Wait for in-flight exit cleanup before STOPPED | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

B15 acceptance found a further startup/STOP race, merged in
[PR 22](https://github.com/existical/nssm-native/pull/22) at `6ff83e1` before
the upgrade/rollback coverage in [PR 21](https://github.com/existical/nssm-native/pull/21).
All 37 findings are released in the first beta; see the
[post-merge evidence](../reviews/b15-validation.md#post-merge-validation).

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-037](NSSM-037.md) | Own startup waits and cancel launch/monitor publication safely | Released | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

The [implementation plan](../remediation-plan.md) tracks NSSM-001 through
NSSM-037. Check each record's state before assuming inclusion in a build.
NSSM-001 through NSSM-009 are P1. NSSM-034 was confirmed by B11; the plan explains
the evidence for the other tasks. Add rows and individual records here as work
starts; never reuse an ID.

Use these states consistently:

| State | Meaning |
| --- | --- |
| Planned | An approach exists; implementation has not landed |
| In progress | Work or a PR exists; its inclusion must not be assumed |
| Fixed, unreleased | Implementation is merged and required tests pass |
| Released | A named public tag and matching artifacts include the fix |
| Not confirmed | Investigation/test did not establish the suspected defect |
| Deferred | Open work postponed with a written reason and affected scope |

For each behavior PR, copy [TEMPLATE.md](TEMPLATE.md) into
`docs/fixes/NSSM-NNN.md`, fill it with actual evidence, and link it from this
index and the changelog. Include source/PR links even before the implementation
hash is final. Record the immutable hash after merge, in a follow-up or release
documentation commit. Never claim tests or old-version reproduction that were
not performed.

Release preparation must fill the first-release column and individual
records, list included IDs in `docs/releases/<tag>.md`, and publish build
metadata and hashes described in [comparing builds](../comparing-builds.md).
Keep records and release notes in Git even when CI artifacts expire.
