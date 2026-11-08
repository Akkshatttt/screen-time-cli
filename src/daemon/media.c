/* Media signals for "passive" time: fullscreen window, or foreground app playing audio.
 * MinGW 6.3 ships no WASAPI headers, so the few COM vtable slots we use are declared by hand.
 * Only called when the user has been idle, so the COM cost is paid rarely. */
#include "../core/wincompat.h"
#include <string.h>
#include "media.h"

static const GUID CLSID_Enum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID IID_Enum   = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID IID_Mgr2   = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
static const GUID IID_Meter  = {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};
static const GUID IID_Ctl2   = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};

/* Generic COM object: IUnknown slots + numbered method slots. */
typedef struct Vt {
    HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    void *m[16];
} Vt;
typedef struct { Vt *v; } Obj;
#define REL(o) do { if (o) (o)->v->Release(o); } while (0)

typedef HRESULT (STDMETHODCALLTYPE *F_defep)(void *, int, int, Obj **);
typedef HRESULT (STDMETHODCALLTYPE *F_activate)(void *, REFIID, DWORD, void *, void **);
typedef HRESULT (STDMETHODCALLTYPE *F_getenum)(void *, Obj **);
typedef HRESULT (STDMETHODCALLTYPE *F_count)(void *, int *);
typedef HRESULT (STDMETHODCALLTYPE *F_getsess)(void *, int, Obj **);
typedef HRESULT (STDMETHODCALLTYPE *F_getpid)(void *, DWORD *);
typedef HRESULT (STDMETHODCALLTYPE *F_peak)(void *, float *);

static int g_com;

static int same_exe(DWORD pid, const wchar_t *fg)
{
    wchar_t p[MAX_PATH];
    DWORD n = MAX_PATH;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    int same = 0;
    if (!h) return 0;
    if (QueryFullProcessImageNameW(h, 0, p, &n)) same = !_wcsicmp(p, fg);
    CloseHandle(h);
    return same;
}

int stc_audio_playing(const wchar_t *fg_exe)
{
    Obj *en = NULL, *dev = NULL, *mgr = NULL, *se = NULL;
    int count = 0, i, playing = 0;

    if (!g_com) { CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); g_com = 1; }
    if (CoCreateInstance(&CLSID_Enum, NULL, CLSCTX_ALL, &IID_Enum, (void **)&en)) return 0;
    if (((F_defep)en->v->m[1])(en, 0 /*eRender*/, 1 /*eMultimedia*/, &dev)) goto out;
    if (((F_activate)dev->v->m[0])(dev, &IID_Mgr2, CLSCTX_ALL, NULL, (void **)&mgr)) goto out;
    if (((F_getenum)mgr->v->m[2])(mgr, &se)) goto out;
    ((F_count)se->v->m[0])(se, &count);
    for (i = 0; i < count && !playing; i++) {
        Obj *c = NULL, *c2 = NULL, *meter = NULL;
        DWORD pid = 0;
        float peak = 0;
        if (((F_getsess)se->v->m[1])(se, i, &c)) continue;
        if (!c->v->QI(c, &IID_Ctl2, (void **)&c2) && !c->v->QI(c, &IID_Meter, (void **)&meter)) {
            ((F_getpid)c2->v->m[11])(c2, &pid);
            ((F_peak)meter->v->m[0])(meter, &peak);
            if (peak > 0.0005f && pid && same_exe(pid, fg_exe)) playing = 1;
        }
        REL(meter); REL(c2); REL(c);
    }
out:
    REL(se); REL(mgr); REL(dev); REL(en);
    return playing;
}

int stc_fullscreen(HWND h)
{
    RECT r;
    MONITORINFO mi;
    wchar_t cls[32];
    if (!h || h == GetDesktopWindow() || h == GetShellWindow()) return 0;
    if (GetClassNameW(h, cls, 32) && (!wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Progman"))) return 0;
    mi.cbSize = sizeof mi;
    if (!GetWindowRect(h, &r) || !GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}
