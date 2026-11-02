# NSSM 026 Account lookup ownership and outputs

| Field | Value |
| --- | --- |
| Priority and review | P2; B11 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f28a43141a576033713190fe9b716fb503ac227a` |
| Fix PR | Pending creation |
| Implementation commit | `b91392150c23ecec4299c4eec75a7fcb65f867fb` |
| First release | Not yet released |

`canonicalise_username` leaked its policy handle and heap SID, and leaked
its intermediate conversion buffer on conversion failure. Adjacent SID lookup
paths used uninitialized LSA outputs and some returned a freed/non-null SID.
Callers used FreeSid for SIDs allocated with HeapAlloc. The relevant legacy
functions contain those patterns. Seven initial `nssm.account_lookup` checks
failed on successful lookup and policy/name/SID/conversion failures.

Small scoped owners now pair LsaClose, LsaFreeMemory and HeapFree with the
resources they own. A caller-supplied policy stays borrowed. SID and canonical
name outputs are null on failure and transferred only on success. SID-consuming
helpers use HeapFree. Lookup-only calls request POLICY_LOOKUP_NAMES; the
right-granting path retains its previous policy access. No account rights are
changed by canonical-name lookup.

Lookup checks computer-name failure, translated-domain indices, null results,
SID validity/subauthority capacity and counted name metadata before use. UTF-16
conversion writes its length to an unsigned long, then checks and narrows it;
it no longer writes through a cast into a USHORT field. Combined name sizes
use size_t instead of a wrapping USHORT. Missing, malformed and oversized
inputs return failure instead of exposing partial results. Valid local and
virtual-account lookup paths retain their legacy LSA APIs.

[Account tests](../../tests/accounts.cpp) inject policy, name/SID, conversion,
computer-name, SID-initialization and allocation failures; check borrowed
policy ownership, invalid indices/lengths, rejected groups and a combined name
over 65535 bytes; and verify balanced resources. They repeat a real read-only
lookup of the current local account 30 times after LSA/RPC warm-up and check
handle stability. Domain results are simulated; no live domain account or
policy mutation was tested. These ownership rules follow Microsoft's
[LsaLookupSids](https://learn.microsoft.com/en-us/windows/win32/api/ntsecapi/nf-ntsecapi-lsalookupsids)
and [FreeSid](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-freesid)
contracts.

See [B11 validation](../reviews/b11-validation.md) for the build matrix,
sanitizer results, tool versions and remaining coverage limits.
