# B02 command-quoting validation

Validation date: 2026 November 2. Scope:
[NSSM-002](../fixes/NSSM-002.md), command-quoting bounds and caller error handling.
Implementation/release status is recorded in the fix record.

The pre-fix production source was
`41c22afc60e276e3b4c36c8f6611098d29c9a3f6`. Its `quote`, `needs_quote` and
`needs_escape` functions match
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a:nssm.cpp`.

| Scenario | Pre-fix observation | Corrected result |
| --- | --- | --- |
| Four ampersands, capacity 9 | Success returned; 13 characters written, including NUL | Failure; destination and guards unchanged |
| Capacity zero | Subtraction underflow allowed writes | Failure without writes |
| Quotes, escapes and backslash runs near capacity | Preflight accepted outputs that crossed the logical boundary | Only fully fitting, terminated results succeed |
| Dump field exceeds its escaped buffer | Source paths used the unsafe helper | Tested helper returns nonzero before printing any part of the line |

The initial bounds-only test run failed 58,058 assertions on the old code,
including repeated checks across generated strings and capacities. These are
repeated manifestations of the same defect, not 58,058 distinct findings.
The backing arrays deliberately had enough space for legacy writes; logical
capacity and guard checks observed the overwrite without corrupting the test
process's heap. Later null-input and real dump-caller checks were added after
the fix, and were not executed against the unsafe implementation.

The committed [bounds and shell tests](../../tests/quoting.cpp) check 7,381
generated short strings plus explicit and long cases.
[Dump tests](../../tests/dump_quoting.cpp) compile the production settings
implementation with test-only output capture and executable-name selection.
All four quoting failures are checked before output is emitted.
The install-line and aggregate exit-status paths were inspected in source;
there was no live SCM export/import test.

Real process tests copy the regression executable to a path containing spaces.
They invoke cmd.exe with `/d /v:off /s /c` and compare actual child `argv`.
A second launch runs an ASCII batch file with its executable path supplied by
the inherited Unicode environment. Tests cover spaces, tabs, quotes,
backslashes, ampersands, carets, redirection/pipe characters, asterisks,
literal exclamation marks, and empty/final arguments. Direct invocation also
tests selected percent strings; batch invocation excludes them because of
the confirmed legacy limitation described below. Temporary files are removed.
No administrator rights or service operations are required.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All seven CTest groups passed |
| x64 Debug | All seven CTest groups passed |
| x86 Release | All seven CTest groups passed |
| x86 Debug | All seven CTest groups passed |
| x64 Release with AddressSanitizer | `nssm.quoting` passed |
| MSVC `/analyze` Release | Build completed; no diagnostics in the changed quoting code |

The full analyzer run still reports existing findings elsewhere, including
registry formatting, handle ownership, event buffers and large stack frames.
This is not a claim that the full project is analyzer-clean.

Run the matrix with [the standard build commands](../building.md). A separate
sanitizer build can be reproduced with:

```powershell
cmake -S . -B build/b02-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b02-asan --config Release --parallel
# Use a VS developer shell or put the selected toolset's x64 ASan DLL
# directory on PATH before running:
ctest --test-dir build/b02-asan -C Release -R nssm.quoting --output-on-failure
```

For static analysis use a separate build configured with
`-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`, then build Release.

## Existing shell limitation observed

A separate ASCII batch probe, run with `cmd.exe /d /v:off`, printed these
results using the same legacy argument spellings:

```text
Command: echo single-percent: ^"100^%^"
Output:  single-percent: "100^"
Command: echo variable-form: ^"^%NSSM_QUOTE_TEST^%^"
Output:  variable-form: "^"
```

`NSSM_QUOTE_TEST` was set to `expanded` and `NSSM_QUOTE_TEST^` was absent.
This is a shell-parsing observation, not a passing batch round trip.
The unchanged legacy percent rules need separate export design/testing.
The bounds test still covers percent input. Delayed expansion, nested shell
contexts and embedded newlines are not covered by the successful round trips.
