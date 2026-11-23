# Final review of Windows service editor saves

Two independent regression probes against the previous candidate reproduced configuration loss. After an administrator disabled a service and added a dependency, saving only its description from an older editor restored manual startup and cleared the dependency. In a separate test the same kind of save deleted a newly added environment override. Both saves reported success. The ordinary 37-test x64 Release suite passed on that candidate.

- [x] Compare the draft SCM fields with the editor's opening snapshot. Omit untouched startup, delayed start, service type, display name, account and dependencies from SCM writes. Keep explicit dependency clearing and installation working.
- [x] Compare editable registry settings with their opening values before staging writes or deletions. Preserve both environment blocks independently, numeric settings, affinity and the default exit action. Keep explicit resets and existing rollback behavior.
- [x] Add regression coverage for an unrelated GUI save after another writer changes configuration, including the explicit edit and reset paths. Update the service editor documentation.
- [x] Build and test x86 and x64 Debug and Release, audit the candidate package and run the Windows 11 live service, editor and upgrade checks against the packaged executable.
- [x] Review the final changes and record the verified results and remaining release conditions.

## Candidate evidence

The release packager built commit `fb5740cd8941927fee4efb175fcfa0121fce9c31` from a clean source archive. All 37 standard tests passed in each of x86 Debug, x86 Release, x64 Debug and x64 Release. The independent package audit verified the source identity, checksums, executables, symbols and test reports. The four build logs contained no reported compiler or linker warnings.

Windows 11 build 26300 ran the packaged x64 Release executable with SHA-256 `debf7f2d8dcd6c772dfe9d29817941458b8bf5b03ccb1865f221492a72829f0e`. All five elevated suites passed, covering export and replay, dependencies, service controls, the wrapped application lifecycle, and upgrade and rollback with a stateful Windows PowerShell worker.

The logged-in desktop test changed the executable, startup mode, dependency, environment and restart delay through the CLI while the service editor was open. A description-only save preserved all five changes. Reopening the editor and explicitly clearing the dependency and environment override removed both, applied a new display name and retained manual startup. The packaged editor also passed the rejected-save rollback and virtual-account/LocalSystem checks.

Ten additional consecutive lifecycle runs passed. The guest reported zero remaining fixture services, and the host removed the temporary VMware adapter address.

The review traced the GUI draft through the SCM and registry writes, explicit deletion, installation and rollback. Saving an untouched field now preserves a concurrent change. Two operators explicitly editing the same non-launch field can still replace one another's values. The three launch fields retain the conflict checks described in the [editor guide](../usage.md).

Acceptance covers Windows 11 and the repository's stateful PowerShell workload. A specific customer application or domain account would need its own deployment verification if included in the advertised support scope. Stable publication still requires the version tag, the tagged CI run and verification of its exact assets under the [release procedure](../releases/README.md).
