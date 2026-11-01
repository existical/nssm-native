# Changelog

## Unreleased

- Pass registry value types directly to the settings-dump helper, eliminating
  x64 pointer conversion warnings. Treat pointer-size conversion warnings as
  compiler errors to prevent regressions.
- Move C++ code, Windows resources and historical documentation into dedicated
  directories while preserving the original Git history and release tags.
- Add a CMake build with shared x86/x64 Debug and Release presets, static C
  runtime linkage and generated message/version files outside the source tree.
- Add executable smoke checks and a Windows GitHub Actions build matrix.
- Add current build, usage and contributor documentation.

Historical release notes are preserved in
[docs/upstream/ChangeLog.txt](docs/upstream/ChangeLog.txt).
