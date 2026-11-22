# Review of startup settings, environment export and console output

The baseline was `032a954` with the five fixes from the preceding review in the working tree. The review covered service control and startup, the CLI and GUI configuration paths, process and hook execution, registry and environment handling, stream output, and the build and test entry points. All 37 x64 Release tests passed before this implementation.

## Findings and implementation plan

- [x] **High — A rejected delayed startup request can enable a disabled service.** A fixture recorded the successful switch to automatic before the delayed-start write failed. Share one startup update path between the CLI and editor. Read the existing delayed flag, prepare the requested flag before changing the base configuration, and restore it if the base update fails. Preserve compatibility with managers that lack delayed startup. Test both failure orders and verify the setting on a real disposable service. [Windows delayed-start contract](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_delayed_auto_start_info)
- [x] **Medium — Driver startup queries report the wrong mode.** Windows reported boot start for the disk and TCP/IP drivers, while NSSM returned automatic with exit code zero. Handle boot and system modes in the read-only CLI path, keep them out of the service editor, and test both against SCM configuration.
- [x] **Medium — Duplicate environment keys change meaning across a dump and replay.** The named getter returned the first assignment while the child received the last. Replaying the emitted operations also changed an intermediate variable from `first` to `last`. Preserve assignment order and duplicates in export, honor the command length limit, and return the last assignment on a named query. Test case variants and a dependent assignment.
- [x] **Medium — Overlapping CLI commands leave the console on the wrong code page.** Two real NSSM listings on a private console starting at code page 437 left it at 65001. Preserve Unicode console and UTF-8 redirected output without changing the shared console setting. Repeat the overlapping-process check and validate non-ASCII output.
- [x] Run the four Windows configurations, focused regressions, static analysis, sanitizer and disposable Windows service checks. Record the results and review the final changes.

## Verification

| Check | Result |
| --- | --- |
| Startup edits | Fault-injection tests checked a rejected delayed flag, an unsupported information level, a rejected base configuration after the flag changed, and a successful update. Windows 11 accepted a disabled-to-delayed transition through the CLI, returned the delayed mode on query and accepted the reset to disabled. |
| Driver startup queries | On the workstation, both `sc.exe qc disk` and NSSM reported boot start. Both reported system start for `AFD`. The regression suite exercises both read-only modes. |
| Environment replay | The live test replayed a block containing spaces, an ampersand and a repeated key with different casing through `cmd.exe`. The restored registry type, length and bytes matched the source. A local regression checked that an oversized duplicate-key command fails without emitting the command. |
| Console output | Two overlapping NSSM processes left the private console at code page 437 throughout. The non-ASCII diagnostic survived both a native console-buffer read and UTF-8 decoding of redirected stderr. |
| Build matrix and analysis | All 37 tests passed in each of x86 Debug, x86 Release, x64 Debug and x64 Release. Both production MSVC analysis builds reported zero warnings. All 37 x64 Release AddressSanitizer tests passed. |
| Elevated Windows 11 | All five suites passed, including the legacy upgrade. Three additional wrapped-service lifecycle runs passed. The guest reported zero new fixture services remaining. The tested x64 binary had SHA-256 `cd5420b9f0ab084b402cebe99b3b81f03946efba24a4ec9c18a5fd27ce71aed1`. |

Windows exposes the base startup configuration and delayed flag through separate calls. If the SCM also refuses the compensating write, NSSM records that failure in the event log. Duplicate-key environment exports use one command and therefore share the existing 8,191-character shell limit.
