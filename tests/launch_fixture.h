#ifndef NSSM_LAUNCH_FIXTURE_H
#define NSSM_LAUNCH_FIXTURE_H
#include "test.h"
namespace launch_test {
const TCHAR *imagepath();
BOOL WINAPI create(LPCTSTR, LPTSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCTSTR, LPSTARTUPINFO, LPPROCESS_INFORMATION);
BOOL WINAPI close(HANDLE);
BOOL WINAPI terminate(HANDLE, UINT);
DWORD WINAPI wait(HANDLE, DWORD);
DWORD WINAPI resume(HANDLE);
BOOL WINAPI code(HANDLE, LPDWORD);
BOOL WINAPI affinity(HANDLE, PDWORD_PTR, PDWORD_PTR);
BOOL WINAPI set_affinity(HANDLE, DWORD_PTR);
BOOL WINAPI status(SERVICE_STATUS_HANDLE, LPSERVICE_STATUS);
BOOL WINAPI free_console();
int parameters(nssm_service_t *, STARTUPINFO *);
int output(nssm_service_t *, STARTUPINFO *);
void close_output(STARTUPINFO *);
void cleanup(nssm_service_t *);
void environment(TCHAR *);
int time(HANDLE, FILETIME *);
int exit_time(HANDLE, FILETIME *);
int kill(kill_t *);
unsigned long kill_tree(kill_t *, unsigned long);
int hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long, bool);
int hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long);
int hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *);
void log(unsigned short, unsigned long, ...);
}
void test_launch_handles();
#endif
