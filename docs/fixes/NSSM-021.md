# NSSM 021 Install-object ownership

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 21, B08 |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `909fba2ce8616763b8daaabda45ec55b1f5193f7` |
| Merge commit | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

When opening SCM failed, `install_service` destroyed the service object even
though the CLI and GUI callers each destroyed it after the function returned.
Both functions match the checked legacy source. The initial regression
observed duplicate destruction through both actual callers, producing four
failed assertions with service allocations quarantined to avoid a real
use-after-free.

`install_service` now borrows its service object on every return path. It
closes its own SCM manager handle and leaves destruction of the object and
its created service handle to the caller. The existing rollback attempt after
configuration failure remains. CLI allocation is checked before any field
access, and rejecting oversized arguments releases the allocated object.

Return codes remain 1 for an absent object/allocation failure, 2 for SCM-open
failure, 5 for CreateService failure, 6 for configuration failure, and 0 for
success. These are existing distinctions; no real service is created by the
regressions.

Tests execute `pre_install_service` and `install(HWND)` using fake SCM APIs.
The latter uses the actual resource dialog and valid controls while its parent
window stays hidden and popups are intercepted. Both callers cover SCM-open,
creation, configuration and parameter failures, plus success. Assertions count
object destruction, manager/service handle closes and rollback calls. CLI
allocation failure and oversized arguments are also covered.

Interactive configuration validation, edit-dialog borrowed handles and account
selection remain B11 work. See [install fixtures](../../tests/install.cpp),
[hidden GUI coverage](../../tests/install_gui.cpp) and
[B08 validation](../reviews/b08-validation.md).
