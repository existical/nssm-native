# B08 ownership and error-path validation

Validation date: 2026 November 2. Scope: NSSM-015, NSSM-018, NSSM-019,
NSSM-021, NSSM-028 and NSSM-032. The [fix index](../fixes/README.md) tracks
implementation, merge and release status.

The baseline is `0ba9551903b5a47568bd317c5447fe4eed54d685`, after PR 12 merged.
The relevant pre-fix functions match legacy
`dee49fc1a0804f06687299b8e9612a3fce1c5e9a` (`v2.24-103-gdee49fc`):
setting_set_string, check_admin, await_single_handle, install_service,
pre_install_service, get_debug_token, error_string and message_string.
Each initial test used the relevant unmodified function before its repair.
No historic binary was executed and no real service was installed or controlled.

| Fix | Initial evidence | Final regression coverage |
| --- | --- | --- |
| [015](../fixes/NSSM-015.md) | Three reset pointers unexpectedly changed to default literals | Actual isolated registry values, null input objects/values, empty/null/nonempty defaults, explicit input, write/delete failures and allocation denial |
| [018](../fixes/NSSM-018.md) | Three invalid BOOL destinations plus a true result after API failure; four failed assertions | Member/nonmember, allocation failure, failed query with misleading output, balanced SID release |
| [019](../fixes/NSSM-019.md) | Zero timeout made no OS wait and returned undefined results; nine checks failed in the observed run | Simulated and real event outcomes at zero/positive timeouts, invalid handle, status/checkpoint behavior, multiple intervals and AppThrottle=0 startup |
| [021](../fixes/NSSM-021.md) | CLI and actual hidden GUI each destroyed their service object twice on SCM-open failure; four checks failed | Open/create/configure/parameter failures and success through both callers; object/handle/rollback counts; CLI allocation failure and oversized arguments |
| [028](../fixes/NSSM-028.md) | Incomplete assignment accepted, impersonation left active, failed impersonation ignored; three failed checks | No/existing token, enabled/disabled privilege, access/retry/lookup/adjustment failures, ERROR_NOT_ALL_ASSIGNED, restoration and intercepted fatal cleanup failures |
| [032](../fixes/NSSM-032.md) | Invalid advertised capacities and lost fallback allocation; seven checks failed | Long injected messages, locale/numeric fallback, allocation/format failure, repeated calls, concurrent thread isolation, 40 real worker exits and native system/resource formatting |

The admin wrapper refuses to write a four-byte BOOL into a one-byte bool.
The message wrapper checks allocation capacities before copying or formatting
text. Thus the red tests demonstrate unsafe API contracts without corrupting
test memory. Install allocations are quarantined until assertions finish so
the old duplicate destruction is observable without freeing live data twice.
Token tests use fake handles and identity state; no real privilege is enabled
and no other account is impersonated. Fatal restoration exits are intercepted.

The GUI install test loads the real installer resource and calls its actual
install path with valid controls. The parent dialog stays hidden; popups are
intercepted. SCM operations are fake, and registry writes are redirected to an
isolated HKCU fixture. This covers install ownership; it is not an interactive
GUI/account acceptance test or coverage of the separately planned edit-dialog
ownership problem. All six new groups have a 20-second CTest timeout.

Error-message worker fixtures signal a release event and join returning
threads before freeing their handles. Their thread-local allocations are
counted: one main-thread buffer plus two live worker buffers, returning to one
after each pair exits and zero after fixture cleanup. The 40000-character
message is injected to test capacity units; it is not evidence of a real
system message that long. Existing live logger and hook tests also run after
the TLS ownership change.

Local tools: Visual Studio 2022, MSVC toolset 14.44.35207 (compiler
19.44.35229.0), Windows SDK 10.0.26100.0, Windows 10.0.26300.0.

| Configuration | Result |
| --- | --- |
| x64 Release | All 22 CTest groups passed |
| x64 Debug | All 22 CTest groups passed |
| x86 Release | All 22 CTest groups passed |
| x86 Debug | All 22 CTest groups passed |
| x64 AddressSanitizer | All six B08 groups plus launch, hook_live and logger passed (nine groups) |
| MSVC `/analyze` Release | Build completed; remaining diagnostics retained |

The former event-buffer C6386 diagnostics, reset setter's null-value C6011
and await_single_handle's uninitialized-result C6001 were absent in this
analysis run. Existing large stack frames, ignored service API results,
critical-section warnings, and AppExit value/termination warnings remain,
along with diagnostics elsewhere in the project. None were suppressed.
This is not an analyzer-clean claim or old-Windows compatibility certification.

Use [building instructions](../building.md) for the matrix. Targeted sanitizer
checks are reproducible with:

```powershell
cmake -S . -B build/b08-asan -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG'
cmake --build build/b08-asan --config Release --parallel
# Use a VS developer shell or add the toolset's x64 ASan DLL directory to PATH.
ctest --test-dir build/b08-asan -C Release -R '^nssm.(admin|setting_ownership|waits|install_ownership|debug_token|error_messages|launch|hook_live|logger)$' --output-on-failure
```

The analysis build uses `-DBUILD_TESTING=OFF -DCMAKE_CXX_FLAGS=/analyze`.
Production source retains CRLF; B08 changes no message-resource IDs or encodings.
