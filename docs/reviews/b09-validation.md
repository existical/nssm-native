# B09 redirection and logging validation

Validation date: 2026 November 2. Scope: NSSM-012, NSSM-013, NSSM-014,
NSSM-022. Implementation/merge/release state is tracked in the
[fix index](../fixes/README.md).

The baseline is `38224b13c24eff70e404fe85e1ed67d35f9e975b`, after PR 13 merged.
Relevant functions were also inspected at recovered legacy commit
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`).
The missing stdin inheritance, invalid UTF-16 buffer address, byte-level newline
scan, accounting/metadata assignments and startup integer insertion are present
there. B05's worker ownership changes are retained. No historic binary was run.

| Fix | Pre-fix evidence | Final coverage |
| --- | --- | --- |
| [012](../fixes/NSSM-012.md) | 30 missing inheritance flags and 15 real children failing to read expected input | Bytes and EOF through real inherited stdin, missing/access-denied input, successful/failed launches, parent cleanup and 30-launch handle counts |
| [013](../fixes/NSSM-013.md) | Guarded WriteFile rejected the dereferenced UTF-16 value as an address; no timestamp bytes written | Exact ASCII/UTF-8 and UTF-16LE bytes, LF/CRLF, empty lines, 1–20-byte reads, split code units/surrogates, U+010A, BOM/append/odd EOF and rotation |
| [014](../fixes/NSSM-014.md) | Empty timestamped writes did not return a defined zero-byte result; source showed accumulated counts and metadata clearing policy | Short/zero/failed writes, failure at each byte offset 0–64, actual byte totals, metadata fallback/failure, thresholds around complete lines, unsigned high word, manual/offline modes, copy/truncate and failed rename/truncate |
| [022](../fixes/NSSM-022.md) | Source passes integer `ret` instead of formatted `code`; resource audit found a console ID used for an event | Exact captured startup/affinity/pipe IDs and strings, zero/null insertions, registration failure, positive and negative compilation checks |

The invalid pointer test rejects low addresses before touching them. Initial
empty-write return values are not treated as stable numeric results: the old
function returned an uninitialized variable. Two initial harness assertions
were unrelated to product defects: conversion allocation accounting crossed
translation-unit wrappers, and a nonexistent drive path used for failed launches
caused external OS handle activity. Final timestamp conversion uses no allocation;
launch failures use a real local non-executable fixture file after warm-up.

The stream harness runs the actual worker synchronously with deterministic
pipe chunks, clock and failures, and writes real temporary files. It compares
both current and archived files byte for byte. Real concurrent pipes, child-held
writers, cancellation and restart ownership remain covered by `nssm.logger`.
Read failures retain a nonzero worker result; ordinary EOF is successful.
Event Log API calls are intercepted. No installed service is created or controlled.

The audit covers 136 production event calls. Constant-ID calls match resource
insertion counts; dynamic registry warning IDs use three insertions and dynamic
service-control IDs use two. CMake requires valid string calls to compile and
an integer insertion to fail with a pointer-conversion diagnostic. No event
resource or translation was rewritten.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All 25 CTest groups passed |
| x64 Debug | All 25 CTest groups passed |
| x86 Release | All 25 CTest groups passed |
| x86 Debug | All 25 CTest groups passed |
| x64 AddressSanitizer | stdin, log_bytes, event_insertions, error_messages, launch, hook_live, logger and rotation passed |
| MSVC `/analyze` Release | Build completed; remaining diagnostics retained |

The old timestamp result's C6001 and rotated-path C6054 are absent.
The counted event adapter has no uninitialized-insertion diagnostic. Existing large-stack warnings,
GUI/account findings, ignored service API results, hook tracking diagnostics,
AppExit warnings and registry MULTI_SZ analysis diagnostics remain. This is not
an analyzer-clean claim. B09 groups use the standard 20-second CTest timeout.

Reproduce the full matrix using `scripts/build.ps1 -Architecture x64|x86
-Configuration Release|Debug`. For targeted sanitizer checks:

```powershell
cmake -S . -B build/b09-asan -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' `
  '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b09-asan --config Release --parallel
# Make the matching MSVC ASan runtime directory available on PATH.
ctest --test-dir build/b09-asan -C Release `
  -R 'nssm.(stdin|log_bytes|event_insertions|error_messages|launch|hook_live|logger|rotation)$' `
  --output-on-failure
```

Compatibility limits: encoding detection without a UTF-16LE BOM is heuristic;
malformed final input is preserved, not repaired; existing log encoding must
match the stream; rotation waits for LF; forced shutdown may discard buffered
bytes; unknown initial size can delay rotation; concurrent external writers
and offline metadata/post-copy failures are not resolved by B09. Real SCM,
interactive GUI, account and sustained production-load acceptance remain
release prerequisites. These tests do not certify historic Windows support.
