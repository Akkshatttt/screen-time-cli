/* screen-time-daemon: hidden, event driven. Sleeps in the message loop; wakes on foreground events, settled title
 * changes, session lock, power events, and one heartbeat timer. Built to stay tiny: see HANDOVER.md for the budget. */
#include "../core/wincompat.h"
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "media.h"

#define T_EVAL 1   /* debounced re-evaluation after a foreground change */
#define T_BEAT 2   /* heartbeat: extends the open span, re-checks idle/media */
#define T_TITLE 3  /* title settled: animated titles (spinners) keep it pending, so they never record */
#define TITLE_SETTLE_MS 5000
#define DEBOUNCE_MS 400
#define TRIM_EVERY_MS 30000

static sqlite3 *g_db;
static stc_tracker g_tr;
static stc_hb g_hb;
static HWND g_wnd;
static int g_locked, g_paused, g_suspended;
static unsigned g_active_s = 60, g_away_s = 600, g_beat_s = 60;

/* cache of the last foreground process and title: unchanged values never touch SQLite */
static DWORD g_pid;
static sqlite3_int64 g_exe;
static wchar_t g_exe_w[MAX_PATH];
static sqlite3_int64 g_title;
static char g_title_text[256 * 3];
static HWINEVENTHOOK g_title_hook; /* title events, narrowed to the foreground process */

/* title settle: one timer, re-armed when it fires early (animated titles keep it pending, so they never record) */
static DWORD g_last_title_ms;
static int g_title_armed;

/* media probe cache */
static time_t g_probe_s;
static int g_media;

#ifdef STC_PROFILE
static unsigned g_n_fg, g_n_title, g_n_raw, g_n_eval, g_n_beat, g_n_probe;
static LONGLONG g_us_eval, g_us_probe;
static LARGE_INTEGER g_qf;
static ULONG64 g_cycles0;   /* cost of startup, subtracted from the steady-state figure */
static LONGLONG qpc_us(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000000 / g_qf.QuadPart; }
#define PROF(x) x
#else
#define PROF(x)
#endif

static void utf8(const wchar_t *w, char *out, int n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL)) out[0] = 0;
}

/* Shared DLL pages the daemon is not touching leave its working set. Rate limited. */
static void trim(void)
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (last && now - last < TRIM_EVERY_MS) return;
    last = now;
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void CALLBACK on_event(HWINEVENTHOOK hk, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD ms);

static void hook_title(DWORD pid)
{
    if (g_title_hook) UnhookWinEvent(g_title_hook);
    g_title_hook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, on_event, pid, 0,
                                   WINEVENT_OUTOFCONTEXT);
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

static void eval(int refresh_title)
{
    time_t now = time(NULL);
    HWND h;
    DWORD pid = 0;
    LASTINPUTINFO li;
    unsigned idle = 0;
    int media = 0, state, began = 0;
    PROF(LONGLONG t0 = qpc_us(); g_n_eval++;)

    if (g_paused || g_suspended) return;
    li.cbSize = sizeof li;
    h = GetForegroundWindow();
    if (h) GetWindowThreadProcessId(h, &pid);
    if (h && pid) {
        int new_exe = pid != g_pid;
        if (new_exe) {
            sqlite3_exec(g_db, "BEGIN", 0, 0, 0);   /* exe, title and span rows share one commit */
            began = 1;
            load_exe(pid);
            hook_title(pid);
            g_probe_s = 0;                      /* probe the new app right away if idle */
        }
        if (new_exe || refresh_title) {
            wchar_t t[256] = L"";
            char t8[256 * 3];
            GetWindowTextW(h, t, 256);
            utf8(t, t8, sizeof t8);
            if (new_exe || strcmp(t8, g_title_text)) {      /* same text = same id: skip SQLite */
                if (!began) { sqlite3_exec(g_db, "BEGIN", 0, 0, 0); began = 1; }
                g_title = stc_intern_title(g_db, t8);
                strcpy(g_title_text, t8);
            }
        }
    }
    if (!g_exe) return; /* nothing seen yet */

    if (GetLastInputInfo(&li)) idle = (GetTickCount() - li.dwTime) / 1000;
    if (!g_locked && idle >= g_active_s) {
        /* the audio probe is the daemon's most expensive act: cache it between heartbeats */
        if (stc_probe_due(now, g_probe_s, g_beat_s)) {
            PROF(LONGLONG p0 = qpc_us();)
            g_media = stc_fullscreen(h) || stc_audio_playing(g_exe_w);
            g_probe_s = now;
            PROF(g_n_probe++; g_us_probe += qpc_us() - p0;)
        } else if (!g_media) {
            g_media = stc_fullscreen(h);         /* cheap; lets a fullscreen switch count at once */
        }
        media = g_media;
    } else {
        g_probe_s = 0;
        g_media = 0;
    }
    state = stc_classify(idle, media, g_locked, g_active_s, g_away_s);
    stc_tr_update(&g_tr, now, g_exe, g_title, state);   /* its own BEGIN fails inside ours; its COMMIT ends both */
    if (began && !sqlite3_get_autocommit(g_db)) sqlite3_exec(g_db, "COMMIT", 0, 0, 0);
    PROF(g_us_eval += qpc_us() - t0;)
}

static void CALLBACK on_event(HWINEVENTHOOK hk, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD ms)
{
    (void)hk; (void)tid; (void)ms;
    if (ev == EVENT_OBJECT_NAMECHANGE) {
        DWORD now = GetTickCount();
        PROF(g_n_raw++;)
        if (obj != OBJID_WINDOW || child != CHILDID_SELF || h != GetForegroundWindow()) return;
        PROF(g_n_title++;)
        g_last_title_ms = now;                   /* no syscall per event: one timer, re-armed when it fires early */
        if (!g_title_armed) { g_title_armed = 1; SetTimer(g_wnd, T_TITLE, TITLE_SETTLE_MS, NULL); }
        return;
    }
    PROF(g_n_fg++;)
    SetTimer(g_wnd, T_EVAL, DEBOUNCE_MS, NULL);
}

#ifdef STC_PROFILE
static void write_profile(void)
{
    char buf[400];
    ULONG64 cycles = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &cycles);   /* exact, unlike the 15 ms scheduler tick */
    snprintf(buf, sizeof buf, "fg=%u title=%u raw=%u evals=%u beats=%u probes=%u eval_us=%I64d probe_us=%I64d cycles=%I64u startup_cycles=%I64u",
             g_n_fg, g_n_title, g_n_raw, g_n_eval, g_n_beat, g_n_probe, g_us_eval, g_us_probe, (unsigned long long)cycles, (unsigned long long)g_cycles0);
    stc_meta_set(g_db, "profile", buf);
}
#endif

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TIMER:
        if (wp == T_BEAT) {
            PROF(g_n_beat++;)
            eval(0);
            PROF(write_profile();)
            trim();
        } else if (wp == T_TITLE) {
            DWORD since = GetTickCount() - g_last_title_ms;
            if (since < TITLE_SETTLE_MS) {       /* still changing: wait out the rest of the quiet period */
                SetTimer(w, T_TITLE, TITLE_SETTLE_MS - since + 50, NULL);
                return 0;
            }
            KillTimer(w, T_TITLE);
            g_title_armed = 0;
            eval(1);
        } else {
            KillTimer(w, T_EVAL);
            eval(1);
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) g_locked = 1;
        else if (wp == WTS_SESSION_UNLOCK) g_locked = 0;
        else return 0;
        eval(1);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) { stc_tr_close(&g_tr, time(NULL)); g_suspended = 1; }
        else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            g_suspended = 0;
            SetTimer(w, T_EVAL, DEBOUNCE_MS, NULL);
        }
        return TRUE;
    case WM_ENDSESSION:
        if (wp) { stc_tr_close(&g_tr, time(NULL)); stc_tr_free(&g_tr); sqlite3_close(g_db); }
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
    g_beat_s = GetPrivateProfileIntW(L"tracking", L"heartbeat_seconds", 60, ini);
    if (g_beat_s < 5) g_beat_s = 5;
}

/* The text-services framework (MSCTF) is loaded for every GUI thread; this daemon has no text input. */
static void disable_ime(void)
{
    typedef BOOL (WINAPI *F)(DWORD);
    HMODULE imm = LoadLibraryW(L"imm32.dll");
    F f = imm ? (F)GetProcAddress(imm, "ImmDisableIME") : NULL;
    if (f) f(0xFFFFFFFF);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    HANDLE once, ev_stop, ev_pause, ev_resume, hs[3];
    WNDCLASSW wc;
    MSG msg;
    wchar_t nm[64];
    (void)prev; (void)cmd; (void)show;
    memset(&wc, 0, sizeof wc);

    stc_obj_name(nm, 64, L"Local\\screen-time-daemon");
    once = CreateMutexW(NULL, FALSE, nm);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    stc_obj_name(nm, 64, L"Local\\screen-time-stop");
    ev_stop = CreateEventW(NULL, FALSE, FALSE, nm);
    stc_obj_name(nm, 64, L"Local\\screen-time-pause");
    ev_pause = CreateEventW(NULL, FALSE, FALSE, nm);
    stc_obj_name(nm, 64, L"Local\\screen-time-resume");
    ev_resume = CreateEventW(NULL, FALSE, FALSE, nm);
    hs[0] = ev_stop; hs[1] = ev_pause; hs[2] = ev_resume;

    load_config();
    sqlite3_config(SQLITE_CONFIG_LOOKASIDE, 0, 0);      /* no per-connection scratch pool: saves ~120 KB */
    g_db = stc_db_open(NULL, 0);
    if (!g_db) return 1;
    stc_meta_set(g_db, "paused", "0");
    stc_meta_set(g_db, "daemon_version", STC_VERSION);
    stc_hb_recover(g_db, time(NULL));                   /* a crash left the open span's end_ts stale */
    stc_tr_init(&g_tr, g_db);
    if (!stc_hb_open(&g_hb)) g_tr.hb = &g_hb;
    PROF(QueryPerformanceFrequency(&g_qf);)

    disable_ime();
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
    trim();
    PROF(QueryProcessCycleTime(GetCurrentProcess(), &g_cycles0);)

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
    PROF(write_profile();)
    stc_tr_free(&g_tr);
    stc_hb_close(&g_hb);
    sqlite3_close(g_db);
    CloseHandle(once);
    return 0;
}
