# NSSM 029 Sparse CPU masks and affinity choices

| Field | Value |
| --- | --- |
| Priority and review | P2; follow-up D in the 2026 November 2 review |
| State | In progress; implementation present, merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `a3c6260b23b1c229cb9c74a2b172655e41c30c55` |
| Evidence | Injected API masks and failures; actual hidden Windows list-box checks; GUI caller inspection |
| Fix PR | [PR 8](https://github.com/existical/nssm-native/pull/8), branch `fix/b03-affinity-parsing` |
| Implementation commit | Pending merge |
| First release | Not yet released |

The old `num_cpus` counted consecutive low bits, so a system mask of `0b101`
reported one CPU instead of two, and `0b1010` reported none. Its full-width
loop checked the shift boundary after evaluating the shift. API failure
invented 64 CPUs, including in the x86 build.

[nssm.cpp](../../src/nssm.cpp) now exposes the native-width system affinity
mask, returns zero when the API fails, and counts set bits by shifting that
unsigned mask right. No shift can reach the type width.

The [GUI](../../src/gui.cpp) now adds only IDs present in that system mask.
Each list item stores its CPU ID separately, and loading/saving selections
uses that ID rather than the row number. For mask `0b101` the choices are
`0` and `2`. Invalid list data is rejected before shifting; an empty list
does not fabricate choices or shrink the control using a negative row count.
All rows selected retains the existing unrestricted-mask convention.

When the API fails or returns a zero mask, no specific IDs are listed. The
`All` option is available; trying to save a specific affinity without any
selected CPU fails validation. Sparse-mask support does not mean the process
can choose CPUs in arbitrary processor groups.

`nssm.affinity` injects zero, sparse, top-bit and full masks, including API
failure with nonzero output values that must be discarded. x86 tests cover
32-bit masks; x64 tests cover bit 63. A real hidden Windows list box checks
labels, item data, sparse selection, all/none selection and invalid IDs.
The control is destroyed afterwards. These are not tests on physical
multi-group hardware or of a complete interactive service-edit session.
See [tests/nssm_entry.cpp](../../tests/nssm_entry.cpp),
[tests/parsing.cpp](../../tests/parsing.cpp) and
[B03 validation](../reviews/b03-validation.md).

The system-mask API remains the source of available IDs, within the scope
defined by [GetProcessAffinityMask](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessaffinitymask).
This preserves the current/primary-group model and native pointer width.
The stored affinity syntax remains 64-bit, including on x86; the launch code
intersects it with the child's system mask. Cross-group selection and its
hardware validation remain future work.

No migration is needed for valid configurations. The UI repair applies when
editing with the new executable; a changed affinity applies on the next
application launch. The pre-fix counting function matches the checked legacy
source; no claim is made about every historic binary.
