# Stable release review, 23 November 2026

**Hold v2.25.0.** The public beta.5 tag and remote `master` both resolve to `5534e26ab7e6aadcf97464980f6b1603ce3893a3`. Two configuration defects can change which application runs or whether the service can start. A third defect makes a named environment query disagree with the child process. The repository had no open GitHub issues or pull requests at review time.

## 1. High — A failed service edit can leave the service disabled

`edit_service()` writes the startup type, account, dependencies and display name to the Service Control Manager (SCM) before saving the description and NSSM registry parameters. If a later write fails, the caller receives an error and the earlier SCM changes persist. An operator who cancels after the error can discover the disabled service at the next restart. Beta.5 covers invalid dependencies and rejection of the first SCM update. A failure after that update follows a different path. [Edit sequence](../../src/service.cpp#L1488) · [GUI failure handling](../../src/gui.cpp#L1074)

**Observed.** A test double initialized an existing service to manual startup with its original dependency and display name. It accepted the SCM writes and then rejected the parameter save. The production edit routine returned 6. The fixture still recorded `SERVICE_DISABLED`, display name `Changed`, and dependency `ServiceA`. This was a fault-injection test with a simulated SCM.

**Release check.** Make fallible parameter and description changes recoverable and order the writes so that an irreversible account change cannot be followed by an ordinary configuration failure. Account for the separate GUI hook save. Where rollback itself fails, identify the settings already applied. Inject failures after each write and query the resulting configuration, including startup, on a disposable Windows service.

## 2. High — Saving an unrelated GUI field can restore an obsolete executable

The editor reads the application path from its original dialog contents. On save, the registry code compares that stale path with the current registry value and treats the difference as a requested change. An administrator can deploy a replacement executable while the dialog is open, then lose that update when another operator saves the description. The next service start uses the old path or fails if the old binary has been removed. [Reading the application field](../../src/gui.cpp#L613) · [Writing launch parameters](../../src/registry.cpp#L116)

**Observed.** A hidden Windows editor displayed `C:\fixture.exe`. The fixture edited the description, then wrote `C:\replacement.exe` to its isolated registry before the dialog was saved. The production GUI collection and registry save calls both succeeded. Reading back `Application` returned `C:\fixture.exe`.

**Release check.** Preserve untouched launch fields. When a field that the operator actually edited has also changed in storage since the dialog opened, surface the conflict. Exercise both cases with two writers and check the final registry values and the executable selected on the next launch.

## 3. Medium — Non-ASCII environment keys can return the wrong value

The named getter uses the C runtime's case-insensitive comparison. Under the executable's default locale, it can treat two spellings of a key as distinct when Windows treats them as the same environment variable. The duplicate-key detector uses the same comparison. [Named lookup](../../src/settings.cpp#L564) · [Duplicate detection](../../src/settings.cpp#L595)

**Observed.** With `NSSM_Ä=first` followed by `NSSM_ä=last`, the production getter for `NSSM_Ä` returned `first`. After applying the same block, Windows `GetEnvironmentVariableW` returned `last` for that spelling.

**Release check.** Use consistent Windows-compatible case comparison across named lookup, append, removal and duplicate detection. Test both spellings through the public CLI and verify the stored assignments and the child environment after dump and replay.

## Verification and stable-release acceptance

| Check | Result |
| --- | --- |
| Tagged Windows CI | All five jobs passed. The packaged binaries passed 37 standard tests per configuration and five extra live x64 Release suites, including upgrade and rollback. [CI run](https://github.com/existical/nssm-native/actions/runs/44965954460) |
| Fresh baseline | Rebuilt the unmodified x64 Release checkout and ran all 37 standard tests successfully. |
| Package verification | Rechecked the retained beta.5 CI package. All checksums, executable and symbol packages, test reports, and source identity passed. |
| Additional review cases | Built the tagged production source with additional test fixtures in an isolated directory. Three new assertions failed as described above. A real anonymous-pipe check delivered all six bytes written after an empty write. |
| Stable acceptance | The release guide calls for representative applications, interactive GUI and account configuration, and the Windows versions advertised for stable support. The reviewed records contain automated Server 2022 and prior Windows 11 fixture results. I found no completed stable-candidate acceptance record covering all three requirements. [Release guide](../releases/README.md#L90) · [Windows 11 evidence](2026-11-21-startup-environment-console.md) |

One documentation correction can accompany the fixes. The upgrade guide still says the packaged x64 Release suite runs 41 groups and omits the legacy executable for testing. The packager builds the legacy executable separately and the beta.5 package records 42 passing groups. [Upgrade guide](../releases/upgrade-rehearsal.md#L63) · [Packager](../../scripts/package.ps1#L64)
