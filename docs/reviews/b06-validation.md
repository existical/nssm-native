# B06 offline rotation validation

Validation date: 2026 November 2. Scope: [NSSM-008](../fixes/NSSM-008.md).
The fix record tracks merge and release status.

The pre-fix commit was `b41b26e4c304b0dfd5202e1bb3fd99286766f178`, after
PR 10 merged. Its complete `rotate_file` function and the two rotation call
sites match `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`).

The initial regression created a 4-byte log and configured a 1 MiB threshold,
zero delay and copy/truncate mode. Through `get_output_handles`, both stdout
and stderr unexpectedly copied/truncated the file and requested
`Sleep(1048576)`. Eight assertions failed across the two streams: unexpected
rotation, unexpected sleep, changed original bytes and an unexpected archive.
The sleep wrapper recorded the argument without waiting. All eight checks
pass with the corrected implementation.

`nssm.rotation` exercises production output setup and rotation. It uses real
temporary files, CopyFile/MoveFile and truncation; a fixed clock and intercepted
Sleep make policy checks deterministic and fast. Large size words are injected
through GetFileInformationByHandle while the real file remains small. Failure
wrappers exist only in the regression executable.

| Case | Evidence |
| --- | --- |
| Original 4-byte / 1 MiB / zero-delay failure | Both streams remain unchanged; no archive or sleep |
| Disabled, below, exact and above size limit | Copy and rename modes, stdout and stderr; actual bytes checked |
| 32-bit boundary and nonzero high word | Sizes around 2^32, mixed low/high words, unsigned 64-bit maximum; metadata injected |
| Age policy | Too young, exact age, older, future timestamp and disabled age limit; maximum DWORD age also checked |
| Combined age/size limits | Both must pass; an old but undersized file stays untouched |
| Delay policy | Zero and nonzero copy/truncate delays match configuration exactly, including a large DWORD value; no real sleep |
| Rename mode | Never requests the copy/truncate delay |
| Copy/rename failure | Injected access denial; original bytes preserved, no success event or delay |
| Archive collision | Real pre-existing archive remains unchanged; original retained |
| Missing file / rotation disabled | No rotation event, copy, rename or sleep; output setup still succeeds |
| Distinct stdout/stderr | Both mode combinations tested; per-stream policy is retained |
| Shared output path | One rotation using stdout's policy |
| Handle stability | After initial copy/error paths are exercised, the remaining suite leaves the handle count unchanged |

An observed local x64 Release run had 106 handles before and after the
measured cases. Host counts can differ. Every fixture removes only its own
known original/archive files and temporary directory. No installed service is
created, configured or controlled, and no visible helper window is launched.
The test group has a 20-second CTest limit and does not perform long sleeps
or allocate multi-gigabyte files.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All fourteen CTest groups passed |
| x64 Debug | All fourteen CTest groups passed |
| x86 Release | All fourteen CTest groups passed |
| x86 Debug | All fourteen CTest groups passed |
| x64 AddressSanitizer | `nssm.rotation` and `nssm.logger` passed |
| MSVC `/analyze` Release | Build completed; retained diagnostics described below |

Analysis still reports the rotated-filename termination warning (C6054),
the timestamp return-value warning (C6001), and large I/O stack frames (C6262),
including `rotate_file`. They are not suppressed. This is not an
analyzer-clean claim. Metadata-query fallback and failures after a successful
copy remain outside this repair, as recorded in the fix's compatibility notes.
Online timestamp/byte-accounting repairs remain scheduled separately.

Use the [building instructions](../building.md) for the matrix. The targeted
sanitizer configuration is reproducible with:

```powershell
cmake -S . -B build/b06-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b06-asan --config Release --parallel
# Use a VS developer shell or add the toolset's x64 ASan DLL directory to PATH.
ctest --test-dir build/b06-asan -C Release -R '^nssm.(rotation|logger)$' --output-on-failure
```

The separate analysis build uses `-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`.
Production source retains CRLF, and message resources are unchanged.
