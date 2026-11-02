# NSSM 030 Read-only configuration export

| Field | Value |
| --- | --- |
| Priority and review | P2; B10 |
| State | In progress; merge pending |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| Fix PR | [PR 15](https://github.com/existical/nssm-native/pull/15) |
| Implementation commit | `fc029e716391d3aa9ff3399cd388b428da8e4561` |
| First release | Not yet released |

MODE_DUMPING previously requested SERVICE_CHANGE_CONFIG in addition to
SERVICE_QUERY_CONFIG. A caller allowed to inspect a service but not alter it
could not export it. The same access expression exists in recovered legacy
source and the B10 baseline; the pre-fix access-mask regression failed once.

Dump and get now request SERVICE_QUERY_CONFIG. Set, reset and GUI edit still
request SERVICE_CHANGE_CONFIG as well. SCM connection/enumeration access is
retained for the existing service/display-name lookup, and registry reads
use KEY_READ or KEY_QUERY_VALUE. No export path creates or sets registry
values. CLI cleanup owns the allocated service object and the dump key,
including early failures; failed allocation returns before field access.

For a canonical service name, service access can be limited to
SERVICE_QUERY_CONFIG. The SCM must permit SC_MANAGER_CONNECT and
SC_MANAGER_ENUMERATE_SERVICE, and relevant registry keys must permit the
requested read access. Display-name fallback uses enumeration, which can hide
services lacking SERVICE_QUERY_STATUS; use the canonical name for a strictly
query-configuration-only service. These distinctions follow
[Microsoft's service access reference](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).

`nssm.export_access` captures SCM/service masks for dump, get, set, reset and
edit, plus allocation/early-return cleanup. The opt-in
[SCM integration test](../../tests/export_live.cpp) creates two unique disabled
fixtures on an elevated disposable Windows runner. It denies change access
even to its administrator caller while granting configuration queries, exports
the source, verifies a setter is denied without modifying its input, replays
only AppExit set commands into the writable fixture, and checks the rules.
Neither service is started. Both are deleted with already-owned handles.
Ordinary local tests never create services. See [B10 validation](../reviews/b10-validation.md)
for actual run status and [building](../building.md) for explicit opt-in.
