#ifndef NSSM_INSTALL_FIXTURE_H
#define NSSM_INSTALL_FIXTURE_H
#include "test.h"
namespace install_test {
extern bool active, fail_allocation;
void reset(unsigned);
void finish();
LPVOID allocate(HANDLE, DWORD, SIZE_T);
BOOL release(HANDLE, DWORD, LPVOID);
SC_HANDLE manager();
BOOL close(SC_HANDLE);
SC_HANDLE WINAPI create(SC_HANDLE, LPCTSTR, LPCTSTR, DWORD, DWORD, DWORD, DWORD, LPCTSTR, LPCTSTR, LPDWORD, LPCTSTR, LPCTSTR, LPCTSTR);
BOOL WINAPI change(SC_HANDLE, DWORD, DWORD, DWORD, LPCTSTR, LPCTSTR, LPDWORD, LPCTSTR, LPCTSTR, LPCTSTR, LPCTSTR);
BOOL WINAPI change2(SC_HANDLE, DWORD, LPVOID);
BOOL WINAPI remove(SC_HANDLE);
int parameters(nssm_service_t *, bool);
int popup(HWND, unsigned int, unsigned long, ...);
}
void test_install_gui();
#endif
