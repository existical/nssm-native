# B14 release preparation validation

Validation date: 2026 November 3. Baseline:
`0f856a4dedb7e5c9dc08fc010fb69bf90398a05d` (PR 18 merged).
This work prepares candidates; no continuation tag or release is published.

Scope: real NSSM-wrapped application lifecycle on a disposable elevated host;
clean pinned source packaging; legacy/prerelease version generation; source
archive identity; four binary/symbol ZIPs, manifests, checksums and test evidence.

Initial checks passed for legacy/stable/beta/RC/post-tag version generation,
source archive/header equivalence and rejection of malformed identity. Packaging
rejection cases passed for wrong revision, untracked, unstaged and staged
changes. All test tags exist only in disposable local fixtures; upstream tags
were not changed.

The lifecycle fixture uses one uniquely created wrapper service and an owned
temporary directory. Its helper reads stdin and checks an environment value
after an empty entry, spawns a descendant, crashes once, restarts, then completes
two stop/start cycles. Start/Post, Stop/Pre and Exit/Post commands use a helper
path containing spaces and a final quoted argument. It checks final stdout/
stderr, hook output and retained process handles after shutdown. Cleanup touches
only owned objects; children have 45-second watchdogs. No existing local service
is created, reconfigured, started or stopped by unprivileged tests.

Results and the immutable implementation/CI revisions will be filled after the
complete package run and disposable-runner acceptance. Local logs stay under
ignored build directories; durable CI evidence is linked here before merge.

Known limits: the fixture is a small synthetic application, not a production
soak or exhaustive interleaving test. Manual GUI, domain accounts, older Windows,
signing, installed-system upgrade/rollback and large process/hook populations
remain release acceptance considerations. SOURCE_DATE_EPOCH controls version
dates, not every compiler/PDB/archive timestamp; bitwise reproducibility is not
claimed. No historical executable is executed by B14.
