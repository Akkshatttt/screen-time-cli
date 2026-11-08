/* MinGW 6.3 headers lack a few declarations (and wtsapi32.h does not compile). */
#ifndef WINCOMPAT_H
#define WINCOMPAT_H
#define _WIN32_WINNT 0x0601
#include <windows.h>

WINBASEAPI BOOL WINAPI QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR, PDWORD);
WINBASEAPI BOOL WINAPI WTSRegisterSessionNotification(HWND, DWORD);
#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#define WTS_SESSION_LOCK 7
#define WTS_SESSION_UNLOCK 8
#define NOTIFY_FOR_THIS_SESSION 0
#endif
