/* screen-time-cli shared core: used by the daemon, the CLI and tests. */
#ifndef STC_H
#define STC_H

#include <time.h>
#include "../../vendor/sqlite3.h"

enum { STC_ACTIVE = 0, STC_PASSIVE = 1, STC_AWAY = 2 };

/* ---- state.c: pure classification (no Win32) ---- */
/* idle_s: seconds since last input. media: fullscreen or foreground app playing audio. */
int stc_classify(unsigned idle_s, int media, int locked, unsigned active_s, unsigned away_s);

/* ---- db.c ---- */
/* Fills dir with %LOCALAPPDATA%\screen-time-cli (UTF-8) and creates it. Returns 0 on success. */
int stc_data_dir(char *dir, size_t n);
/* readonly=1 for the CLI. path NULL = default location. Returns NULL on failure. */
sqlite3 *stc_db_open(const char *path, int readonly);
int stc_schema(sqlite3 *db);
sqlite3_int64 stc_intern_exe(sqlite3 *db, const char *path, const char *name);
sqlite3_int64 stc_intern_title(sqlite3 *db, const char *text);
void stc_meta_set(sqlite3 *db, const char *key, const char *val);
/* Returns 1 and fills out if key exists. */
int stc_meta_get(sqlite3 *db, const char *key, char *out, size_t n);

/* ---- span.c: writes a span only when (exe,title,state) changes; otherwise extends end_ts ---- */
/* Spans never exceed this, so range queries can bound start_ts and use its index. */
#define STC_MAX_SPAN (6 * 3600)

typedef struct {
    sqlite3 *db;
    sqlite3_int64 id, exe, title;
    int state;
    long min_extend;   /* seconds: skip heartbeat writes more often than this (0 = always write) */
    time_t last_write, start;
} stc_tracker;
void stc_tr_init(stc_tracker *t, sqlite3 *db);
/* Returns 1 if a new span was started, 0 if the open one was extended. */
int stc_tr_update(stc_tracker *t, time_t now, sqlite3_int64 exe, sqlite3_int64 title, int state);
void stc_tr_close(stc_tracker *t, time_t now);

/* ---- query.c ---- */
typedef void (*stc_app_cb)(void *ctx, const char *name, long active_s, long passive_s);
/* Per-app seconds in [from,to), busiest first. Away time is excluded. */
int stc_per_app(sqlite3 *db, time_t from, time_t to, stc_app_cb cb, void *ctx);
/* Seconds-based local midnight of the day containing t (+ days offset). */
time_t stc_day_start(time_t t, int days);

#endif
