#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "stc.h"

int stc_data_dir(char *dir, size_t n)
{
    wchar_t w[MAX_PATH], full[MAX_PATH];
    /* STC_DATA_DIR overrides the location (tests, bench) */
    DWORD len = GetEnvironmentVariableW(L"STC_DATA_DIR", w, MAX_PATH);
    if (len && len < MAX_PATH) {
        wcscpy(full, w);
    } else {
        len = GetEnvironmentVariableW(L"LOCALAPPDATA", w, MAX_PATH);
        if (!len || len >= MAX_PATH - 20) return -1;
        _snwprintf(full, MAX_PATH, L"%ls\\screen-time-cli", w);
    }
    CreateDirectoryW(full, NULL);
    return WideCharToMultiByte(CP_UTF8, 0, full, -1, dir, (int)n, NULL, NULL) ? 0 : -1;
}

/* Ordered, additive migrations: MIGRATIONS[n] takes the schema from version n to n+1.
 * Never edit a shipped entry; append a new one and bump STC_SCHEMA (and add a fixture test). */
static const char *MIGRATIONS[] = {
    /* 1: base schema. Idempotent, so it also adopts databases created before versioning. */
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
    "CREATE TABLE IF NOT EXISTS exe(id INTEGER PRIMARY KEY, path TEXT UNIQUE, name TEXT);"
    "CREATE TABLE IF NOT EXISTS title(id INTEGER PRIMARY KEY, text TEXT UNIQUE);"
    "CREATE TABLE IF NOT EXISTS span(id INTEGER PRIMARY KEY, start_ts INTEGER NOT NULL,"
    " end_ts INTEGER NOT NULL, exe_id INTEGER NOT NULL, title_id INTEGER NOT NULL,"
    " state INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS span_start ON span(start_ts);",
};
typedef char migrations_match_schema[(sizeof MIGRATIONS / sizeof *MIGRATIONS == STC_SCHEMA) ? 1 : -1];

static char g_err[200];
const char *stc_db_error(void) { return g_err; }

int stc_schema_version(sqlite3 *db)
{
    sqlite3_stmt *s;
    int v = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &s, 0)) return 0;
    if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return v;
}

static int has_data_tables(sqlite3 *db)
{
    sqlite3_stmt *s;
    int n = 0;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master WHERE name='span'", -1, &s, 0)) return 0;
    if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return n > 0;
}

/* One consistent copy per old version, made before the first migration touches an existing database. */
static void backup_before_migrate(sqlite3 *db, const char *path, int from)
{
    char bak[MAX_PATH * 3 + 32], sql[MAX_PATH * 3 + 64];
    wchar_t w[MAX_PATH * 2];
    snprintf(bak, sizeof bak, "%s.bak-v%d", path, from);
    if (MultiByteToWideChar(CP_UTF8, 0, bak, -1, w, MAX_PATH * 2) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES)
        return;
    snprintf(sql, sizeof sql, "VACUUM INTO '%s'", bak); /* paths come from our own data dir; no quotes in them */
    sqlite3_exec(db, sql, 0, 0, 0);
}

int stc_migrate(sqlite3 *db, const char *path)
{
    int v = stc_schema_version(db), n;
    char sql[64];
    if (v > STC_SCHEMA) {
        snprintf(g_err, sizeof g_err, "database is schema v%d, this program understands v%d: "
                 "run install from the newer build", v, STC_SCHEMA);
        return 2;
    }
    if (v == STC_SCHEMA) return 0;
    if (path && has_data_tables(db)) backup_before_migrate(db, path, v);
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0)) return -1;
    for (n = v; n < STC_SCHEMA; n++) {
        snprintf(sql, sizeof sql, "PRAGMA user_version=%d", n + 1);
        if (sqlite3_exec(db, MIGRATIONS[n], 0, 0, 0) || sqlite3_exec(db, sql, 0, 0, 0)) {
            snprintf(g_err, sizeof g_err, "migration to v%d failed: %s", n + 1, sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
    }
    return sqlite3_exec(db, "COMMIT", 0, 0, 0) ? -1 : 0;
}

int stc_schema(sqlite3 *db) { return stc_migrate(db, NULL); }

sqlite3 *stc_db_open(const char *path, int readonly)
{
    char buf[MAX_PATH * 3];
    sqlite3 *db = NULL;
    g_err[0] = 0;
    if (!path) {
        if (stc_data_dir(buf, sizeof buf)) return NULL;
        strncat(buf, "\\data.db", sizeof buf - strlen(buf) - 1);
        path = buf;
    }
    if (sqlite3_open_v2(path, &db, readonly ? SQLITE_OPEN_READONLY
                                            : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    if (!readonly) {
        /* small cache: the daemon must stay tiny */
        sqlite3_exec(db, "PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;"
                         "PRAGMA cache_size=-256;PRAGMA temp_store=MEMORY;", 0, 0, 0);
        if (stc_migrate(db, path)) { sqlite3_close(db); return NULL; }
    } else {
        sqlite3_exec(db, "PRAGMA cache_size=-2048;", 0, 0, 0);
        if (stc_schema_version(db) > STC_SCHEMA) {
            stc_migrate(db, NULL); /* fills the message; refuses */
            sqlite3_close(db);
            return NULL;
        }
    }
    return db;
}

static sqlite3_int64 intern(sqlite3 *db, const char *ins, const char *sel,
                            const char *a, const char *b)
{
    sqlite3_stmt *s;
    sqlite3_int64 id = 0;
    if (sqlite3_prepare_v2(db, ins, -1, &s, 0)) return 0;
    sqlite3_bind_text(s, 1, a, -1, SQLITE_STATIC);
    if (b) sqlite3_bind_text(s, 2, b, -1, SQLITE_STATIC);
    sqlite3_step(s);
    sqlite3_finalize(s);
    if (sqlite3_prepare_v2(db, sel, -1, &s, 0)) return 0;
    sqlite3_bind_text(s, 1, a, -1, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return id;
}

sqlite3_int64 stc_intern_exe(sqlite3 *db, const char *path, const char *name)
{
    return intern(db, "INSERT OR IGNORE INTO exe(path,name) VALUES(?1,?2)",
                  "SELECT id FROM exe WHERE path=?1", path, name);
}

sqlite3_int64 stc_intern_title(sqlite3 *db, const char *text)
{
    return intern(db, "INSERT OR IGNORE INTO title(text) VALUES(?1)",
                  "SELECT id FROM title WHERE text=?1", text, NULL);
}

void stc_meta_set(sqlite3 *db, const char *key, const char *val)
{
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO meta VALUES(?1,?2)", -1, &s, 0)) return;
    sqlite3_bind_text(s, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, val, -1, SQLITE_STATIC);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

int stc_meta_get(sqlite3 *db, const char *key, char *out, size_t n)
{
    sqlite3_stmt *s;
    int ok = 0;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key=?1", -1, &s, 0)) return 0;
    sqlite3_bind_text(s, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) {
        snprintf(out, n, "%s", (const char *)sqlite3_column_text(s, 0));
        ok = 1;
    }
    sqlite3_finalize(s);
    return ok;
}
