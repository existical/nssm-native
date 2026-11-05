# NSSM 002 Command-quoting buffer bounds

| Field | Value |
| --- | --- |
| Priority and review | P1; finding 2 in the 2026 November 2 review |
| State | Released; merged in PR 7 |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a`, `v2.24-103-gdee49fc` |
| Reproduced against | `41c22afc60e276e3b4c36c8f6611098d29c9a3f6` with the new bounds regression |
| Evidence | Guarded destination tests, real cmd.exe child arguments, captured production dump output, and source inspection |
| Fix PR | [PR 7](https://github.com/existical/nssm-native/pull/7), branch `fix/b02-command-quoting` |
| Implementation commit | [1bd0b854ae8a774a501728234fbf400b427badaf](https://github.com/existical/nssm-native/commit/1bd0b854ae8a774a501728234fbf400b427badaf) |
| Merge commit | [a3c6260b23b1c229cb9c74a2b172655e41c30c55](https://github.com/existical/nssm-native/commit/a3c6260b23b1c229cb9c74a2b172655e41c30c55) |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

## Trigger and impact

`quote(L"&&&&", buffer, 9)` passed the old size check, then wrote 12 characters
and a NUL into a destination advertised as nine characters. Counting omitted
caret escapes added by the writer, including escapes around backslash runs and
quotes. Capacity zero also underflowed the initial subtraction.

`nssm dump` uses this helper with fixed buffers. A sufficiently long service
name, setting value, additional argument or executable name containing these
characters could overwrite memory during export. This is a source/runtime
finding, not an assertion of a demonstrated exploit.

The affected quoting functions are identical in the checked 2.24-103 source
and the pre-fix continuation. The v2.24 release and every historic binary were
not separately tested; see [comparing builds](../comparing-builds.md).

## Old and corrected behavior

[quote in nssm.cpp](../../src/nssm.cpp) now uses one traversal for both preflight
and writing. Every emitted character, including escapes and the final NUL,
consumes checked capacity. Backslash doubling no longer uses unchecked
multiplication. No new allocation is required.

Capacity is in `TCHAR` units including the terminator. Null arguments, zero
capacity, an unrepresentable byte capacity or insufficient space return 1
without changing the destination. Success returns 0 with NUL-terminated output.
Inputs must be terminated strings in separate, non-overlapping storage.
The helper retains its legacy empty-string result; dump callers explicitly
write `""` for an empty argument.

The four-ampersand example now fails at capacity 9 and succeeds at capacity 13.
Valid output retains the legacy escaping. The fixed dump buffers are not
enlarged: values whose escaped form does not fit now fail safely.

Caller audit: `setting_dump_string` quotes every argument before printing a
line. The service install line does the same. Setting-specific dump functions
propagate quoting errors to the command's nonzero exit status. Existing dump
streaming can still leave earlier complete lines before a later failure.
Discard all output if the command fails; export is not transactional.

## Regression validation

`nssm.quoting` uses [tests/quoting.cpp](../../tests/quoting.cpp) and
[tests/dump_quoting.cpp](../../tests/dump_quoting.cpp):

- The original overflow, zero/one/exact/one-short capacities, quotes, spaces,
  tabs, percent signs, carets, ampersands and backslash runs.
- All 7,381 strings of length zero through four over a nine-character alphabet,
  with every capacity through the required size plus one spare character.
- Long 32,767-character runs, unchanged failure buffers and guard preservation.
- Production dump failures for all four quoted fields, with no partial line
  emitted, plus successful quoted and empty values.
- Actual C runtime child arguments after cmd.exe parsing, including executable
  paths with spaces, final quoted arguments and empty arguments.

All seven CTest groups passed in x64/x86 Debug/Release. The targeted x64
AddressSanitizer check passed. MSVC analysis produced no diagnostics in the
changed quoting code; other legacy diagnostics remain. The
[B02 validation record](../reviews/b02-validation.md) preserves observations,
commands and scope. No service was installed or controlled.

## Compatibility and remaining limits

This repairs memory bounds while retaining the legacy shell representation.
No service configuration migration or restart is needed to use the corrected
dump command in a newly built executable.

Round trips use `cmd.exe /d /v:off /s /c`; batch files also run with delayed
expansion disabled. This prevents `!name!` expansion. Direct-command tests
include `100%` and `%NSSM_QUOTE_TEST%`, with no variable named
`NSSM_QUOTE_TEST^`. Those results do not establish that arbitrary percent text
is safe in every environment.

Saved batch files have a confirmed existing percent-sign limitation: the
legacy spelling of `100%` reaches parsing as `100^`, and a percent-delimited
variable spelling can collapse to a caret. B02 does not change those rules.
Newlines, nested `CALL`/block parsing, encoding conversions and command lines
past cmd.exe's own limit are not covered by the successful argument tests.
Further export work must define and test those cases explicitly.

The [Microsoft C argument rules](https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments)
describe the backslash/quote layer;
[cmd documentation](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/cmd)
describes shell options, special characters and environment expansion. Tests
exercise both layers rather than treating a Windows argument parser alone as
proof of shell correctness.
