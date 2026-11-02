# NSSM 012 Inheritable standard input

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 12, B09 |
| State | Fixed, unreleased |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| Fix PR | [PR 14](https://github.com/existical/nssm-native/pull/14) |
| Implementation commit | `d2c5ad91605af6ee98439cb8071cd11f9f429fea` |
| Merge commit | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| First release | Not yet released |

The legacy `get_output_handles` opened AppStdin without inheritable security
attributes. STARTF_USESTDHANDLES and CreateProcess's inheritance flag alone
do not make that file handle inheritable. A real child could not read its
configured input. The same call was still present at the B09 baseline.

The input file now opens with an inheritable SECURITY_ATTRIBUTES. Ownership
stays in STARTUPINFO: existing launch cleanup closes the parent's copy after
both successful and failed creation, and after setup/pre-start failures.
Sharing, disposition and file flags retain their configured values. This uses
the existing Windows handle-inheritance model, described in
[Microsoft's inheritance reference](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance).
It does not add an explicit process handle allowlist; concurrent launches
can still inherit other temporarily inheritable handles.

The initial regression observed 30 missing inheritance flags and 15 children
unable to read the expected bytes. The final `nssm.stdin` test uses a real
file and child, checks bytes and EOF, alternates successful creation with a
local invalid executable, closes parent copies, and checks stable handle
counts over 30 launches after warm-up. Missing-file and injected access-denied
paths are covered. Existing launch tests cover pre-start abort cleanup.
No real service is installed. See [tests](../../tests/logger.cpp) and
[B09 validation](../reviews/b09-validation.md).
