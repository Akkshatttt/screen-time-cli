#include "stc.h"

void stc_tr_init(stc_tracker *t, sqlite3 *db)
{
    t->db = db;
    t->id = t->exe = t->title = 0;
    t->state = -1;
    t->min_extend = 0;
    t->last_write = t->start = 0;
    t->st_ext = t->st_ins = NULL;
    t->hb = NULL;
}

/* Statements are prepared once and reused: the daemon runs these for its whole life. */
static sqlite3_stmt *prep(stc_tracker *t, sqlite3_stmt **slot, const char *sql)
{
    if (!*slot && sqlite3_prepare_v2(t->db, sql, -1, slot, 0)) *slot = NULL;
    else if (*slot) sqlite3_reset(*slot);
    return *slot;
}

static void extend(stc_tracker *t, time_t now)
{
    sqlite3_stmt *s = prep(t, &t->st_ext, "UPDATE span SET end_ts=?1 WHERE id=?2");
    if (!s) return;
    sqlite3_bind_int64(s, 1, now);
    sqlite3_bind_int64(s, 2, t->id);
    sqlite3_step(s);
    sqlite3_reset(s);
}

int stc_tr_update(stc_tracker *t, time_t now, sqlite3_int64 exe, sqlite3_int64 title, int state)
{
    sqlite3_stmt *s;
    if (t->id && t->exe == exe && t->title == title && t->state == state &&
        now - t->start < STC_MAX_SPAN) {
        if (t->hb) stc_hb_write(t->hb, t->id, now);     /* 24-byte overwrite, no SQLite */
        else if (now - t->last_write >= t->min_extend) { extend(t, now); t->last_write = now; }
        return 0;
    }
    sqlite3_exec(t->db, "BEGIN", 0, 0, 0); /* one commit for close + open */
    if (t->id) extend(t, now);
    s = prep(t, &t->st_ins, "INSERT INTO span(start_ts,end_ts,exe_id,title_id,state) VALUES(?1,?1,?2,?3,?4)");
    if (s) {
        sqlite3_bind_int64(s, 1, now);
        sqlite3_bind_int64(s, 2, exe);
        sqlite3_bind_int64(s, 3, title);
        sqlite3_bind_int(s, 4, state);
        sqlite3_step(s);
        sqlite3_reset(s);
    }
    t->id = sqlite3_last_insert_rowid(t->db);
    sqlite3_exec(t->db, "COMMIT", 0, 0, 0);
    t->exe = exe;
    t->title = title;
    t->state = state;
    t->last_write = t->start = now;
    if (t->hb) stc_hb_write(t->hb, t->id, now);
    return 1;
}

void stc_tr_close(stc_tracker *t, time_t now)
{
    if (t->id) extend(t, now);
    t->id = 0;
    t->state = -1;
}

/* Call before sqlite3_close. */
void stc_tr_free(stc_tracker *t)
{
    sqlite3_finalize(t->st_ext);
    sqlite3_finalize(t->st_ins);
    t->st_ext = t->st_ins = NULL;
}
