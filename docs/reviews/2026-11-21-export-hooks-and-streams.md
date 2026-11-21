# Review of service export, hook commands and log streams

Baseline `3482373` on `review/beta4-followup`. The review traced the CLI, SCM lifecycle, process and hook launch, environment restoration, registry and GUI settings, logging and rotation, and the associated test and packaging entry points.

- [x] **Keep wrapper-owned log handles private.** Direct-file output and shared stderr produce inheritable service-owned handle copies. Retain private handles for the wrapper and give the child inheritable standard handles. Keep the application's prepared standard handles private during Start/Pre. Check separate and shared files, logging pipes, direct-file fallback and the inheritance flags at both launch points.
- [x] **Quote the executable chosen in the hook browser.** A hook field holds a complete command line. Quote a selected filename before setting the field, preserve the existing command on cancellation or overflow, and test the resulting saved command. [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
- [x] **Reject dump commands that exceed the shell limit.** Measure each complete escaped install or set line before writing it. Report an error for overlong lines and test the 8,191-character boundary, including command prefixes and optional arguments. [Windows command prompt limits](https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/command-line-string-limitation)
- [x] **Validate service keys at creation and clone export.** The existing length check allows slash and backslash in a clone target, yielding a script that cannot install it. Validate new keys before generating output or creating a service, and preserve display-name lookup for existing services. [CreateServiceW](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew)
- [x] Review the final diff and verify the Windows build matrix, static analysis, x64 AddressSanitizer and candidate package.
- [x] Run the five elevated Windows 11 service and upgrade suites on the candidate.

## Verification

Implementation commit `06279bf` was built from a clean archive.

| Check | Result |
| --- | --- |
| Before and after | The new assertions in quoting, launch, logger, install ownership and GUI hooks failed against baseline `3482373` and passed on the implementation. The GUI baseline fixture uses the original file-picker helper signature. |
| Windows builds | All 37 tests passed in each x86 and x64 Debug and Release build. The four build logs contain zero compiler warnings. |
| Static and runtime checks | MSVC production code analysis reported zero warnings for x86 and x64. All 37 x64 Release AddressSanitizer tests passed. |
| Candidate package | Checksums, all four executable and symbol packages, test reports and source identity verified. |
| Windows 11 VM (build 26300, x64 Release) | Export, dependency, service-control, wrapped-service lifecycle and legacy upgrade suites all passed. Three additional lifecycle runs passed. The guest had zero new fixture services after cleanup. |

The guest verified the candidate executable's SHA-256 as `31c53b761161a9bd7b55f4393fc923df081656fc60bb2445dab1d8d872f7646e`. After testing, the host removed the temporary IPv4 address and recovered its original VMnet1 address.

The initial suspicion concerning a named NTFS stream called `$DATA` was withdrawn after checking that the existing comparison already distinguishes that name from the default stream. The confirmed logging defect concerns handle inheritance.
