# NSSM 020 GUI service-handle ownership

| Field | Value |
| --- | --- |
| Priority and review | P2; B11 |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f28a43141a576033713190fe9b716fb503ac227a` |
| Fix PR | [PR 16](https://github.com/existical/nssm-native/pull/16) |
| Implementation commit | `b91392150c23ecec4299c4eec75a7fcb65f867fb` |
| Merge commit | `f969dd985039a8d2fe77a0ed0a24e077894c2bef` |
| First release | Not yet released |

An edit attempt copied the original dialog's SCM handle into temporary
configuration. Validation failure, save failure and successful temporary-object
cleanup could close that borrowed handle. A correction and retry in the same
dialog then used an invalid handle. Eight configure error paths also destroyed
the object themselves, while other error returns left cleanup to nobody.
The relevant ownership patterns are present in the recovered legacy source.
The initial `nssm.gui_retry` test reported five failed ownership checks.

`configure` now only fills caller-owned temporary configuration and never
copies an SCM handle or destroys the object. Install/edit callers destroy the
temporary object on every validation result. Edit borrows the original handle
only around `edit_service`, detaches it before cleanup, and leaves it valid for
another attempt. `nssm_gui` owns the original object through dialog creation,
cancellation, message-loop failure and completion. Install/edit/remove reject
allocation failure before proceeding. Existing save error codes are retained;
this does not make a multi-setting save transactional.

The [hidden-dialog tests](../../tests/install_gui.cpp) drive real Windows
controls and production configure/edit/GUI functions, with popup and SCM save
boundaries substituted. They cover missing name/path, invalid environment,
correction/retry, save denial, success, allocation failures, cancel and failed
dialog creation. The save stub checks that the original handle remains open;
production cleanup's CloseServiceHandle boundary is counted. Installation
validation also leaves no owned allocations. No visible manual GUI test or
GUI save against an installed service was performed.

See [B11 validation](../reviews/b11-validation.md) for the build matrix,
sanitizer results, tool versions and remaining coverage limits.
