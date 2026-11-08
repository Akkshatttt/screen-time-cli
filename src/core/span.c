#include "stc.h"

void stc_tr_init(stc_tracker *t, sqlite3 *db)
{
    t->db = db;
    t->id = t->exe = t->title = 0;
    t->state = -1;
    t->min_extend = 0;
    t->last_write = t->start = 0;
}

static void extend(stc_tracker *t, time_t now)
{
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(t->db, "UPDATE span SET end_ts=?1 WHERE id=?2", -1, &s, 0)) return;
    sqlite3_bind_int64(s, 1, now);
    sqlite3_bind_int64(s, 2, t->id);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

int stc_tr_update(stc_tracker *t, time_t now, sqlite3_int64 exe, sqlite3_int64 title, int state)
{
    sqlite3_stmt *s;
    if (t->id && t->exe == exe && t->title == title && t->state == state &&
        now - t->start < STC_MAX_SPAN) {
        if (now - t->last_write >= t->min_extend) { extend(t, now); t->last_write = now; }
        return 0;
    }
    sqlite3_exec(t->db, "BEGIN", 0, 0, 0); /* one commit for close + open */
    if (t->id) extend(t, now);
    if (!sqlite3_prepare_v2(t->db, "INSERT INTO span(start_ts,end_ts,exe_id,title_id,state)"
                                   " VALUES(?1,?1,?2,?3,?4)", -1, &s, 0)) {
        sqlite3_bind_int64(s, 1, now);
        sqlite3_bind_int64(s, 2, exe);
        sqlite3_bind_int64(s, 3, title);
        sqlite3_bind_int(s, 4, state);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    t->id = sqlite3_last_insert_rowid(t->db);
    sqlite3_exec(t->db, "COMMIT", 0, 0, 0);
    t->exe = exe;
    t->title = title;
    t->state = state;
    t->last_write = t->start = now;
    return 1;
}

void stc_tr_close(stc_tracker *t, time_t now)
{
    if (t->id) extend(t, now);
    t->id = 0;
    t->state = -1;
}
