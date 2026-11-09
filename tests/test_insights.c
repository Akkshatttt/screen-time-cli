/* Render primitives (ASCII mode) and the numeric core of each insight, on hand-built spans in memory. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "../src/cli/insights/ins.h"
#include "../src/cli/render.h"
#include "../src/cli/live.h"

static time_t at(int y, int mo, int d, int h, int mi, int s)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static sqlite3 *mkdb(void)
{
    sqlite3 *db;
    sqlite3_open(":memory:", &db);
    assert(stc_schema(db) == 0);
    stc_intern_exe(db, "C:\\a.exe", "a.exe");   /* ids 1..4 */
    stc_intern_exe(db, "C:\\b.exe", "b.exe");
    stc_intern_exe(db, "C:\\c.exe", "c.exe");
    stc_intern_exe(db, "C:\\d.exe", "d.exe");
    stc_intern_title(db, "t");
    return db;
}

static void add(sqlite3 *db, time_t s, time_t e, int exe, int state)
{
    char sql[160];
    snprintf(sql, sizeof sql, "INSERT INTO span(start_ts,end_ts,exe_id,title_id,state) VALUES(%I64d,%I64d,%d,1,%d)",
             (long long)s, (long long)e, exe, state);
    assert(sqlite3_exec(db, sql, 0, 0, 0) == 0);
}

static void test_render(void)
{
    char b[512];
    long v[2] = { 0, 10 };
    rd_set(0, 0, 80);
    assert(!strcmp(rd_dur(b, sizeof b, 3725), "1h 02m"));
    assert(!strcmp(rd_dur(b, sizeof b, 45 * 60), "45m"));
    assert(!strcmp(rd_dur(b, sizeof b, 59), "59s"));
    assert(!strcmp(rd_bar(b, sizeof b, 1.0, 0, 10), "##########"));
    assert(!strcmp(rd_bar(b, sizeof b, 0.5, 0.25, 8), "####++.."));
    assert(!strcmp(rd_bar(b, sizeof b, 0, 0, 4), "...."));
    assert(!strcmp(rd_spark(b, sizeof b, v, 2), ".@"));
    assert(!strcmp(rd_trend(b, sizeof b, 120, 100), "^ +20%"));
    assert(!strcmp(rd_trend(b, sizeof b, 80, 100), "v -20%"));
    assert(!strcmp(rd_trend(b, sizeof b, 100, 100), "-"));
    assert(!strcmp(rd_trend(b, sizeof b, 5, 0), "new"));
    assert(!strcmp(rd_trunc(b, sizeof b, "abcdefghij", 6), "abcde~"));
    assert(!strcmp(rd_trunc(b, sizeof b, "abc", 6), "abc"));
    assert(!strcmp(rd_heat(b, sizeof b, 2), "::"));
    rd_set(1, 1, 80);                                   /* color + unicode: escapes present, bar still sized */
    assert(strstr(rd_bar(b, sizeof b, 1.0, 0, 3), "\xe2\x96\x88") && strstr(b, "\x1b["));
    rd_set(0, 0, 80);
}

static void test_profile(void)
{
    sqlite3 *db = mkdb();
    profile p;
    time_t d0 = at(2026, 1, 5, 0, 0, 0);               /* a Monday */
    add(db, at(2026, 1, 5, 9, 30, 0), at(2026, 1, 5, 10, 30, 0), 1, STC_ACTIVE);       /* crosses an hour */
    add(db, at(2026, 1, 5, 23, 30, 0), at(2026, 1, 6, 0, 30, 0), 2, STC_PASSIVE);      /* crosses midnight */
    add(db, at(2026, 1, 6, 2, 0, 0), at(2026, 1, 6, 3, 0, 0), 3, STC_AWAY);            /* away is ignored */
    profile_compute(db, d0, d0 + 2 * 86400, NULL, &p);
    assert(p.ndays >= 2);
    assert(p.act == 3600 && p.pas == 3600);
    assert(p.hm[0][9] == 1800 && p.hm[0][10] == 1800);  /* Monday 09 and 10 */
    assert(p.hm[0][23] == 1800 && p.hm[1][0] == 1800);  /* Mon 23, Tue 00 */
    assert(p.day_act[0] == 3600 && p.day_pas[0] == 1800 && p.day_pas[1] == 1800);
    profile_sort(&p);
    assert(p.napps == 2 && p.apps[0].act + p.apps[0].pas >= p.apps[1].act + p.apps[1].pas);
    profile_free(&p);
    profile_compute(db, d0, d0 + 2 * 86400, "b.exe", &p);   /* --app filter */
    assert(p.act == 0 && p.pas == 3600);
    profile_free(&p);
    sqlite3_close(db);
}

static void test_focus(void)
{
    sqlite3 *db = mkdb();
    focus_result r;
    add(db, at(2026, 1, 5, 9, 0, 0), at(2026, 1, 5, 9, 40, 0), 1, STC_ACTIVE);
    add(db, at(2026, 1, 5, 9, 40, 0), at(2026, 1, 5, 9, 40, 30), 2, STC_ACTIVE);       /* 30 s glance: block continues */
    add(db, at(2026, 1, 5, 9, 40, 30), at(2026, 1, 5, 10, 10, 0), 1, STC_ACTIVE);
    add(db, at(2026, 1, 5, 10, 30, 0), at(2026, 1, 5, 10, 50, 0), 3, STC_ACTIVE);      /* after a 20 min break */
    focus_compute(db, at(2026, 1, 5, 0, 0, 0), at(2026, 1, 6, 0, 0, 0), NULL, &r);
    assert(r.sessions == 2 && r.breaks == 1 && r.total_break_s == 1200);
    assert(r.longest_session_s == 4200 && r.total_session_s == 5400);
    assert(r.focus_s == 4200);                          /* only the a.exe block is >= 25 min */
    assert(r.nblocks == 2 && r.blocks[0].dur == 4200 && r.blocks[1].dur == 1200);
    assert(!strcmp(r.blocks[0].name, "a.exe"));
    sqlite3_close(db);
}

static void test_switches(void)
{
    sqlite3 *db = mkdb();
    switch_result r;
    add(db, at(2026, 1, 5, 9, 0, 0), at(2026, 1, 5, 9, 10, 0), 1, STC_ACTIVE);
    add(db, at(2026, 1, 5, 9, 10, 0), at(2026, 1, 5, 9, 20, 0), 2, STC_ACTIVE);
    add(db, at(2026, 1, 5, 9, 20, 0), at(2026, 1, 5, 9, 30, 0), 1, STC_ACTIVE);
    add(db, at(2026, 1, 5, 9, 30, 0), at(2026, 1, 5, 9, 40, 0), 2, STC_ACTIVE);
    add(db, at(2026, 1, 5, 11, 0, 0), at(2026, 1, 5, 11, 10, 0), 4, STC_ACTIVE);       /* an hour later: not a switch */
    switches_compute(db, at(2026, 1, 5, 0, 0, 0), at(2026, 1, 6, 0, 0, 0), &r);
    assert(r.switches == 3 && r.by_hour[9] == 3);
    assert(r.active_s == 2400 + 600);
    assert(r.npairs == 2 && r.pairs[0].n == 2 && !strcmp(r.pairs[0].from, "a.exe") && !strcmp(r.pairs[0].to, "b.exe"));
    sqlite3_close(db);
}

static long g_seg_end;
static void grab_end(void *ctx, const stc_seg *s) { (void)ctx; g_seg_end = (long)s->end; }

/* The daemon heartbeats into hb.bin instead of the database: readers overlay it, a restart applies it. */
static void test_heartbeat_file(void)
{
    sqlite3 *db = mkdb();
    stc_hb hb;
    sqlite3_int64 id = 0;
    time_t ts = 0;
    HANDLE f;
    DWORD n;
    CreateDirectoryW(L"build\\hb_test", NULL);
    SetEnvironmentVariableW(L"STC_DATA_DIR", L"build\\hb_test");
    DeleteFileW(L"build\\hb_test\\hb.bin");
    assert(!stc_hb_read(&id, &ts));                         /* no file yet */
    add(db, 1000, 1000, 1, STC_ACTIVE);                     /* open span: end == start */
    assert(stc_hb_open(&hb) == 0);
    stc_hb_write(&hb, 1, 1500);
    assert(stc_hb_read(&id, &ts) && id == 1 && ts == 1500);
    g_seg_end = 0;
    stc_scan(db, 0, 5000, 0, grab_end, NULL);
    assert(g_seg_end == 1500);                              /* reports see the live end of the open span */
    assert(stc_hb_recover(db, 400) == 0);                   /* heartbeat from the future is ignored */
    assert(stc_hb_recover(db, 2000) == 1);                  /* after a crash it is written back */
    g_seg_end = 0;
    stc_hb_close(&hb);
    DeleteFileW(L"build\\hb_test\\hb.bin");
    stc_scan(db, 0, 5000, 0, grab_end, NULL);
    assert(g_seg_end == 1500);                              /* ... and survives losing the file */
    f = CreateFileW(L"build\\hb_test\\hb.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, "garbage garbage garbage garbage", 31, &n, NULL);
    CloseHandle(f);
    assert(!stc_hb_read(&id, &ts));                         /* torn or foreign file is rejected */
    DeleteFileW(L"build\\hb_test\\hb.bin");
    SetEnvironmentVariableW(L"STC_DATA_DIR", NULL);
    sqlite3_close(db);
}

static void test_rules_and_args(void)
{
    int n;
    const stc_kv *rules;
    stc_args a;
    char *argv[] = { "x", "report", "--days", "3", "--app", "chr", "--json", "--by", "category" };
    SetEnvironmentVariableW(L"STC_DATA_DIR", L"build\\cfg_test");   /* no config.ini there: built-in defaults */
    assert(!strcmp(stc_category("chrome.exe"), "Browsing") && !strcmp(stc_category("CHROME.EXE"), "Browsing"));
    assert(!strcmp(stc_category("unknown.exe"), "Other"));
    rules = stc_topics(&n);
    assert(n > 0);
    {   int t = stc_topic_of("Cats - YouTube - Google Chrome", rules, n);
        assert(t >= 0 && !strcmp(rules[t].key, "YouTube")); }
    assert(stc_topic_of("Untitled - Notepad", rules, n) < 0);

    assert(args_parse(9, argv, 2, 1, &a) == 0);
    assert(a.days == 3 && a.json && a.by_cat && !strcmp(a.app, "chr") && a.to > a.from);
    assert(args_parse(2, argv, 2, 7, &a) == 0 && a.days == 7);
}

static void test_live_helpers(void)
{
    char line[] = "  report   --days 14\t--app chrome ";
    char *tok[16];
    int i, n = live_split(line, tok, 16);
    assert(n == 5 && !strcmp(tok[0], "report") && !strcmp(tok[1], "--days") && !strcmp(tok[4], "chrome"));
    { char one[] = "a b c d"; assert(live_split(one, tok, 2) == 2); }
    { char none[] = "   "; assert(live_split(none, tok, 16) == 0); }
    assert(!strcmp(live_key_view('t'), "today") && !strcmp(live_key_view('r'), "report"));
    assert(live_key_view('z') == NULL && live_key_view(':') == NULL && live_key_view('q') == NULL);
    /* every hotkey names a real insight */
    for (i = 0; i < 128; i++) {
        const char *v = live_key_view(i);
        int j;
        if (!v) continue;
        for (j = 0; j < N_INSIGHTS && strcmp(INSIGHTS[j].name, v); j++) ;
        assert(j < N_INSIGHTS);
    }
}

int main(void)
{
    test_live_helpers();
    test_render();
    test_profile();
    test_focus();
    test_switches();
    test_heartbeat_file();
    test_rules_and_args();
    puts("insight tests passed");
    return 0;
}
