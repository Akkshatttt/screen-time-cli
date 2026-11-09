#include "../core/wincompat.h"
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "install.h"

#define APP L"screen-time-cli"
#define CLI L"screen-time-cli.exe"
#define DMN L"screen-time-daemon.exe"
#define WIDE2(x) L##x
#define WIDE(x) WIDE2(x)

void stc_inst_defaults(stc_inst *i)
{
    wchar_t v[MAX_PATH], root[200];
    memset(i, 0, sizeof *i);
    i->manage_daemon = i->broadcast = 1;
    if (GetEnvironmentVariableW(L"STC_INSTALL_DIR", v, MAX_PATH)) wcscpy(i->dir, v);
    else if (GetEnvironmentVariableW(L"LOCALAPPDATA", v, MAX_PATH))
        _snwprintf(i->dir, MAX_PATH, L"%ls\\Programs\\" APP, v);
    if (GetEnvironmentVariableW(L"STC_REG_ROOT", root, 200)) {
        _snwprintf(i->uninst, 260, L"%ls\\Uninstall\\" APP, root);
        _snwprintf(i->run, 260, L"%ls\\Run", root);
        _snwprintf(i->env, 260, L"%ls\\Environment", root);
        i->manage_daemon = i->broadcast = 0;
    } else {
        wcscpy(i->uninst, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP);
        wcscpy(i->run, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        wcscpy(i->env, L"Environment");
    }
}

/* ---- daemon control ---- */
int stc_daemon_running(void)
{
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\screen-time-daemon");
    if (h) CloseHandle(h);
    return h != NULL;
}

void stc_daemon_stop(void)
{
    int n;
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\screen-time-stop");
    if (!h) return;
    SetEvent(h);
    CloseHandle(h);
    for (n = 0; n < 30 && stc_daemon_running(); n++) Sleep(100);
}

int stc_daemon_start(const wchar_t *dir)
{
    wchar_t exe[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (stc_daemon_running()) return 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    _snwprintf(exe, MAX_PATH, L"%ls\\" DMN, dir);
    if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

/* ---- registry helpers ---- */
static int reg_set(const wchar_t *key, const wchar_t *name, DWORD type, const void *data, DWORD n)
{
    HKEY k;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
    if (rc) return -1;
    rc = RegSetValueExW(k, name, 0, type, (const BYTE *)data, n);
    RegCloseKey(k);
    return rc ? -1 : 0;
}
static int reg_str(const wchar_t *key, const wchar_t *name, const wchar_t *v)
{
    return reg_set(key, name, REG_SZ, v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
}
static int reg_get(const wchar_t *key, const wchar_t *name, wchar_t *out, DWORD cap)
{
    HKEY k;
    DWORD n = (cap - 1) * sizeof(wchar_t), type;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k);
    if (rc) return 0;
    rc = RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(k);
    if (rc || (type != REG_SZ && type != REG_EXPAND_SZ)) return 0;
    out[n / sizeof(wchar_t)] = 0;
    return 1;
}
static void reg_del_value(const wchar_t *key, const wchar_t *name)
{
    HKEY k;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k)) { RegDeleteValueW(k, name); RegCloseKey(k); }
}

/* ---- PATH ---- */
#define PATH_CAP 8192
static int path_has(const wchar_t *path, const wchar_t *dir)
{
    size_t n = wcslen(dir);
    const wchar_t *p = path;
    while (*p) {
        const wchar_t *e = wcschr(p, L';');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (len == n && !_wcsnicmp(p, dir, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}
static void path_edit(const stc_inst *i, int add)
{
    static wchar_t cur[PATH_CAP], out[PATH_CAP];
    const wchar_t *p;
    size_t n = wcslen(i->dir);
    cur[0] = 0;
    reg_get(i->env, L"Path", cur, PATH_CAP - 300);
    if (add) {
        if (path_has(cur, i->dir)) return;
        if (cur[0] && cur[wcslen(cur) - 1] != L';') wcscat(cur, L";");
        wcscat(cur, i->dir);
        wcscpy(out, cur);
    } else {
        out[0] = 0;
        for (p = cur; *p;) {
            const wchar_t *e = wcschr(p, L';');
            size_t len = e ? (size_t)(e - p) : wcslen(p);
            if (len && !(len == n && !_wcsnicmp(p, i->dir, n))) {
                if (out[0]) wcscat(out, L";");
                wcsncat(out, p, len);
            }
            if (!e) break;
            p = e + 1;
        }
    }
    reg_set(i->env, L"Path", REG_EXPAND_SZ, out, (DWORD)((wcslen(out) + 1) * sizeof(wchar_t)));
    if (i->broadcast)
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, NULL);
}

/* ---- files ---- */
static void mkdirs(const wchar_t *dir)
{
    wchar_t t[MAX_PATH], *p;
    wcscpy(t, dir);
    for (p = t + 3; *p; p++)
        if (*p == L'\\') { *p = 0; CreateDirectoryW(t, NULL); *p = L'\\'; }
    CreateDirectoryW(t, NULL);
}

static DWORD file_kb(const wchar_t *dir, const wchar_t *name)
{
    wchar_t f[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA d;
    _snwprintf(f, MAX_PATH, L"%ls\\%ls", dir, name);
    return GetFileAttributesExW(f, GetFileExInfoStandard, &d) ? (d.nFileSizeLow + 1023) / 1024 : 0;
}

static void register_all(const stc_inst *i)
{
    wchar_t cli[MAX_PATH], dmn[MAX_PATH], q[MAX_PATH + 40];
    DWORD one = 1, kb = file_kb(i->dir, CLI) + file_kb(i->dir, DMN);
    _snwprintf(cli, MAX_PATH, L"%ls\\" CLI, i->dir);
    _snwprintf(dmn, MAX_PATH, L"%ls\\" DMN, i->dir);
    reg_str(i->uninst, L"DisplayName", APP);
    reg_str(i->uninst, L"DisplayVersion", WIDE(STC_VERSION));
    reg_str(i->uninst, L"Publisher", L"Akkshatttt");
    reg_str(i->uninst, L"InstallLocation", i->dir);
    reg_str(i->uninst, L"DisplayIcon", cli);
    _snwprintf(q, MAX_PATH + 40, L"\"%ls\" uninstall", cli);
    reg_str(i->uninst, L"UninstallString", q);
    reg_str(i->uninst, L"QuietUninstallString", q);
    reg_set(i->uninst, L"EstimatedSize", REG_DWORD, &kb, sizeof kb);
    reg_set(i->uninst, L"NoModify", REG_DWORD, &one, sizeof one);
    reg_set(i->uninst, L"NoRepair", REG_DWORD, &one, sizeof one);
    _snwprintf(q, MAX_PATH + 40, L"\"%ls\"", dmn);
    reg_str(i->run, APP, q);
    path_edit(i, 1);
}

int stc_install(const stc_inst *i, const wchar_t *src_dir)
{
    static const wchar_t *files[] = { CLI, DMN };
    int k;
    if (i->manage_daemon) stc_daemon_stop();
    mkdirs(i->dir);
    if (_wcsicmp(src_dir, i->dir))
        for (k = 0; k < 2; k++) {
            wchar_t a[MAX_PATH], b[MAX_PATH];
            _snwprintf(a, MAX_PATH, L"%ls\\%ls", src_dir, files[k]);
            _snwprintf(b, MAX_PATH, L"%ls\\%ls", i->dir, files[k]);
            if (!CopyFileW(a, b, FALSE)) {
                printf("copy failed: %ls (error %lu)\n", a, GetLastError());
                return -1;
            }
        }
    register_all(i);
    return 0;
}

static void run_hidden_cmd(const wchar_t *cmdline)
{
    wchar_t c[MAX_PATH * 3];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    _snwprintf(c, MAX_PATH * 3, L"cmd.exe /c %ls", cmdline);
    if (CreateProcessW(NULL, c, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static int leaf_is_app(const wchar_t *dir)
{
    const wchar_t *s = wcsrchr(dir, L'\\');
    return s && !_wcsicmp(s + 1, APP);
}

int stc_uninstall(const stc_inst *i, int purge)
{
    wchar_t f[MAX_PATH], cmd[MAX_PATH * 2];
    int stuck = 0, k;
    static const wchar_t *files[] = { CLI, DMN };
    if (i->manage_daemon) stc_daemon_stop();
    reg_del_value(i->run, APP);
    SHDeleteKeyW(HKEY_CURRENT_USER, i->uninst);
    path_edit(i, 0);
    for (k = 0; k < 2; k++) {
        _snwprintf(f, MAX_PATH, L"%ls\\%ls", i->dir, files[k]);
        if (!DeleteFileW(f) && GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES) stuck = 1;
    }
    if (!RemoveDirectoryW(i->dir) && GetFileAttributesW(i->dir) != INVALID_FILE_ATTRIBUTES) stuck = 1;
    if (stuck && leaf_is_app(i->dir)) { /* a running exe cannot delete itself: cmd finishes after we exit */
        _snwprintf(cmd, MAX_PATH * 2, L"ping -n 3 127.0.0.1 >nul & rmdir /s /q \"%ls\"", i->dir);
        run_hidden_cmd(cmd);
    }
    if (purge) {
        char d8[MAX_PATH * 3];
        wchar_t d[MAX_PATH];
        if (!stc_data_dir(d8, sizeof d8) && MultiByteToWideChar(CP_UTF8, 0, d8, -1, d, MAX_PATH) && leaf_is_app(d)) {
            _snwprintf(cmd, MAX_PATH * 2, L"ping -n 3 127.0.0.1 >nul & rmdir /s /q \"%ls\"", d);
            run_hidden_cmd(cmd);
        } else puts("purge skipped: data dir is not the default location");
    }
    return 0;
}

#define CHECK(ok, label, hint) do { if (ok) printf("  ok       %s\n", label); \
    else { printf("  PROBLEM  %s (%s)\n", label, hint); bad++; } } while (0)

int stc_doctor(const stc_inst *i, int fix, const wchar_t *src_dir)
{
    static wchar_t v[PATH_CAP], path[PATH_CAP];
    wchar_t f[MAX_PATH];
    int bad = 0, files_ok;
    printf("install dir: %ls\n", i->dir);
    _snwprintf(f, MAX_PATH, L"%ls\\" CLI, i->dir);
    files_ok = GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES;
    CHECK(files_ok, "screen-time-cli.exe present", "run install");
    _snwprintf(f, MAX_PATH, L"%ls\\" DMN, i->dir);
    files_ok = files_ok && GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES;
    CHECK(GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES, "screen-time-daemon.exe present", "run install");
    CHECK(reg_get(i->uninst, L"UninstallString", v, PATH_CAP), "Settings > Apps entry", "doctor --fix");
    CHECK(reg_get(i->run, APP, v, PATH_CAP), "autostart at logon (Run)", "doctor --fix");
    path[0] = 0;
    reg_get(i->env, L"Path", path, PATH_CAP);
    CHECK(path_has(path, i->dir), "install dir on user PATH", "doctor --fix");
    if (i->manage_daemon) {
        if (stc_daemon_running()) puts("  ok       daemon running");
        else puts("  note     daemon not running (screen-time-cli start)");
    }
    if (fix && bad) {
        if (files_ok) register_all(i);
        else stc_install(i, src_dir);
        puts("re-applied registration");
    }
    return bad;
}
