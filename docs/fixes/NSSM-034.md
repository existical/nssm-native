# NSSM 034 Literal hook references in GUI edits

| Field | Value |
| --- | --- |
| Priority and review | P2; B11 (confirmed follow-up) |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f28a43141a576033713190fe9b716fb503ac227a` |
| Fix PR | [PR 16](https://github.com/existical/nssm-native/pull/16) |
| Implementation commit | `b91392150c23ecec4299c4eec75a7fcb65f867fb` |
| Merge commit | `f969dd985039a8d2fe77a0ed0a24e077894c2bef` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

This initially unconfirmed follow-up is now reproduced. A hook containing
`%VARIABLE%` and a final quoted argument was loaded through the expanding
reader; saving an unrelated GUI setting stored the expanded text. Two initial
`nssm.gui_hooks` assertions failed, covering displayed and persisted text.
Legacy source inspection confirms the same expanding GUI read. Legacy also
had the separate quoting defect recorded in CONT-004; that earlier fix did
not address expansion during editing.

Hook reads now support an explicit raw mode with the stored registry type.
The GUI uses that mode and preserves REG_SZ versus REG_EXPAND_SZ on save,
including explicit edits of an existing hook. New hooks retain REG_EXPAND_SZ.
Execution-time reads still expand REG_EXPAND_SZ and leave REG_SZ literal;
complete command-line quoting is preserved in both modes. No existing stored
commands are automatically changed or reconstructed from past expanded values.
CLI hook get/dump behavior is unchanged by this GUI-focused change.

Each dialog clears its hook scratch values. Entries that cannot be read are
skipped during unrelated saves, and programmatic display updates do not count
as user edits. This avoids overwriting unreadable entries with empty values.
Explicit edits can replace such an entry using the normal expandable type.
Concurrent edits by another process remain outside the snapshot semantics.

[GUI tests](../../tests/install_gui.cpp) cover view/cancel, unrelated save,
explicit edit for both registry types, malformed-entry preservation, and a
new dialog after an earlier edit. A reference is saved while pointing at a
nonexistent executable; the environment then changes and the
[production hook launcher](../../tests/hook_lifecycle.cpp) runs the test child
successfully with its final quoted argument. Registry writes are isolated to
a disposable HKCU tree in the test process. No installed service is changed.

See [B11 validation](../reviews/b11-validation.md) for the build matrix,
sanitizer results, tool versions and remaining coverage limits.
