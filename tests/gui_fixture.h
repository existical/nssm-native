#ifndef NSSM_GUI_FIXTURE_H
#define NSSM_GUI_FIXTURE_H
#include "test.h"
namespace gui_test {
extern bool active;
LPVOID allocate(HANDLE, DWORD, SIZE_T);
BOOL release(HANDLE, DWORD, LPVOID);
BOOL close(SC_HANDLE);
}
#endif
