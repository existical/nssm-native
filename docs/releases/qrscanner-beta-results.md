# QRScanner acceptance of NSSM Native 2.25.0-beta.1

**Passed within the Windows 11 x64, LocalSystem test scope.** The 24-hour
run completed on **4 November 2026 at 16:38 Riga time**. This sanitized record
incorporates the supplied QRScanner report. The NSSM documentation follow-up
checked candidate hashes and receipt consistency locally; it did not rerun the
VM trial. Original application and operational receipts remain local and are
not included in Git.

**QRScanner production adoption remains separate.** Its unmodified native stop
helper pins the legacy executable hash and rejects this beta. The trial checked
that refusal and then used a test-only helper copy admitting the known legacy
and beta hashes, with the other ownership/integrity checks retained. Production
helper sources, bundled wrapper, installer and admission policy were unchanged.
A separately reviewed helper/provenance compatibility change is required before
that installed QRScanner package can adopt the beta.

## Tested candidate

| Item | Identity |
| --- | --- |
| Version | `NSSM 2.25.0-beta.1 64-bit 2026-11-03` |
| Source/tag | `739b5feabdc768a4524fe28bad2c000b05ee5e07`, `v2.25.0-beta.1` |
| Executable SHA-256 | `80c08adcae80cfc7e1e6128d5e1c5a8e4aa9d4cedfa7ad5020a29af03488933d` |
| x64 Release ZIP SHA-256 | `a835731a012d2ac3b6310a4851f6519ff25496efb3fc894f282df1ad9165a7df` |
| Complete bundle SHA-256 | `2f2c022621babe1b1a56b6dbd5a34d235cad9df7ca1ac8a4fcbbdbb5eca71d82` |

These match the prepared candidate exactly. The trial used QRScanner's actual
bundled legacy executable for comparison and rollback, rather than B15's separate
rebuilt-source baseline. The supplied four 37-test package reports were inspected;
they were not four new runs by QRScanner. The candidate is unsigned. These exact bytes were published as
[v2.25.0-beta.1](https://github.com/existical/nssm-native/releases/tag/v2.25.0-beta.1) on 5 November 2026.

## Results

| Check | Reported result, checked against the supplied receipts |
| --- | --- |
| Real-service regression matrix | 195 checks passed, including legacy negative controls |
| Foreground application trial | 248 assertions passed; ten start/readiness/recognition/stop cycles |
| Controlled restart/STOP | Application failure/recovery; three STOP-during-restart and three STOP-during-startup trials passed |
| Reboot | Automatic service startup, readiness and representative work passed |
| Sustained workload | 24 monitored hours; 289 samples across four channels; 339,235 received events |
| Restart observations | No NSSM or application process restart observed during the soak |
| NSSM working set | 8.71 to 8.11 MiB; observed range 8.09-8.71 MiB |
| NSSM handles/threads | 138 to 137 handles; seven to four threads |
| Data and settings | Database integrity, Unicode data and configuration preserved |
| Rollback | Exact saved legacy executable restored and representative work passed |
| Cleanup | No owned test services, processes or monitoring task remained |

The original first application run is retained locally as a failure. Its harness
incorrectly required an application-owned endpoint file to disappear after forced
process death, even though NSSM STOP succeeded and no runtime processes remained.
The corrected check still requires removal after a normal drain; after forced
death it checks ownership and absence of a live publisher, then exercises recovery.
This is a harness expectation correction, not an NSSM fix or a relabelled pass.

The synthetic loopback workload exercised four recognition workers. Its event
count does not establish exactly-once delivery, recognition accuracy or unique
video-frame throughput. The measurements show no sustained NSSM resource growth
for this trial, not leak freedom for every workload. GUI editing was not applicable
to this CLI workflow; x86, other Windows builds, domain/non-System accounts,
remote/live inputs and production installer/helper adoption remain outside scope.

## Evidence handling and release status

The [machine-readable summary](qrscanner-beta-summary.json) retains only selected
aggregate results and the NSSM candidate identities already recorded in PR 23.
It excludes raw receipts, application source/archive/helper hashes, machine/network
identifiers, service/task names, process identities and private paths. The original
report and receipts were reviewed locally to check the sample count, event growth,
resource figures, passing assertions, data/rollback results and cleanup fields.
This verifies consistency of the supplied evidence, not independent re-execution.

The beta tag and original package hashes remain pinned to the tested commit.
This later documentation commit is not a new candidate build. Notes inside the
original archives reflect the earlier handoff when application acceptance was
pending; this summary supplies the later result without replacing those archives.
This summary accompanies the published beta assets.

The planned first application acceptance gate is satisfied within the stated
scope. PR 23 merged before GitHub prerelease publication on 5 November 2026.
QRScanner production adoption needs its helper/policy change, while stable-release
support claims need broader acceptance. The beta tag and original archives
retain their tested identities.
