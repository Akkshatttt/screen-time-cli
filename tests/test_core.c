/* Core tests: state classification, span writer, per-app query. In-memory DB, no Win32 needed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/core/stc.h"

static long g_a, g_p;
static int g_rows;
static void cb(void *c, const char *n, long a, long p) { (void)c; (void)n; g_a += a; g_p += p; g_rows++; }

static void exec_file(sqlite3 *db, const char *file)
{
    char buf[4096];
    size_t n;
    FILE *f = fopen(file, "rb");
    assert(f);
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    assert(sqlite3_exec(db, buf, 0, 0, 0) == 0);
}

/* A database written by an older release is migrated in place, backed up first, and a newer one is refused. */
static void test_migrate(void)
{
    const char *path = "build/mig_test.db";
    sqlite3 *db;
    sqlite3_stmt *s;
    int rows = 0;

    remove(path); remove("build/mig_test.db-wal"); remove("build/mig_test.db-shm"); remove("build/mig_test.db.bak-v0");
    assert(sqlite3_open(path, &db) == 0);
    exec_file(db, "tests/fixtures/schema_v0.sql");
    assert(stc_schema_version(db) == 0);
    sqlite3_close(db);

    db = stc_db_open(path, 0);
    assert(db && stc_schema_version(db) == STC_SCHEMA);
    sqlite3_prepare_v2(db, "SELECT count(*) FROM span", -1, &s, 0);
    sqlite3_step(s); rows = sqlite3_column_int(s, 0); sqlite3_finalize(s);
    assert(rows == 2);                                       /* data intact */
    sqlite3_close(db);
    {   FILE *b = fopen("build/mig_test.db.bak-v0", "rb");   /* backup of the old version exists */
        assert(b); fclose(b); }

    db = stc_db_open(path, 1);                               /* reopening is a no-op */
    assert(db && stc_schema_version(db) == STC_SCHEMA);
    sqlite3_close(db);

    assert(sqlite3_open(path, &db) == 0);                    /* pretend a future release wrote it */
    sqlite3_exec(db, "PRAGMA user_version=99", 0, 0, 0);
    sqlite3_close(db);
    assert(stc_db_open(path, 0) == NULL && strstr(stc_db_error(), "newer build"));
    assert(stc_db_open(path, 1) == NULL && strstr(stc_db_error(), "newer build"));
}

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
    assert(stc_ver_cmp("0.2.1", "0.3.0") < 0 && stc_ver_cmp("v1.0.0", "0.9.9") > 0);
    assert(stc_ver_cmp("0.10.0", "0.9.0") > 0 && stc_ver_cmp("v0.2.1", "0.2.1") == 0);
    test_migrate();
    puts("core tests passed");
    return 0;
}
