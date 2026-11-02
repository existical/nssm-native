# NSSM 032 Error-message buffer ownership

| Field | Value |
| --- | --- |
| Priority and review | P2; follow-up G, B08 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0ba9551903b5a47568bd317c5447fe4eed54d685` |
| Fix PR | [PR 13](https://github.com/existical/nssm-native/pull/13) |
| Implementation commit | `5baab9964b6e7080a1e98aab8dfba8cb82b0aa0b` |
| First release | Not yet released |

`error_string` allocated 65535 bytes but advertised 65535 TCHARs to formatting
functions. In Unicode builds those capacities differ by a factor of two.
The `message_string` fallback allocated 32 TCHARs with HeapAlloc, passed a
65535-character formatting capacity, and returned memory to callers that use
LocalFree. Allocation and formatting failure could also expose invalid memory
or lose the fallback allocation. Both legacy functions match the pre-fix code.

The error buffer now allocates `65535 * sizeof(TCHAR)` bytes and continues
passing a character count. A C++11 thread-local owner replaces the manually
allocated TLS slot: it works before CLI dispatch, reuses one lazily allocated
buffer per thread, and frees it when that thread returns. No TlsSetValue or
TLS-slot initialization failure can leave an untracked allocation. Allocation
failure returns a borrowed static diagnostic.

`message_string` lets FormatMessage allocate its actual required size with a
zero minimum. Its numeric fallback uses LocalAlloc for 32 TCHARs, passes that
capacity, checks allocation and frees on formatting failure. The ownership
contract is now explicit in [event.h](../../src/event.h):

| Function | Caller contract |
| --- | --- |
| error_string | Borrowed text; do not free; overwritten by the next call on that thread and invalid after thread exit |
| message_string | Owned result or NULL; free a successful result with LocalFree |

The allocation and size rules follow
[Microsoft's FormatMessage reference](https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-formatmessage).
Formatted text and locale fallback order remain unchanged. Normal returning
threads were tested; forced thread/process termination is not a destructor
guarantee. This uses the existing C++11/MSVC build requirement and makes no
claim of validation on legacy Windows versions.

The initial safe regression failed seven capacity/content/ownership checks.
It refused invalid advertised capacities and injected a 40000-character
message instead of overflowing an allocation. This establishes the API
capacity defect, not that a real installed system message has that length.
Final coverage includes locale and numeric fallback, allocation/format failure,
100 same-thread calls, distinct buffers in concurrent threads, reclamation
after 40 real returning workers, and real system/resource formatting.
See [message tests](../../tests/error_messages.cpp) and
[B08 validation](../reviews/b08-validation.md).
