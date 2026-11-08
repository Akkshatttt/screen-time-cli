/* Core tests: state classification, span writer, per-app query. In-memory DB, no Win32 needed. */
#include <assert.h>
#include <stdio.h>
#include "../src/core/stc.h"

static long g_a, g_p;
static int g_rows;
static void cb(void *c, const char *n, long a, long p) { (void)c; (void)n; g_a += a; g_p += p; g_rows++; }

int main(void)
{
    sqlite3 *db;
    stc_tracker t;
    sqlite3_int64 e1, e2, t1;

    /* classify(idle, media, locked, active_s=60, away_s=600) */
    assert(stc_classify(5, 0, 0, 60, 600) == STC_ACTIVE);
    assert(stc_classify(300, 0, 0, 60, 600) == STC_ACTIVE);   /* reading */
    assert(stc_classify(300, 1, 0, 60, 600) == STC_PASSIVE);  /* video */
    assert(stc_classify(5000, 1, 0, 60, 600) == STC_PASSIVE); /* long movie */
    assert(stc_classify(601, 0, 0, 60, 600) == STC_AWAY);
    assert(stc_classify(0, 1, 1, 60, 600) == STC_AWAY);       /* locked */

    sqlite3_open(":memory:", &db);
    assert(stc_schema(db) == 0);
    e1 = stc_intern_exe(db, "C:\\a.exe", "a.exe");
    e2 = stc_intern_exe(db, "C:\\b.exe", "b.exe");
    t1 = stc_intern_title(db, "t");
    assert(e1 && e2 && e1 != e2 && e1 == stc_intern_exe(db, "C:\\a.exe", "a.exe"));
    assert(t1 == stc_intern_title(db, "t"));

    stc_tr_init(&t, db);
    assert(stc_tr_update(&t, 1000, e1, t1, STC_ACTIVE) == 1);
    assert(stc_tr_update(&t, 1030, e1, t1, STC_ACTIVE) == 0);  /* heartbeat extends */
    assert(stc_tr_update(&t, 1100, e1, t1, STC_PASSIVE) == 1); /* state change */
    assert(stc_tr_update(&t, 1200, e2, t1, STC_ACTIVE) == 1);  /* app change */
    stc_tr_close(&t, 1300);

    stc_per_app(db, 0, 5000, cb, NULL);
    assert(g_rows == 2 && g_a == 100 + 100 && g_p == 100);     /* 1000-1100 a, 1100-1200 p, 1200-1300 b */
    g_a = g_p = g_rows = 0;
    stc_per_app(db, 1050, 1150, cb, NULL);                     /* clipped to range */
    assert(g_a == 50 && g_p == 50);

    /* long unchanged span rolls over so range queries can bound start_ts */
    stc_tr_init(&t, db);
    assert(stc_tr_update(&t, 10000, e1, t1, STC_ACTIVE) == 1);
    assert(stc_tr_update(&t, 10000 + STC_MAX_SPAN - 1, e1, t1, STC_ACTIVE) == 0);
    assert(stc_tr_update(&t, 10000 + STC_MAX_SPAN, e1, t1, STC_ACTIVE) == 1);

    sqlite3_close(db);
    puts("core tests passed");
    return 0;
}
