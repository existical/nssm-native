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

B06 has an implementation under review, pending merge:

| ID | Change | State | First release |
| --- | --- | --- | --- |
| [NSSM-008](NSSM-008.md) | Separate offline rotation size, age and delay options | In progress | Not yet released |

The [implementation plan](../remediation-plan.md) tracks NSSM-001 through
NSSM-034. Check each record's state before assuming inclusion in a build.
NSSM-001 through NSSM-009 are P1. NSSM-034 needs confirmation; the plan explains
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
