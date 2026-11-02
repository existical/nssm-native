# NSSM 035 Standard-I/O configuration byte capacity

| Field | Value |
| --- | --- |
| Priority and origin | P1; startup blocker discovered during B14 acceptance |
| State | In progress; merge pending |
| Reproduced against | `0f856a4dedb7e5c9dc08fc010fb69bf90398a05d` |
| Introduced startup rejection | `1ecfc6be59e80fe78ea94553634d02a6ed9524d0` (B01) |
| Legacy source checked | `dee49fc1a0804f06687299b8e9612a3fce1c5e9a` |
| Fix PR | [PR 20](https://github.com/existical/nssm-native/pull/20) |
| Implementation commit | `3146ceca98065907f5f4b5d999139e287065b2ed` |
| First release | Not yet released |

The first real NSSM-wrapped lifecycle test reached SERVICE_STOPPED with
service-specific code 2 during START. get_parameters failed in its standard-I/O
reader. get_createfile_parameters passed PATH_LENGTH (32,767 TCHARs in Unicode
builds) as the byte capacity to expand_parameter/get_string. B01's stricter
reader rejects odd byte capacities, so this caller failed even when AppStdin
was absent. This prevented wrapped service startup in the continuation.

The legacy source also passed PATH_LENGTH, but it did not have the new odd-byte
rejection. This is a continuation startup regression exposed by B14, not a
claim that legacy NSSM universally failed to start. It is additional to the
original 34 review tasks. No release containing this regression was published.

The caller now passes `PATH_LENGTH * sizeof(*path)`. Registry validation stays
strict and the complete allocated character capacity is available. The same
path is used for AppStdin, AppStdout and AppStderr. No schema, timeout, account
or command-line changes are needed.

The extended `nssm.stdin` regression executes the real configuration reader
against an HKCU-isolated registry fixture, including absent redirection, short
paths, the maximum terminated path and one character too many for each stream.
Canaries beyond the destination remain intact. It also calls get_parameters
instead of only constructing already-populated test service objects. The new
cases produced 28 failed checks against the baseline, then passed with the
one-line capacity correction. Existing inherited-stdin/child tests remain.

Validation date: 2026 November 3. All 35 standard CTest groups passed in
x64/x86 Debug/Release. AddressSanitizer passed stdin, registry and logger
(3/3 groups) with no findings. Local logs: build/stdio-*-*.log and
build/stdio-asan.log; initial failing evidence: build/b14-stdio-red-tests.log.
[CI run 41542196154](https://github.com/existical/nssm-native/actions/runs/41542196154)
passed all four jobs at `787f3920defd7b12ede0fee414b809637fd95b1d`:
38 groups in x64 Release (including the three existing SCM suites), 35 in each
other configuration. Full wrapper acceptance continues separately in PR 19;
its startup now succeeds after this correction. The release-preparation PR retains
its real SCM lifecycle scenario so future coverage includes actual startup.
After the additional NSSM-036 cleanup repair, the complete lifecycle and
source-archive packaging passed in
[CI 41544419568](https://github.com/existical/nssm-native/actions/runs/41544419568);
see [B14 validation](../reviews/b14-validation.md) for the pinned revision and scope.

```powershell
git show dee49fc1a0804f06687299b8e9612a3fce1c5e9a:io.cpp
git show 1ecfc6be59e80fe78ea94553634d02a6ed9524d0:src/registry.cpp
```
