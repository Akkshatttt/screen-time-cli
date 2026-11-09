/* check-update: asks GitHub for the latest release and prints it. On demand only; never downloads or installs.
 * MinGW 6.3 has no WinHTTP header or import lib, so winhttp.dll (a system DLL) is loaded at run time. */
#include "../core/wincompat.h"
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "update.h"

#define REPO_PATH L"/repos/Akkshatttt/screen-time-cli/releases/latest"

typedef void *H;
typedef H (WINAPI *F_open)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef H (WINAPI *F_connect)(H, LPCWSTR, WORD, DWORD);
typedef H (WINAPI *F_req)(H, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL (WINAPI *F_send)(H, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *F_recv)(H, LPVOID);
typedef BOOL (WINAPI *F_query)(H, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *F_read)(H, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *F_close)(H);
typedef BOOL (WINAPI *F_timeouts)(H, int, int, int, int);

/* Copies the string value of "key":"..." from a JSON blob into out. */
static int json_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[48];
    const char *p, *e;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), '"');
    if (!p) return 0;
    e = strchr(++p, '"');
    if (!e || (size_t)(e - p) >= n) return 0;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = 0;
    return 1;
}

int cmd_check_update(void)
{
    HMODULE dll = LoadLibraryW(L"winhttp.dll");
    H ses, con, req;
    F_open WOpen; F_connect WConnect; F_req WReq; F_send WSend; F_recv WRecv; F_query WQuery;
    F_read WRead; F_close WClose; F_timeouts WTimeouts;
    DWORD code = 0, len = sizeof code, got;
    static char body[16384];
    size_t used = 0;
    char tag[64], url[256];

    if (!dll) { puts("cannot check for updates: winhttp.dll not available"); return 1; }
    WOpen = (F_open)GetProcAddress(dll, "WinHttpOpen");
    WConnect = (F_connect)GetProcAddress(dll, "WinHttpConnect");
    WReq = (F_req)GetProcAddress(dll, "WinHttpOpenRequest");
    WSend = (F_send)GetProcAddress(dll, "WinHttpSendRequest");
    WRecv = (F_recv)GetProcAddress(dll, "WinHttpReceiveResponse");
    WQuery = (F_query)GetProcAddress(dll, "WinHttpQueryHeaders");
    WRead = (F_read)GetProcAddress(dll, "WinHttpReadData");
    WClose = (F_close)GetProcAddress(dll, "WinHttpCloseHandle");
    WTimeouts = (F_timeouts)GetProcAddress(dll, "WinHttpSetTimeouts");
    if (!WOpen || !WConnect || !WReq || !WSend || !WRecv || !WQuery || !WRead || !WClose || !WTimeouts) {
        puts("cannot check for updates: unexpected winhttp.dll");
        return 1;
    }

    printf("this program: %s\nchecking github.com/Akkshatttt/screen-time-cli ...\n", STC_VERSION);
    ses = WOpen(L"screen-time-cli", 0 /* default proxy */, NULL, NULL, 0);
    if (!ses) { puts("could not start a connection"); return 1; }
    WTimeouts(ses, 5000, 5000, 5000, 5000);
    con = WConnect(ses, L"api.github.com", 443, 0);
    req = con ? WReq(con, L"GET", REPO_PATH, NULL, NULL, NULL, 0x00800000 /* secure */) : NULL;
    if (!req || !WSend(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) || !WRecv(req, NULL)) {
        puts("could not reach GitHub (offline?). Nothing was changed.");
        if (req) WClose(req);
        if (con) WClose(con);
        WClose(ses);
        return 1;
    }
    WQuery(req, 19 /* status code */ | 0x20000000 /* as number */, NULL, &code, &len, NULL);
    while (used < sizeof body - 1 && WRead(req, body + used, (DWORD)(sizeof body - 1 - used), &got) && got) used += got;
    body[used] = 0;
    WClose(req); WClose(con); WClose(ses);

    if (code == 404 || code == 401 || code == 403) {
        puts("no release found (none published yet, or the repository is private). Nothing to do.");
        return 0;
    }
    if (code != 200 || !json_str(body, "tag_name", tag, sizeof tag)) {
        printf("unexpected answer from GitHub (HTTP %lu)\n", code);
        return 1;
    }
    if (stc_ver_cmp(tag, STC_VERSION) > 0) {
        printf("newer version available: %s\n", tag);
        if (json_str(body, "html_url", url, sizeof url)) printf("release notes and downloads: %s\n", url);
        puts("to upgrade: download or build it, then run: screen-time-cli install (from the new build)");
    } else puts("up to date");
    return 0;
}
