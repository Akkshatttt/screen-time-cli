/* User labels, category and topic assignments per exe, kept in labels.ini (owned and written by this program;
 * config.ini stays the user's). Sections: [labels] exe=Name, [category] exe=Category, [topic] exe=Topic. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "stc.h"

#define MAXL 128   /* ponytail: one 8 KB ini section read per table; raise if people label more than ~100 apps */

static stc_kv g_t[3][MAXL];
static int g_n[3], g_loaded;
static const char *SEC[3] = { "labels", "category", "topic" };

static void load(void)
{
    int i;
    if (g_loaded) return;
    for (i = 0; i < 3; i++) g_n[i] = stc_ini_section("labels.ini", SEC[i], g_t[i], MAXL);
    g_loaded = 1;
}

static const char *find(int sec, const char *exe)
{
    int i;
    load();
    for (i = 0; i < g_n[sec]; i++)
        if (!_stricmp(g_t[sec][i].key, exe) && g_t[sec][i].val[0]) return g_t[sec][i].val;
    return NULL;
}

const char *stc_label_get(const char *exe) { return find(0, exe); }
const char *stc_label_cat(const char *exe) { return find(1, exe); }
const char *stc_label_topic(const char *exe) { return find(2, exe); }
const char *stc_disp(const char *exe) { const char *l = find(0, exe); return l ? l : exe; }

int stc_label_cats(char names[][48], int n, int max)
{
    int i, j;
    load();
    for (i = 0; i < g_n[1] && n < max; i++) {
        for (j = 0; j < n && _stricmp(names[j], g_t[1][i].val); j++) ;
        if (j == n && g_t[1][i].val[0]) snprintf(names[n++], 48, "%s", g_t[1][i].val);
    }
    return n;
}

static int put(int sec, const char *exe, const char *val)
{
    wchar_t ini[MAX_PATH * 2], wsec[16], wk[96], wv[160];
    if (stc_ini_path("labels.ini", ini, MAX_PATH * 2)) return -1;
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) {   /* new file: UTF-16 BOM so non-ASCII labels survive */
        HANDLE f = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD n;
        if (f != INVALID_HANDLE_VALUE) { WriteFile(f, "\xff\xfe", 2, &n, NULL); CloseHandle(f); }
    }
    MultiByteToWideChar(CP_UTF8, 0, SEC[sec], -1, wsec, 16);
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, wk, 96);
    if (val && val[0]) MultiByteToWideChar(CP_UTF8, 0, val, -1, wv, 160);
    return WritePrivateProfileStringW(wsec, wk, val && val[0] ? wv : NULL, ini) ? 0 : -1;
}

/* NULL leaves a field as it is; "" removes it. Returns 0 on success. */
int stc_label_set(const char *exe, const char *label, const char *cat, const char *topic)
{
    int rc = 0;
    if (!exe || !exe[0]) return -1;
    if (label) rc |= put(0, exe, label);
    if (cat) rc |= put(1, exe, cat);
    if (topic) rc |= put(2, exe, topic);
    g_loaded = 0;
    return rc;
}
