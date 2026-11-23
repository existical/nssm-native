# Final review of Windows service editor saves

Two independent regression probes against the previous candidate reproduced configuration loss. After an administrator disabled a service and added a dependency, saving only its description from an older editor restored manual startup and cleared the dependency. In a separate test the same kind of save deleted a newly added environment override. Both saves reported success. The ordinary 37-test x64 Release suite passed on that candidate.

- [ ] Compare the draft SCM fields with the editor's opening snapshot. Omit untouched startup, delayed start, service type, display name, account and dependencies from SCM writes. Keep explicit dependency clearing and installation working.
- [ ] Compare editable registry settings with their opening values before staging writes or deletions. Preserve both environment blocks independently, numeric settings, affinity and the default exit action. Keep explicit resets and existing rollback behavior.
- [ ] Add regression coverage for an unrelated GUI save after another writer changes configuration, including the explicit edit and reset paths. Update the service editor documentation.
- [ ] Build and test x86 and x64 Debug and Release, audit the candidate package and run the Windows 11 live service, editor and upgrade checks against the packaged executable.
- [ ] Review the final changes and record the verified results and remaining release conditions.
