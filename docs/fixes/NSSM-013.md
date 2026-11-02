# NSSM 013 UTF-16 timestamps and line boundaries

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 13, B09 |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| Fix PR | [PR 14](https://github.com/existical/nssm-native/pull/14) |
| Implementation commit | `d2c5ad91605af6ee98439cb8071cd11f9f429fea` |
| Merge commit | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| First release | Not yet released |

Legacy `write_timestamp` dereferenced the converted UTF-16 pointer and passed
the resulting character value as a buffer address. Its line scanners searched
individual bytes for LF, so they could split a UTF-16 newline or mistake the
low byte of another character for LF. The relevant legacy source matches the
B09 baseline. The initial guarded test rejected that invalid WriteFile address.

Timestamps now use a fixed stack buffer containing the ASCII timestamp widened
to UTF-16LE. The worker keeps incomplete code units between reads and recognizes
LF as a complete code unit. Timestamping applies to every line, including the
line ending at rotation. Timestamp format and UTC clock choice are unchanged.

A leading UTF-16LE BOM selects the encoding even when split across reads. It
is consumed as an encoding marker; known-empty UTF-16 output files receive one
BOM, including new files after rotation. Appending to a nonempty file does not
insert another BOM. Without a BOM, the legacy IsTextUnicode heuristic remains,
with at least four probe bytes unless EOF arrives sooner. A probe can delay up
to three initial bytes; thereafter at most one incomplete UTF-16 byte is held.
[Microsoft documents that this detection is heuristic](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-istextunicode).
Use a UTF-16LE BOM for unambiguous detection. UTF-16BE and encoding changes
within a stream are unsupported. Ordinary byte/UTF-8 streams remain byte-oriented;
UTF-8 BOM bytes are not treated as a separate encoding marker. Existing output
encoding must match the child stream; append does not transcode old contents.

At EOF an incomplete final byte is preserved verbatim, without interpreting it
as LF or inventing a replacement character. Malformed input can therefore
remain malformed; valid input stays decodable. If the existing file size cannot
be established, NSSM avoids inserting a possible mid-file BOM.

`nssm.log_bytes` compares exact bytes for ASCII/UTF-8 and UTF-16LE, LF/CRLF,
empty and multiple lines, 1–20-byte read fragments, split surrogate pairs,
U+010A, BOM-only/empty input, appends, odd EOF, and timestamps across rotation.
See [byte tests](../../tests/log_bytes_cases.h), [implementation](../../src/io.cpp)
and [B09 validation](../reviews/b09-validation.md).
