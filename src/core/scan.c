#include "stc.h"

/* Streams spans overlapping [from,to), clipped to it, in start order. One pass feeds every report. */
int stc_scan(sqlite3 *db, time_t from, time_t to, int want_title, stc_seg_cb cb, void *ctx)
{
    static const char *WITH =
        "SELECT s.start_ts,s.end_ts,s.state,s.exe_id,e.name,t.text,s.title_id FROM span s"
        " JOIN exe e ON e.id=s.exe_id JOIN title t ON t.id=s.title_id"
        " WHERE s.start_ts>=?1-" STC_STR(STC_MAX_SPAN) " AND s.start_ts<?2 AND s.end_ts>?1 ORDER BY s.start_ts";
    static const char *WITHOUT =
        "SELECT s.start_ts,s.end_ts,s.state,s.exe_id,e.name,NULL,s.title_id FROM span s"
        " JOIN exe e ON e.id=s.exe_id"
        " WHERE s.start_ts>=?1-" STC_STR(STC_MAX_SPAN) " AND s.start_ts<?2 AND s.end_ts>?1 ORDER BY s.start_ts";
    sqlite3_stmt *s;
    int rc = sqlite3_prepare_v2(db, want_title ? WITH : WITHOUT, -1, &s, 0);
    if (rc) return rc;
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    while (sqlite3_step(s) == SQLITE_ROW) {
        stc_seg g;
        g.start = (time_t)sqlite3_column_int64(s, 0);
        g.end = (time_t)sqlite3_column_int64(s, 1);
        if (g.start < from) g.start = from;
        if (g.end > to) g.end = to;
        if (g.end <= g.start) continue;
        g.state = sqlite3_column_int(s, 2);
        g.exe = sqlite3_column_int64(s, 3);
        g.name = (const char *)sqlite3_column_text(s, 4);
        g.title = (const char *)sqlite3_column_text(s, 5);
        g.title_id = sqlite3_column_int64(s, 6);
        cb(ctx, &g);
    }
    return sqlite3_finalize(s);
}
