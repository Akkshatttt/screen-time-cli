#include "stc.h"

time_t stc_day_start(time_t t, int days)
{
    struct tm tm = *localtime(&t);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_mday += days;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

int stc_per_app(sqlite3 *db, time_t from, time_t to, stc_app_cb cb, void *ctx)
{
    sqlite3_stmt *s;
    int rc = sqlite3_prepare_v2(db,
        "SELECT e.name,"
        " SUM(CASE WHEN d.state=0 THEN d.sec ELSE 0 END),"
        " SUM(CASE WHEN d.state=1 THEN d.sec ELSE 0 END)"
        " FROM (SELECT exe_id, state, MIN(end_ts,?2)-MAX(start_ts,?1) AS sec FROM span"
        "       WHERE start_ts>=?1-" STC_STR(STC_MAX_SPAN) " AND start_ts<?2 AND end_ts>?1 AND state<2) d"
        " JOIN exe e ON e.id=d.exe_id GROUP BY d.exe_id ORDER BY SUM(d.sec) DESC", -1, &s, 0);
    if (rc) return rc;
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    while (sqlite3_step(s) == SQLITE_ROW)
        cb(ctx, (const char *)sqlite3_column_text(s, 0),
           (long)sqlite3_column_int64(s, 1), (long)sqlite3_column_int64(s, 2));
    return sqlite3_finalize(s);
}
