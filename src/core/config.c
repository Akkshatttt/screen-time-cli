/* Reads user rules from config.ini in the data directory. The program never writes this file. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "stc.h"

/* Full path of an ini file in the data directory. */
int stc_ini_path(const char *file, wchar_t *out, size_t n)
{
    char dir[MAX_PATH * 3];
    wchar_t wdir[MAX_PATH], wfile[64];
    if (stc_data_dir(dir, sizeof dir)) return -1;
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, file, -1, wfile, 64);
    _snwprintf(out, n, L"%ls\\%ls", wdir, wfile);
    return 0;
}

int stc_ini_section(const char *file, const char *section, stc_kv *out, int max)
{
    wchar_t ini[MAX_PATH * 2], wsec[64], buf[8192];
    DWORD n, i;
    int count = 0;
    if (stc_ini_path(file, ini, MAX_PATH * 2)) return 0;
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsec, 64);
    n = GetPrivateProfileSectionW(wsec, buf, 8192, ini);
    for (i = 0; i < n && count < max;) {
        wchar_t *line = buf + i, *eq = wcschr(line, L'=');
        size_t len = wcslen(line);
        if (eq && eq != line) {
            *eq = 0;
            WideCharToMultiByte(CP_UTF8, 0, line, -1, out[count].key, sizeof out[count].key, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, eq + 1, -1, out[count].val, sizeof out[count].val, NULL, NULL);
            count++;
        }
        i += (DWORD)len + 1;
    }
    return count;
}

int stc_cfg_section(const char *section, stc_kv *out, int max) { return stc_ini_section("config.ini", section, out, max); }

/* Case-insensitive substring search. */
const char *stc_stristr(const char *h, const char *n)
{
    size_t ln = strlen(n);
    if (!ln) return h;
    for (; *h; h++) {
        size_t i = 0;
        while (i < ln && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++;
        if (i == ln) return h;
    }
    return NULL;
}

/* True if the item list ("a, b|c") contains word (whole item, case-insensitive). */
static int list_has(const char *list, const char *word)
{
    char item[128];
    const char *p = list;
    while (*p) {
        size_t n = strcspn(p, ",|");
        const char *a = p, *b = p + n;
        while (a < b && isspace((unsigned char)*a)) a++;
        while (b > a && isspace((unsigned char)b[-1])) b--;
        if ((size_t)(b - a) < sizeof item) {
            memcpy(item, a, (size_t)(b - a));
            item[b - a] = 0;
            if (!_stricmp(item, word)) return 1;
        }
        p += n;
        if (*p) p++;
    }
    return 0;
}

static const stc_kv DEFAULT_CATEGORIES[] = {
    { "Browsing", "chrome.exe,msedge.exe,firefox.exe,brave.exe,opera.exe,vivaldi.exe" },
    { "Development", "Code.exe,devenv.exe,idea64.exe,pycharm64.exe,WindowsTerminal.exe,cmd.exe,powershell.exe,pwsh.exe,wt.exe" },
    { "Communication", "Discord.exe,slack.exe,Teams.exe,ms-teams.exe,WhatsApp.exe,Telegram.exe,Zoom.exe,OUTLOOK.EXE" },
    { "Media", "vlc.exe,mpv.exe,Spotify.exe,wmplayer.exe,Music.UI.exe,Video.UI.exe" },
    { "Games", "steam.exe,steamwebhelper.exe,EpicGamesLauncher.exe,javaw.exe" },
    { "Office", "WINWORD.EXE,EXCEL.EXE,POWERPNT.EXE,ONENOTE.EXE,Notion.exe,Acrobat.exe" },
    { "System", "explorer.exe,SystemSettings.exe,taskmgr.exe,Taskmgr.exe" },
};

static stc_kv g_cat[32];
static int g_ncat = -1;

const char *stc_category(const char *exe_name)
{
    int i;
    const char *lc = stc_label_cat(exe_name);
    if (lc) return lc;
    if (g_ncat < 0) {
        g_ncat = stc_cfg_section("categories", g_cat, 32);
        if (!g_ncat) {
            g_ncat = (int)(sizeof DEFAULT_CATEGORIES / sizeof *DEFAULT_CATEGORIES);
            memcpy(g_cat, DEFAULT_CATEGORIES, sizeof DEFAULT_CATEGORIES);
        }
    }
    for (i = 0; i < g_ncat; i++)
        if (list_has(g_cat[i].val, exe_name)) return g_cat[i].key;
    return "Other";
}

/* Distinct category names: user-assigned ones, config/default ones, then "Other". Returns the count. */
int stc_category_names(char names[][48], int max)
{
    int i, n = 0;
    (void)stc_category("");   /* loads the table */
    for (i = 0; i < g_ncat && n < max; i++) snprintf(names[n++], 48, "%s", g_cat[i].key);
    n = stc_label_cats(names, n, max);
    if (n < max) snprintf(names[n++], 48, "Other");
    return n;
}

static const stc_kv DEFAULT_TOPICS[] = {
    { "YouTube", "youtube" }, { "GitHub", "github" }, { "Stack Overflow", "stack overflow" },
    { "Netflix", "netflix" }, { "Reddit", "reddit" }, { "Twitter / X", "twitter|/ x" },
    { "AI chat", "chatgpt|claude|gemini" }, { "Mail", "gmail|outlook|inbox" },
};

/* Returns the rule table (config [topics] or the built-in defaults). */
const stc_kv *stc_topics(int *n)
{
    static stc_kv t[32];
    static int cnt = -1;
    if (cnt < 0) {
        cnt = stc_cfg_section("topics", t, 32);
        if (!cnt) {
            cnt = (int)(sizeof DEFAULT_TOPICS / sizeof *DEFAULT_TOPICS);
            memcpy(t, DEFAULT_TOPICS, sizeof DEFAULT_TOPICS);
        }
    }
    *n = cnt;
    return t;
}

/* True when exe matches filter by file name or by user label (case-insensitive substring). */
int stc_app_match(const char *exe, const char *filter)
{
    const char *lb;
    if (stc_stristr(exe, filter)) return 1;
    lb = stc_label_get(exe);   /* ponytail: linear label scan per span, fine for a few dozen labels */
    return lb && stc_stristr(lb, filter) ? 1 : 0;
}

/* Index of the first topic whose keyword appears in title, or -1. */
int stc_topic_of(const char *title, const stc_kv *rules, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char kw[200];
        char *tok;
        strncpy(kw, rules[i].val, sizeof kw - 1);
        kw[sizeof kw - 1] = 0;
        for (tok = strtok(kw, ",|"); tok; tok = strtok(NULL, ",|")) {
            while (*tok == ' ') tok++;
            if (*tok && stc_stristr(title, tok)) return i;
        }
    }
    return -1;
}
