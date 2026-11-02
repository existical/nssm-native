# NSSM 025 GUI password-confirmation cleanup

| Field | Value |
| --- | --- |
| Priority and review | P2; B11 |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `f28a43141a576033713190fe9b716fb503ac227a` |
| Fix PR | [PR 16](https://github.com/existical/nssm-native/pull/16) |
| Implementation commit | `b91392150c23ecec4299c4eec75a7fcb65f867fb` |
| Merge commit | `f969dd985039a8d2fe77a0ed0a24e077894c2bef` |
| First release | Not yet released |

After two matching GUI passwords were read, the temporary confirmation
allocation was neither wiped nor freed. Error paths handled that allocation
inconsistently. The legacy source contains the same successful-path omission.
The initial `nssm.gui_password` test found one outstanding allocation after
configuration and service-object cleanup.

A scoped owner captures the confirmation allocation size and securely zeros
the entire region before freeing it, including success, mismatch, allocation
failure and read failure. The service password remains independently owned
and is wiped by its existing cleanup. Replacing a typed account with its
LocalService/NetworkService canonical name also releases the typed-name buffer
before assigning another allocation.

[Tests](../../tests/install_gui.cpp) exercise matching and mismatching values,
unequal lengths, missing password, both dialog-read failures, each credential
allocation failure, canonical-account replacement and replacement-allocation
failure. A test allocator checks every byte of both password allocations at
free and checks for retained allocations after every attempt. No real passwords
or credentials are used. This repairs local heap retention; it does not claim
a demonstrated disclosure to another user/process or zeroing of Windows edit
control internals. Accepted password/account syntax is unchanged.

See [B11 validation](../reviews/b11-validation.md) for the build matrix,
sanitizer results, tool versions and remaining coverage limits.
