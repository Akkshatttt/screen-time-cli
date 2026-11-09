/* screen-time-daemon: hidden, event driven. Sleeps in the message loop; wakes on
 * foreground/title events, session lock, power events, and one 30 s heartbeat timer. */
#include "../core/wincompat.h"
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "media.h"

#define T_EVAL 1   /* debounced re-evaluation after an event */
#define T_BEAT 2   /* heartbeat: extends the open span, re-checks idle/media */
#define T_TITLE 3  /* title settled: animated titles (spinners) keep restarting it, so they never record */
#define TITLE_SETTLE_MS 5000
#define DEBOUNCE_MS 400

static sqlite3 *g_db;
static stc_tracker g_tr;
static HWND g_wnd;
static int g_locked, g_paused, g_suspended;
static unsigned g_active_s = 60, g_away_s = 600, g_beat_s = 30;

/* cache of the last foreground process */
static DWORD g_pid;
static sqlite3_int64 g_exe;
static wchar_t g_exe_w[MAX_PATH];
static sqlite3_int64 g_title;
static HWINEVENTHOOK g_title_hook; /* title events, narrowed to the foreground process */

static void utf8(const wchar_t *w, char *out, int n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL)) out[0] = 0;
}

static void load_exe(DWORD pid)
{
    wchar_t path[MAX_PATH] = L"";
    char p8[MAX_PATH * 3], n8[MAX_PATH * 3];
    const wchar_t *base;
    DWORD n = MAX_PATH;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) { QueryFullProcessImageNameW(h, 0, path, &n); CloseHandle(h); }
    if (!path[0]) wcscpy(path, L"[protected]");
    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    utf8(path, p8, sizeof p8);
    utf8(base, n8, sizeof n8);
    g_exe = stc_intern_exe(g_db, p8, n8);
    wcscpy(g_exe_w, path);
    g_pid = pid;
}

static void CALLBACK on_event(HWINEVENTHOOK hk, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD ms);

static void eval(int refresh_title)
{
    time_t now = time(NULL);
    HWND h;
    DWORD pid = 0;
    LASTINPUTINFO li;
    unsigned idle = 0;
    int media = 0, state;

    if (g_paused || g_suspended) return;
    li.cbSize = sizeof li;
    h = GetForegroundWindow();
    if (h) GetWindowThreadProcessId(h, &pid);
    if (h && pid) {
        wchar_t t[256] = L"";
        char t8[256 * 3];
        int new_exe = pid != g_pid;
        if (new_exe) {
            load_exe(pid);
            if (g_title_hook) UnhookWinEvent(g_title_hook);
            g_title_hook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, on_event,
                                           pid, 0, WINEVENT_OUTOFCONTEXT);
        }
        if (new_exe || refresh_title) {
            GetWindowTextW(h, t, 256);
            utf8(t, t8, sizeof t8);
            g_title = stc_intern_title(g_db, t8);
        }
    }
    if (!g_exe) return; /* nothing seen yet */

    if (GetLastInputInfo(&li)) idle = (GetTickCount() - li.dwTime) / 1000;
    /* media check costs COM calls: only when it can change the answer */
    if (!g_locked && idle >= g_active_s)
        media = stc_fullscreen(h) || stc_audio_playing(g_exe_w);
    state = stc_classify(idle, media, g_locked, g_active_s, g_away_s);
    stc_tr_update(&g_tr, now, g_exe, g_title, state);
}

static void schedule(void) { SetTimer(g_wnd, T_EVAL, DEBOUNCE_MS, NULL); }

static void CALLBACK on_event(HWINEVENTHOOK hk, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD ms)
{
    (void)hk; (void)tid; (void)ms;
    if (ev == EVENT_OBJECT_NAMECHANGE) {
        if (obj == OBJID_WINDOW && child == CHILDID_SELF && h == GetForegroundWindow())
            SetTimer(g_wnd, T_TITLE, TITLE_SETTLE_MS, NULL); /* restarts on every change */
        return;
    }
    schedule();
}

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TIMER:
        if (wp == T_EVAL || wp == T_TITLE) KillTimer(w, wp);
        eval(wp != T_BEAT);
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) g_locked = 1;
        else if (wp == WTS_SESSION_UNLOCK) g_locked = 0;
        else return 0;
        eval(1);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) { stc_tr_close(&g_tr, time(NULL)); g_suspended = 1; }
        else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) { g_suspended = 0; schedule(); }
        return TRUE;
    case WM_ENDSESSION:
        if (wp) { stc_tr_close(&g_tr, time(NULL)); sqlite3_close(g_db); }
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void load_config(void)
{
    char dir[MAX_PATH * 3];
    wchar_t w[MAX_PATH], ini[MAX_PATH * 2];
    if (stc_data_dir(dir, sizeof dir)) return;
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, w, MAX_PATH);
    _snwprintf(ini, MAX_PATH * 2, L"%ls\\config.ini", w);
    g_active_s = GetPrivateProfileIntW(L"tracking", L"active_seconds", 60, ini);
    g_away_s = GetPrivateProfileIntW(L"tracking", L"away_seconds", 600, ini);
    g_beat_s = GetPrivateProfileIntW(L"tracking", L"heartbeat_seconds", 30, ini);
    if (g_beat_s < 5) g_beat_s = 5;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    HANDLE once, ev_stop, ev_pause, ev_resume, hs[3];
    WNDCLASSW wc;
    MSG msg;
    (void)prev; (void)cmd; (void)show;
    memset(&wc, 0, sizeof wc);

    once = CreateMutexW(NULL, FALSE, L"Local\\screen-time-daemon");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    ev_stop = CreateEventW(NULL, FALSE, FALSE, L"Local\\screen-time-stop");
    ev_pause = CreateEventW(NULL, FALSE, FALSE, L"Local\\screen-time-pause");
    ev_resume = CreateEventW(NULL, FALSE, FALSE, L"Local\\screen-time-resume");
    hs[0] = ev_stop; hs[1] = ev_pause; hs[2] = ev_resume;

    load_config();
    g_db = stc_db_open(NULL, 0);
    if (!g_db) return 1;
    stc_meta_set(g_db, "paused", "0");
    stc_meta_set(g_db, "daemon_version", STC_VERSION);
    stc_tr_init(&g_tr, g_db);

    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.lpszClassName = L"stc_daemon";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    WTSRegisterSessionNotification(g_wnd, NOTIFY_FOR_THIS_SESSION);
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, on_event, 0, 0,
                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(g_wnd, T_BEAT, g_beat_s * 1000, NULL);
    g_tr.min_extend = g_beat_s - 2;
    eval(1);

    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(3, hs, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) {
            stc_tr_close(&g_tr, time(NULL));
            g_paused = 1;
            stc_meta_set(g_db, "paused", "1");
        } else if (r == WAIT_OBJECT_0 + 2) {
            g_paused = 0;
            stc_meta_set(g_db, "paused", "0");
            eval(1);
        } else {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    stc_tr_close(&g_tr, time(NULL));
    sqlite3_close(g_db);
    CloseHandle(once);
    return 0;
}
