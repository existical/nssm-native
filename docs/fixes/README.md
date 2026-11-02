# Fix history and status

This index connects behavior changes to their implementation, tests and first
released build. Read [comparing builds](../comparing-builds.md) to select the
correct legacy baseline. The [changelog](../../CHANGELOG.md) summarizes changes;
the records here explain them.

The five continuation changes below are already merged. Their first tagged
continuation release has not yet been recorded. A successful CI artifact is
not a tagged release.

| ID | Change | State | Implementation | First release |
| --- | --- | --- | --- | --- |
| [CONT-001](implemented.md#cont-001) | Source layout, CMake and Windows CI | Implemented, unreleased | [PR 1](https://github.com/existical/nssm-native/pull/1), `486e81f` | Not yet released |
| [CONT-002](implemented.md#cont-002) | Typed registry values in settings dump helper | Fixed, unreleased | [PR 2](https://github.com/existical/nssm-native/pull/2), `40b8969` | Not yet released |
| [CONT-003](implemented.md#cont-003) | Empty environment values no longer skip entries | Fixed, unreleased | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | Not yet released |
| [CONT-004](implemented.md#cont-004) | Complete hook command quoting is preserved | Fixed, unreleased | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | Not yet released |
| [CONT-005](implemented.md#cont-005) | Startup waits independently of restart throttling | Fixed, unreleased | [PR 3](https://github.com/existical/nssm-native/pull/3), `8fede4b` | Not yet released |

The B01 implementation was merged in
[PR 5](https://github.com/existical/nssm-native/pull/5) at
[bdeca1c](https://github.com/existical/nssm-native/commit/bdeca1cf21314b8fc88b1927333aa69197a8571b).
All four Windows CI configurations passed. These fixes are not yet released:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-001](NSSM-001.md) | Bound registry environment expansion | Fixed, unreleased | Not yet released |
| [NSSM-003](NSSM-003.md) | Replace duplicate environment keys once | Fixed, unreleased | Not yet released |
| [NSSM-010](NSSM-010.md) | Validate registry strings and environment blocks | Fixed, unreleased | Not yet released |
| [NSSM-011](NSSM-011.md) | Bound MULTI_SZ formatting | Fixed, unreleased | Not yet released |

B02 merged in [PR 7](https://github.com/existical/nssm-native/pull/7), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-002](NSSM-002.md) | Bound command-quoting output | Fixed, unreleased | Not yet released |

B03 merged in [PR 8](https://github.com/existical/nssm-native/pull/8), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-004](NSSM-004.md) | Bound affinity tokens and CPU ranges | Fixed, unreleased | Not yet released |
| [NSSM-029](NSSM-029.md) | Count sparse masks and map GUI rows to CPU IDs | Fixed, unreleased | Not yet released |
| [NSSM-031](NSSM-031.md) | Reject invalid and overflowing unsigned numbers | Fixed, unreleased | Not yet released |

B04 merged in [PR 9](https://github.com/existical/nssm-native/pull/9), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-005](NSSM-005.md) | Separate hook caller and worker ownership | Fixed, unreleased | Not yet released |
| [NSSM-006](NSSM-006.md) | Return fallback results without duplicate cleanup | Fixed, unreleased | Not yet released |
| [NSSM-017](NSSM-017.md) | Close launch handles and validate cleanup | Fixed, unreleased | Not yet released |

B05 merged in [PR 10](https://github.com/existical/nssm-native/pull/10), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-007](NSSM-007.md) | Own logger state and join before releasing thread handles | Fixed, unreleased | Not yet released |

B06 merged in [PR 11](https://github.com/existical/nssm-native/pull/11), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-008](NSSM-008.md) | Separate offline rotation size, age and delay options | Fixed, unreleased | Not yet released |

B07 merged in [PR 12](https://github.com/existical/nssm-native/pull/12), with
all four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-009](NSSM-009.md) | Retain validated process identities through tree cleanup | Fixed, unreleased | Not yet released |

B08 merged in [PR 13](https://github.com/existical/nssm-native/pull/13), with all
four CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-015](NSSM-015.md) | Preserve caller ownership during string resets | Fixed, unreleased | Not yet released |
| [NSSM-018](NSSM-018.md) | Use BOOL storage for administrator membership | Fixed, unreleased | Not yet released |
| [NSSM-019](NSSM-019.md) | Define zero-duration handle waits | Fixed, unreleased | Not yet released |
| [NSSM-021](NSSM-021.md) | Leave install-object cleanup to its caller | Fixed, unreleased | Not yet released |
| [NSSM-028](NSSM-028.md) | Restore scoped debug privilege and impersonation state | Fixed, unreleased | Not yet released |
| [NSSM-032](NSSM-032.md) | Correct message capacities and thread-local ownership | Fixed, unreleased | Not yet released |

B09 merged in [PR 14](https://github.com/existical/nssm-native/pull/14), with all
four Windows CI configurations passing:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-012](NSSM-012.md) | Inherit configured stdin in the child | Fixed, unreleased | Not yet released |
| [NSSM-013](NSSM-013.md) | Preserve UTF-16 units and timestamp bytes | Fixed, unreleased | Not yet released |
| [NSSM-014](NSSM-014.md) | Account for actual log writes and rotation | Fixed, unreleased | Not yet released |
| [NSSM-022](NSSM-022.md) | Use counted string-only event insertions | Fixed, unreleased | Not yet released |

B10 merged in [PR 15](https://github.com/existical/nssm-native/pull/15), with all
four Windows CI configurations passing, including the real SCM export test:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-016](NSSM-016.md) | Preserve explicit AppExit export rules | Fixed, unreleased | Not yet released |
| [NSSM-030](NSSM-030.md) | Export with read-only service access | Fixed, unreleased | Not yet released |
| [NSSM-033](NSSM-033.md) | Initialize and validate AppExit fallbacks | Fixed, unreleased | Not yet released |

B11 has implementation and tests, pending review and merge:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-020](NSSM-020.md) | Retain GUI service-handle ownership across retries | In progress | Not yet released |
| [NSSM-025](NSSM-025.md) | Wipe and free password confirmations | In progress | Not yet released |
| [NSSM-026](NSSM-026.md) | Balance account lookup resources and initialize outputs | In progress | Not yet released |
| [NSSM-027](NSSM-027.md) | Bound LSA right comparisons by counted characters | In progress | Not yet released |
| [NSSM-034](NSSM-034.md) | Preserve literal hook references and registry types in GUI edits | In progress; confirmed | Not yet released |

The [implementation plan](../remediation-plan.md) tracks NSSM-001 through
NSSM-034. Check each record's state before assuming inclusion in a build.
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
