# NSSM 022 Counted event-log insertions

| Field | Value |
| --- | --- |
| Priority and review | P2; finding 22, B09 |
| State | Released |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Reproduced against | `38224b13c24eff70e404fe85e1ed67d35f9e975b` |
| Fix PR | [PR 14](https://github.com/existical/nssm-native/pull/14) |
| Implementation commit | `d2c5ad91605af6ee98439cb8071cd11f9f429fea` |
| Merge commit | `0497d8a4d37881e89c3762c3957d1abf775a2190` |
| First release | [v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) (beta) |

Legacy `monitor_service` formatted its startup error into `code`, then passed
the integer `ret` to a variadic function expecting TCHAR pointers. Logging the
failure could dereference that integer. Earlier B03/B05 fixes supplied missing
sentinels for affinity and pipe errors, but other calls still relied on an
unchecked sentinel-based interface. The startup integer defect exists in the
recovered legacy source as well as the B09 baseline.

Startup now passes the formatted code. The event interface is a C++11 template
which constructs a counted string-pointer array and calls a bounded ReportEvent
adapter. Integer arguments fail compilation, no terminator is needed, and a
null string becomes an empty insertion rather than ending the list. The
adapter supports up to 16 insertions, including zero, and closes its event
source after reporting. Registration failure returns without reporting.

All 136 production call sites were audited against message insertions.
The two dynamic-ID call sites use three strings for registry millisecond
warnings and two for service-control events; their possible IDs were checked.
The settings delayed-start failure incorrectly selected a console format
message; it now selects the existing Event Log equivalent. Message IDs,
translations and UTF-16 resource files are otherwise unchanged.

`nssm.event_insertions` captures exact IDs, severity and insertion strings for
forced startup, affinity and pipe failures, plus zero/three/null insertions and
registration failure. It intercepts ReportEvent and never writes the system
Event Log. CMake also requires valid string calls to compile and an integer
insertion to fail compilation. These checks do not prove delivery if Windows
rejects an event or its size; the existing best-effort logging policy remains.
See [event API](../../src/event.h), [event tests](../../tests/error_messages.cpp),
[launch tests](../../tests/launch.cpp), [B09 validation](../reviews/b09-validation.md)
and the [ReportEvent reference](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reporteventw).
