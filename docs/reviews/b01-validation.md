# B01 registry and environment validation

This records validation for NSSM-001, NSSM-003, NSSM-010 and NSSM-011.
The [individual records](../fixes/README.md) identify implementation/release
status; this evidence does not mark an unmerged branch as released.

The pre-fix production source was
`05205d7784dbd0e90dc6d26957d3133f7a1c62a9`, following documentation PR 4.
Comparing its `src/registry.cpp` with
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a:registry.cpp` showed that the four
affected functions were identical. The only intervening change in that file
was the previously documented hook-quoting fix.

| Scenario | Pre-fix observation | Corrected result |
| --- | --- | --- |
| Expansion into a 64-character destination | Oversized results accepted; destination guards changed | Oversized results rejected; output empty and guards intact |
| Duplicate environment update | Wrong result and write beyond requested allocation | One replacement at the first match; guards intact |
| Unterminated/mistyped/misaligned registry data | Invalid data accepted or scanned past returned bytes | Reader failure with empty/null output |
| Formatting final double NUL next to a nonzero sentinel | Wrong output length and trailing CRLF | Exact terminated output without following-byte access |
| Empty input append | Pre-fix MULTI_SZ test process faulted on the null block | New entry forms a valid block |

The initial pre-fix `nssm.registry` run failed 219 assertions, including
repeated guard checks. `nssm.multisz` reported formatting and duplicate
failures, then faulted in the isolated test process on an empty input.
Additional boundary and failure cases were subsequently added to the committed
[regression source](../../tests/registry.cpp).

Final local validation used Visual Studio 2022, MSVC toolset 14.44.35207
(compiler 19.44.35229.0), Windows SDK 10.0.26100.0 and Windows 10.0.26300.0:

| Configuration | Result |
| --- | --- |
| x64 Release | All six CTest groups passed |
| x64 Debug | All six CTest groups passed |
| x86 Release | All six CTest groups passed |
| x86 Debug | All six CTest groups passed |
| x64 Release with AddressSanitizer | `nssm.registry` and `nssm.multisz` passed |
| MSVC `/analyze` Release | Build completed; no diagnostics in the changed reader, append or formatting functions |

Analysis still reports existing diagnostics in `unformat_double_null`
and the large stack frame in `get_parameters`. Those functions are outside
the B01 repair. This is not a claim that the full project is analyzer-clean.

The ordinary groups are `nssm.smoke`, `nssm.environment`, `nssm.hooks`,
`nssm.startup`, `nssm.registry` and `nssm.multisz`. Tests compile production
implementations with test-only guarded allocation, query and expansion
wrappers. An actual PAGE_NOACCESS boundary detects input overreads. Registry
writes are redirected to a temporary HKCU fixture. Tests install or control
no services, and do not require elevation.

Reproduce the standard matrix with [the build commands](../building.md).
For a separate x64 Release sanitizer build:

```powershell
cmake -S . -B build/b01-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address'
cmake --build build/b01-asan --config Release --parallel
# Use a VS developer shell or add the selected toolset's x64 ASan DLL
# directory to PATH before running:
ctest --test-dir build/b01-asan -C Release -R 'nssm.(registry|multisz)' --output-on-failure
```

For static analysis, configure a separate build with
`-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze` and build Release. Test wrappers
and sanitizer flags do not change the normal shipped build.
