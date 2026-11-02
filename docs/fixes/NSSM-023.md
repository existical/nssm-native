# NSSM 023 Dependency reset and merge semantics

| Field | Value |
| --- | --- |
| Priority and review | P2; B12 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f969dd985039a8d2fe77a0ed0a24e077894c2bef` |
| Fix PR | [PR 17](https://github.com/existical/nssm-native/pull/17) |
| Implementation commit | `856abf321fc56f890ef0e6ea17b7def04e02c53a` |
| First release | Not yet released |

Resetting DependOnService or DependOnGroup when the other type was absent
passed a null dependency pointer to ChangeServiceConfig. Windows interprets
that as no change, so the reset reported success while retaining the old list.
Removing the final service dependency with groups present could put a leading
empty entry before the group list, making Windows see an empty dependency
list. The legacy source contains both expressions. Six initial regression
checks failed: two null-pointer contract checks, three incorrect stored-list
checks and one unclosed dependency-service handle.

The two settings now share one update path. It reads one complete dependency
snapshot, changes the requested type, and constructs a single valid MULTI_SZ.
An empty result is always a non-null double-NUL block. NULL remains the Windows
API's no-change sentinel; reset never uses it. This follows Microsoft's
[ChangeServiceConfig contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-changeserviceconfigw).

The existing CLI operators remain:

- Plain names or `:name` replace the selected dependency type; additional
  arguments supply more entries. CRLF/LF lists and blank lines are handled.
- `+name` adds one entry and `-name` removes matching entries without regard
  to case. An optional group `+` prefix is retained, so `++Group` adds and
  `-+Group` removes that group.
- Reset, an empty value, or a colon with no following entries clears only
  the selected type. Bare add/remove operators and multiple-entry add/remove
  requests fail; they cannot accidentally clear a list.
- Duplicate requested names are collapsed case-insensitively, preserving the
  first spelling. Removal deletes all matching entries of the requested type.
  Entries of the other type, including duplicates, are retained. Distinct
  display-name aliases are still resolved by the existing service lookup;
  this is not a promise to deduplicate aliases before canonicalization.
- Replacement entries occupy the first old position of their type, or append
  if that type was absent. Addition appends. Other entries retain their order
  in the submitted buffer. Windows may regroup service/group storage.

Group input receives its required SC_GROUP_IDENTIFIER prefix. Group entries
cannot be injected through DependOnService. Overlong and invalid names fail
before an SCM write. Reset/removal does not require retained names to remain
resolvable: an unrelated deleted or inaccessible dependency is preserved.
Adding/replacing names retains the existing canonical service/group lookup.
A failed query, validation, allocation or SCM write reports failure, rather
than a successful partial update. Read/modify/write is not a transaction
against concurrent administrators; use a quiet configuration window.

The touched validator now closes its SCM, dependency-service and registry
handles and releases allocations on every exit. It checks canonical-buffer
allocation, reads group metadata with the bounded REG_MULTI_SZ reader, and
never treats a pointer into caller input as an owned group allocation. Missing,
malformed or unreadable group metadata fails safely. Canonicalization no longer
modifies the caller's input. Dependency queries validate the returned pointer
and double-NUL bounds within the QueryServiceConfig allocation and copy the
exact character count, fixing the source-traced extra-character copy. Dependency
dump allocation failures now return failure instead of a successful partial dump.

`nssm.dependencies` uses production setters, queries and validation with
intercepted SCM boundaries and an isolated HKCU registry fixture. It tests
reset/removal with either or both types present, one-character names, empty
lists, order/prefixes/duplicates, missing names, malformed query/group data,
allocation failures, denied updates, dump failure and 50 rejected group updates
with stable handle counts. See [tests](../../tests/dependencies.cpp).

The opt-in `nssm.dependencies_live` test uses the real CLI and SCM on a
disposable elevated runner. Four unique services stay disabled throughout.
Existing group names are read only. It checks reset/remove/set/get, duplicates,
failed validation, SCM self-dependency rejection, query-only denial and replay
of exported dependency commands into an owned destination. All fixtures are
deleted; no installed user service or system group membership is changed.
See [integration cases](../../tests/dependencies_live_cases.h) and
[B12 validation](../reviews/b12-validation.md) for actual results and limits.
