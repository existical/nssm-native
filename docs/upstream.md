# Upstream history and continuation

This repository continues the native C++ NSSM application from
<https://git.nssm.cc/nssm/nssm.git>. The upstream author is Iain Patterson.
NSSM's original public-domain statement is retained in [LICENSE](../LICENSE),
and contributor credits remain in the [upstream README](upstream/README.txt).

The recovered upstream baseline is
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (16 May 2017), with 367 commits.
All original source files matched that baseline exactly before local setup work.
The continuation setup was committed separately as
`2a7aa086a709914e478b98273c970937dca81aad`.

The official HTTPS endpoint advertised no release tags during recovery.
The 26 historical annotated tags, `v1.0` and `v2.0` through `v2.24`, were recovered
unchanged from <https://github.com/ONLYOFFICE/nssm>. Every target commit was
verified to belong to the original upstream history. This consistency check
does not authenticate the tags with cryptographic signatures.

The ONLYOFFICE fork adds a description change and merges older upstream work.
Its fork-specific commits were not merged into this project's development
branch. The existing `existical/nssm` repository is a separate Node.js wrapper;
the native application is maintained at <https://github.com/existical/nssm-native>.

Historical documents are preserved without rewriting their old instructions:

- [Original README and manual](upstream/README.txt)
- [Original changelog](upstream/ChangeLog.txt)
- [Git recovery record](upstream/GIT-RECOVERY.txt)

Current build instructions are in [building.md](building.md). The Visual Studio
2008 project, original batch version generator and first local build script
remain available in Git history. CMake is the build definition for the current
source layout.
