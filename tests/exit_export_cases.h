// Included after settings.cpp so tests can exercise its actual callbacks.
static void test_exit_export_cases(const TCHAR *source, HKEY parameters, HKEY key, settings_t *setting) {
  capture_dump = true;
  REQUIRE(set_string(key, _T("0"), _T("Restart")) == 0);
  REQUIRE(set_string(key, _T("4294967295"), _T("Suicide")) == 0);
  const TCHAR *target = _T("NSSM Test Imported");
  dump_output.clear();
  CHECK(dump_setting(target, parameters, NULL, setting, source) == 0);
  const text_t exported = dump_output;
  CHECK(exported.find(_T(" AppExit 0 Restart\n")) != text_t::npos);
  CHECK(exported.find(_T(" AppExit 4294967295 Suicide\n")) != text_t::npos);
  // Replay the actual emitted command arguments through the real setter.
  size_t offset = 0; unsigned rules = 0;
  while (offset < exported.size()) {
    size_t end = exported.find(_T('\n'), offset); REQUIRE(end != text_t::npos);
    text_t line = exported.substr(offset, end - offset);
    int argc; TCHAR **argv = CommandLineToArgvW(line.c_str(), &argc); REQUIRE(argv);
    REQUIRE(argc == 6);
    CHECK(! _tcscmp(argv[1], _T("set")) && ! _tcscmp(argv[2], target) && ! _tcscmp(argv[3], NSSM_REG_EXIT));
    value_t input; input.string = argv[5];
    CHECK(setting_set_exit_action(argv[2], NULL, NSSM_REG_EXIT, setting->default_value, &input, argv[4]) >= 0);
    LocalFree(argv); offset = end + 1; rules++;
  }
  CHECK(rules == 4);
  HKEY imported = open_registry(target, KEY_READ); REQUIRE(imported);
  dump_output.clear(); CHECK(dump_setting(target, imported, NULL, setting) == 0);
  CHECK(dump_output == exported);
  RegCloseKey(imported);
  const TCHAR *aliases[] = {_T("042"), _T("0x2a"), _T("+42"), _T(" 42"), _T("42x"), _T("4294967296"), _T("abcdefghijklmnop-long-value-name")};
  for (const TCHAR *alias : aliases) {
    REQUIRE(set_string(key, const_cast<TCHAR *>(alias), _T("Exit")) == 0);
    dump_output.clear(); CHECK(dump_setting(source, parameters, NULL, setting) == -1);
    REQUIRE(RegDeleteValue(key, alias) == ERROR_SUCCESS);
  }
  REQUIRE(set_number(key, _T("2"), 5) == 0);
  CHECK(dump_setting(source, parameters, NULL, setting) == -1);
  REQUIRE(RegDeleteValue(key, _T("2")) == ERROR_SUCCESS);
  const TCHAR *spellings[] = {_T("42"), _T("052"), _T("0x2a"), _T("+42"), _T(" 42")};
  for (const TCHAR *spelling : spellings) {
    value_t input; input.string = _T("Ignore");
    CHECK(setting_set_exit_action(source, NULL, NSSM_REG_EXIT, setting->default_value, &input, spelling) == 1);
    TCHAR action[ACTION_LEN]; bool fallback; DWORD code = 42;
    CHECK(get_exit_action(source, &code, action, &fallback) == 0 && ! fallback && ! _tcscmp(action, _T("Ignore")));
    if (_tcscmp(spelling, _T("42"))) CHECK(RegQueryValueEx(key, spelling, NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND);
    CHECK(setting_set_exit_action(source, NULL, NSSM_REG_EXIT, setting->default_value, NULL, spelling) == 0);
    CHECK(RegQueryValueEx(key, _T("42"), NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND);
  }
  value_t result = {};
  reset_test::active = true;
  CHECK(setting_get_exit_action(source, NULL, NSSM_REG_EXIT, setting->default_value, &result, _T("Default")) == -1);
  CHECK(! result.string);
  reset_test::active = false;
  CHECK(setting_set_exit_action(source, NULL, NSSM_REG_EXIT, NULL, NULL, NULL) == 0);
  CHECK(setting_get_exit_action(source, NULL, NSSM_REG_EXIT, NULL, &result, NULL) == 0);
  REQUIRE(result.string); CHECK(! _tcscmp(result.string, _T("Restart"))); HeapFree(GetProcessHeap(), 0, result.string);
  // Renaming a dump must read source hooks and native environment as well.
  REQUIRE(set_hook(source, _T("Start"), _T("Pre"), _T("fixture-command")) == 0);
  HKEY native = open_service_registry(source, KEY_SET_VALUE, true); REQUIRE(native);
  const TCHAR environment[] = _T("EXPORT_TEST=value\0");
  REQUIRE(RegSetValueEx(native, NSSM_NATIVE_ENVIRONMENT, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(environment), sizeof(environment)) == ERROR_SUCCESS);
  RegCloseKey(native);
  reset_test::tracking = true;
  DWORD before = test_handle_count();
  for (unsigned repeat = 0; repeat < 100; repeat++) {
    dump_output.clear();
    for (unsigned i = 0; settings[i].name; i++) {
      const TCHAR *name = settings[i].name;
      if (_tcscmp(name, NSSM_REG_EXIT) && _tcscmp(name, NSSM_REG_HOOK) && _tcscmp(name, NSSM_NATIVE_ENVIRONMENT) && _tcscmp(name, NSSM_REG_PRIORITY)) continue;
      CHECK(dump_setting(target, parameters, reinterpret_cast<SC_HANDLE>(1), &settings[i], source) == 0);
    }
    CHECK(dump_output.find(_T(" AppEvents Start/Pre fixture-command\n")) != text_t::npos);
    CHECK(dump_output.find(_T(" Environment :EXPORT_TEST=value\n")) != text_t::npos);
    CHECK(reset_test::owned.empty());
  }
  CHECK(test_handle_count() == before);
  reset_test::tracking = false;
  CHECK(reset_test::owned.empty());
  capture_dump = false;
}
