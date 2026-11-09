/* Install tests run against a throw-away registry subtree and a temp dir, never the real keys. */
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <shlwapi.h>
#include "../src/core/stc.h"
#include "../src/install/install.h"

static int has_value(const wchar_t *key, const wchar_t *name)
{
    HKEY k;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k);
    if (rc) return 0;
    rc = RegQueryValueExW(k, name, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return rc == 0;
}

static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static DWORD fsize(const wchar_t *p)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    return GetFileAttributesExW(p, GetFileExInfoStandard, &d) ? d.nFileSizeLow : 0;
}

/* Makes dir with copies of both exes; the cli copy gets `extra` trailing bytes so it differs but still runs. */
static void make_build(const wchar_t *from, const wchar_t *dir, int extra)
{
    static const wchar_t *names[] = { L"screen-time-cli.exe", L"screen-time-daemon.exe" };
    int k;
    CreateDirectoryW(dir, NULL);
    for (k = 0; k < 2; k++) {
        wchar_t a[MAX_PATH], b[MAX_PATH];
        _snwprintf(a, MAX_PATH, L"%ls\\%ls", from, names[k]);
        _snwprintf(b, MAX_PATH, L"%ls\\%ls", dir, names[k]);
        assert(CopyFileW(a, b, FALSE));
        if (k == 0 && extra) {
            HANDLE h = CreateFileW(b, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
            DWORD w;
            int e;
            assert(h != INVALID_HANDLE_VALUE);
            for (e = 0; e < extra; e++) WriteFile(h, "x", 1, &w, NULL);
            CloseHandle(h);
        }
    }
}

int main(void)
{
    stc_inst in;
    wchar_t cwd[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], f[MAX_PATH];

    GetCurrentDirectoryW(MAX_PATH, cwd);
    _snwprintf(src, MAX_PATH, L"%ls\\build", cwd);
    _snwprintf(dst, MAX_PATH, L"%ls\\build\\inst_test", cwd);
    SetEnvironmentVariableW(L"STC_INSTALL_DIR", dst);
    SetEnvironmentVariableW(L"STC_REG_ROOT", L"Software\\screen-time-cli-test");
    stc_inst_defaults(&in);
    assert(!in.manage_daemon && !in.broadcast);

    assert(stc_install(&in, src) == 0);
    _snwprintf(f, MAX_PATH, L"%ls\\screen-time-cli.exe", dst);
    assert(exists(f));
    _snwprintf(f, MAX_PATH, L"%ls\\screen-time-daemon.exe", dst);
    assert(exists(f));
    assert(has_value(in.uninst, L"UninstallString"));
    assert(has_value(in.uninst, L"DisplayVersion"));
    assert(has_value(in.run, L"screen-time-cli"));
    assert(has_value(in.env, L"Path"));
    assert(stc_doctor(&in, 0, src) == 0);

    assert(stc_install(&in, src) == 0); /* idempotent: PATH must not duplicate */
    {
        wchar_t p[8192] = L"";
        DWORD n = sizeof p;
        HKEY k;
        RegOpenKeyExW(HKEY_CURRENT_USER, in.env, 0, KEY_QUERY_VALUE, &k);
        RegQueryValueExW(k, L"Path", NULL, NULL, (BYTE *)p, &n);
        RegCloseKey(k);
        assert(wcsstr(p, dst) && !wcsstr(wcsstr(p, dst) + 1, dst));
    }

    /* a cleaner removed the autostart entry: doctor sees it, --fix restores it */
    {
        HKEY k;
        RegOpenKeyExW(HKEY_CURRENT_USER, in.run, 0, KEY_SET_VALUE, &k);
        RegDeleteValueW(k, L"screen-time-cli");
        RegCloseKey(k);
    }
    assert(stc_doctor(&in, 0, src) == 1);
    assert(stc_doctor(&in, 1, src) == 1);
    assert(stc_doctor(&in, 0, src) == 0);

    /* ---- upgrade keeps data, replaces files, leaves no leftovers ---- */
    {
        wchar_t up1[MAX_PATH], up2[MAX_PATH], cli[MAX_PATH], dmn[MAX_PATH], data[MAX_PATH], marker[MAX_PATH];
        DWORD base_size;
        HANDLE lock, m;
        _snwprintf(up1, MAX_PATH, L"%ls\\build\\up_src1", cwd);
        _snwprintf(up2, MAX_PATH, L"%ls\\build\\up_src2", cwd);
        _snwprintf(cli, MAX_PATH, L"%ls\\screen-time-cli.exe", dst);
        _snwprintf(dmn, MAX_PATH, L"%ls\\screen-time-daemon.exe", dst);
        _snwprintf(data, MAX_PATH, L"%ls\\build\\data_test", cwd);
        _snwprintf(marker, MAX_PATH, L"%ls\\keep.txt", data);
        CreateDirectoryW(data, NULL);
        m = CreateFileW(marker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); /* stands in for the user's data */
        assert(m != INVALID_HANDLE_VALUE);
        CloseHandle(m);

        base_size = fsize(cli);
        make_build(src, up1, 7);                       /* "new release" */
        assert(stc_install(&in, up1) == 0);
        assert(fsize(cli) == base_size + 7);
        assert(exists(marker));                        /* data untouched by upgrade */
        {   wchar_t l[MAX_PATH];
            _snwprintf(l, MAX_PATH, L"%ls\\screen-time-cli.new.exe", dst); assert(!exists(l));
            _snwprintf(l, MAX_PATH, L"%ls\\screen-time-cli.exe.old", dst); assert(!exists(l)); }
        assert(stc_doctor(&in, 0, src) == 0);

        /* failed upgrade: the daemon exe is held open, so the swap fails after the cli was already swapped */
        make_build(src, up2, 13);
        lock = CreateFileW(dmn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        assert(lock != INVALID_HANDLE_VALUE);
        assert(stc_install(&in, up2) == -1);
        CloseHandle(lock);
        assert(fsize(cli) == base_size + 7);           /* rolled back to the previous release */
        assert(has_value(in.run, L"screen-time-cli"));
        assert(stc_doctor(&in, 0, src) == 0);          /* no staged or .old leftovers */
        assert(exists(marker));
        DeleteFileW(marker);
        RemoveDirectoryW(data);
    }

    assert(stc_uninstall(&in, 0) == 0);
    assert(!exists(dst));
    assert(!has_value(in.uninst, L"UninstallString"));
    assert(!has_value(in.run, L"screen-time-cli"));
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\screen-time-cli-test"); /* tidy the test subtree */
    puts("install tests passed");
    return 0;
}
