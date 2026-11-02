# B03 affinity and numeric validation

Validation date: 2026 November 2. Scope:
[NSSM-004](../fixes/NSSM-004.md),
[NSSM-029](../fixes/NSSM-029.md) and
[NSSM-031](../fixes/NSSM-031.md).
The fix records track merge and release status.

The pre-fix source was `a3c6260b23b1c229cb9c74a2b172655e41c30c55`,
after B02 merged in PR 7. Comparisons with
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` confirmed identical
`str_number` conversion, `num_cpus` counting, affinity parser and formatter
functions. Source inspection established the GUI's dense-row assumption.

| Scenario | Pre-fix observation | Corrected result |
| --- | --- | --- |
| Affinity `0-64` | Accepted an endpoint outside the mask | Rejected, output mask zero |
| Reversed range `3-1` | Accepted an empty mask | Rejected |
| More than 64 tokens | Source showed unbounded writes to a 64-element stack array; not executed before repair | 65 and 130 repeated entries succeed without temporary-array storage |
| Empty, negative or overflowing numbers | Invalid values accepted; error paths could also overwrite output | Rejected, output unchanged |
| Sparse masks `5` and `10` | CPU count 1 and 0 instead of 2 | Both report 2 |
| Top-bit-only mask | CPU count 0 | CPU count 1 |
| Affinity API failure | Fabricated 64 CPUs | Zero mask/count |
| GUI list rows versus CPU IDs | Dense mapping source-traced | Real hidden list box maps item data correctly |

The first pre-fix numeric run failed 28 assertions and the affinity/count run
failed 7. These are repeated assertions, not counts of independent bugs.
The final suite adds cases unsafe to execute in the old parser, allocation
failure and GUI tests; those were not retroactively run on the unsafe source.

`nssm.affinity` checks 4,225 endpoint combinations, repeated and 64/65-token
lists, malformed delimiters, huge numbers, zero/high/all-bit round trips and
allocation failure in the formatter. The Windows list box is real but hidden.
It verifies sparse labels, item-data selection, all/none choices, native-width
masks and rejection of invalid item IDs. The full interactive service editor
and physical multi-group hardware were not exercised.

`nssm.numeric` checks both converter contracts and preservation of stored
values after errors. Production setters/getters use the existing isolated HKCU
fixture. AppExit maximum-code tests use decimal identifiers; legacy key-name
normalization remains outside this repair. The invalid-affinity logging
sentinel repair was inspected in source; a full startup/event-log integration
test was not performed.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All nine CTest groups passed |
| x64 Debug | All nine CTest groups passed |
| x86 Release | All nine CTest groups passed |
| x86 Debug | All nine CTest groups passed |
| x64 Release AddressSanitizer | `nssm.numeric` and `nssm.affinity` passed |
| MSVC `/analyze` Release | Completed; no diagnostics in the numeric/affinity helper implementations |

Existing analyzer findings in other code remain, including GUI hook state,
event buffers, handle ownership and large stack frames in GUI/service code.
No full-project analyzer-clean claim is made. Resources retain their original
encoding and production source retains CRLF.

Run the standard matrix using [building instructions](../building.md). For
the targeted sanitizer run:

```powershell
cmake -S . -B build/b03-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b03-asan --config Release --parallel
# Use a VS developer shell or add the selected toolset's x64 ASan DLL
# directory to PATH before running:
ctest --test-dir build/b03-asan -C Release -R 'nssm.(numeric|affinity)' --output-on-failure
```

Static analysis uses a separate build configured with
`-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`, then Release compilation.
ASan validates memory access, not every possible undefined integer operation;
bounded unsigned shifts are also checked by source inspection and boundary
tests. No real service was installed, edited, started or stopped.
