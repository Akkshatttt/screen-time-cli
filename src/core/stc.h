/* screen-time-cli shared core: used by the daemon, the CLI and tests. */
#ifndef STC_H
#define STC_H

#include "version.h"
#define STC_SCHEMA 1   /* database schema version (PRAGMA user_version); bump with a migration in db.c */

#include <time.h>
#include <stddef.h>
#include "../../vendor/sqlite3.h"

enum { STC_ACTIVE = 0, STC_PASSIVE = 1, STC_AWAY = 2 };

/* ---- state.c: pure classification (no Win32) ---- */
/* idle_s: seconds since last input. media: fullscreen or foreground app playing audio. */
int stc_classify(unsigned idle_s, int media, int locked, unsigned active_s, unsigned away_s);

int stc_ver_cmp(const char *a, const char *b);   /* "0.10.0" > "0.9.0"; leading v ignored */

/* ---- sched.c: daemon scheduling rules, pure so they are testable ---- */
int stc_probe_due(time_t now_s, time_t last_probe_s, unsigned beat_s);

/* ---- db.c ---- */
/* Names of the daemon's named kernel objects. STC_NS (env) appends a suffix so a test daemon can run beside the real one. */
void stc_obj_name(wchar_t *out, size_t cap, const wchar_t *base);
/* Fills dir with %LOCALAPPDATA%\screen-time-cli (UTF-8) and creates it. Returns 0 on success. */
int stc_data_dir(char *dir, size_t n);
/* readonly=1 for the CLI. path NULL = default location. Returns NULL on failure. */
sqlite3 *stc_db_open(const char *path, int readonly);
int stc_schema(sqlite3 *db);                 /* migrate without backup (tests, in-memory) */
int stc_schema_version(sqlite3 *db);
/* Brings db up to STC_SCHEMA. path (optional) enables a VACUUM INTO backup before migrating existing data.
 * Returns 0 ok, 2 if the database is newer than this program, -1 on failure; see stc_db_error(). */
int stc_migrate(sqlite3 *db, const char *path);
const char *stc_db_error(void);              /* why the last stc_db_open returned NULL, or "" */
sqlite3_int64 stc_intern_exe(sqlite3 *db, const char *path, const char *name);
sqlite3_int64 stc_intern_title(sqlite3 *db, const char *text);
void stc_meta_set(sqlite3 *db, const char *key, const char *val);
/* Returns 1 and fills out if key exists. */
int stc_meta_get(sqlite3 *db, const char *key, char *out, size_t n);

/* ---- hb.c: heartbeat file (open span still alive as of T), overlaid by readers, applied after a crash ---- */
typedef struct { void *h; } stc_hb;
int stc_hb_open(stc_hb *h);                                   /* daemon: open for writing; -1 if unavailable */
void stc_hb_write(stc_hb *h, sqlite3_int64 span, time_t ts);
void stc_hb_close(stc_hb *h);
int stc_hb_read(sqlite3_int64 *span, time_t *ts);            /* 1 if an intact record exists */
int stc_hb_recover(sqlite3 *db, time_t now);                  /* apply it to the database; returns rows changed */

/* ---- span.c: writes a span only when (exe,title,state) changes; otherwise extends end_ts ---- */
/* Spans never exceed this, so range queries can bound start_ts and use its index. */
#define STC_MAX_SPAN (6 * 3600)

typedef struct {
    sqlite3 *db;
    sqlite3_int64 id, exe, title;
    int state;
    long min_extend;   /* seconds: skip heartbeat writes more often than this (0 = always write) */
    time_t last_write, start;
    sqlite3_stmt *st_ext, *st_ins;   /* prepared once, reused */
    stc_hb *hb;                      /* optional: heartbeats go here instead of into the database */
} stc_tracker;
void stc_tr_init(stc_tracker *t, sqlite3 *db);
/* Returns 1 if a new span was started, 0 if the open one was extended. */
int stc_tr_update(stc_tracker *t, time_t now, sqlite3_int64 exe, sqlite3_int64 title, int state);
void stc_tr_close(stc_tracker *t, time_t now);
void stc_tr_free(stc_tracker *t);   /* finalize cached statements; call before sqlite3_close */

/* ---- query.c ---- */
typedef void (*stc_app_cb)(void *ctx, const char *name, long active_s, long passive_s);
/* Per-app seconds in [from,to), busiest first. Away time is excluded. */
int stc_per_app(sqlite3 *db, time_t from, time_t to, stc_app_cb cb, void *ctx);
/* Seconds-based local midnight of the day containing t (+ days offset). */
time_t stc_day_start(time_t t, int days);


/* ---- scan.c: one streaming pass over spans, shared by every report ---- */
#define STC_STR2(x) #x
#define STC_STR(x) STC_STR2(x)
typedef struct {
    time_t start, end;      /* clipped to the scanned range */
    int state;              /* STC_ACTIVE / STC_PASSIVE / STC_AWAY */
    sqlite3_int64 exe, title_id;
    const char *name;       /* exe file name */
    const char *title;      /* NULL unless want_title */
} stc_seg;
typedef void (*stc_seg_cb)(void *ctx, const stc_seg *s);
int stc_scan(sqlite3 *db, time_t from, time_t to, int want_title, stc_seg_cb cb, void *ctx);

/* ---- config.c: user rules in config.ini (read only) ---- */
typedef struct { char key[48]; char val[200]; } stc_kv;
int stc_cfg_section(const char *section, stc_kv *out, int max);
const char *stc_stristr(const char *haystack, const char *needle);
const char *stc_category(const char *exe_name);          /* config [categories] or built-in defaults; "Other" */
const stc_kv *stc_topics(int *n);                         /* config [topics] or built-in defaults */
int stc_topic_of(const char *title, const stc_kv *rules, int n);

#endif
