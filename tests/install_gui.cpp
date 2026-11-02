#include "install_fixture.h"
#define popup_message install_test::popup
#include "../src/gui.cpp"
#undef popup_message

void test_install_gui() {
  registry_fixture registry;
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
  REQUIRE(InitCommonControlsEx(&controls));
  nssm_service_t *defaults = new nssm_service_t();
  HWND window = dialog(MAKEINTRESOURCE(IDD_INSTALL), NULL, nssm_dlg, reinterpret_cast<LPARAM>(defaults));
  delete defaults;
  REQUIRE(window && ! IsWindowVisible(window));
  SetDlgItemText(window, IDC_NAME, _T("NSSM-Test-Install"));
  SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, _T("C:\\fixture.exe"));
  const int expected[] = {2, 5, 6, 6, 0};
  for (unsigned scenario = 0; scenario < _countof(expected); scenario++) {
    install_test::reset(scenario);
    CHECK(install(window) == expected[scenario]);
    install_test::finish();
  }
  DestroyWindow(window);
}
