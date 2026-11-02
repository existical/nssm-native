#ifndef NSSM_ROTATION_FIXTURE_H
#define NSSM_ROTATION_FIXTURE_H
#include "test.h"
namespace rotation_test {
extern bool active;
BOOL WINAPI copy(LPCTSTR, LPCTSTR, BOOL);
BOOL WINAPI move(LPCTSTR, LPCTSTR);
BOOL WINAPI information(HANDLE, LPBY_HANDLE_FILE_INFORMATION);
void WINAPI sleep(DWORD);
void WINAPI now(LPSYSTEMTIME);
void event(unsigned long);
}
#endif
