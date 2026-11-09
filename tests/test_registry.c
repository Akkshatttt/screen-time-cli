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

    assert(stc_uninstall(&in, 0) == 0);
    assert(!exists(dst));
    assert(!has_value(in.uninst, L"UninstallString"));
    assert(!has_value(in.run, L"screen-time-cli"));
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\screen-time-cli-test"); /* tidy the test subtree */
    puts("install tests passed");
    return 0;
}
