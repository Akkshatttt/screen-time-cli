/* Heartbeat file: the daemon records "span N is still open as of time T" here instead of updating the database.
 * One 24-byte overwrite replaces a SQLite transaction (WAL frame + later checkpoint) every beat. The CLI overlays it on
 * the open span so reports are live, and the daemon applies it to the database after a crash. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "stc.h"

#define HB_MAGIC 0x48435453u   /* "STCH" */

typedef struct { unsigned magic; unsigned check; long long span; long long ts; } hb_rec;

static unsigned check_of(const hb_rec *r) { return (unsigned)(r->span ^ (r->ts * 2654435761LL)) ^ 0x5bd1e995u; }

static int hb_path(wchar_t *out, size_t cap)
{
    char d8[MAX_PATH * 3];
    wchar_t d[MAX_PATH];
    if (stc_data_dir(d8, sizeof d8) || !MultiByteToWideChar(CP_UTF8, 0, d8, -1, d, MAX_PATH)) return -1;
    _snwprintf(out, cap, L"%ls\\hb.bin", d);
    return 0;
}

int stc_hb_open(stc_hb *h)
{
    wchar_t p[MAX_PATH + 16];
    h->h = INVALID_HANDLE_VALUE;
    if (hb_path(p, MAX_PATH + 16)) return -1;
    h->h = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    return h->h == INVALID_HANDLE_VALUE ? -1 : 0;
}

void stc_hb_write(stc_hb *h, sqlite3_int64 span, time_t ts)
{
    hb_rec r;
    DWORD n;
    if (h->h == INVALID_HANDLE_VALUE) return;
    r.magic = HB_MAGIC;
    r.span = span;
    r.ts = ts;
    r.check = check_of(&r);
    SetFilePointer(h->h, 0, NULL, FILE_BEGIN);
    WriteFile(h->h, &r, sizeof r, &n, NULL);
}

void stc_hb_close(stc_hb *h)
{
    if (h->h != INVALID_HANDLE_VALUE) CloseHandle(h->h);
    h->h = INVALID_HANDLE_VALUE;
}

/* Returns 1 and fills span/ts when the file holds an intact record. */
int stc_hb_read(sqlite3_int64 *span, time_t *ts)
{
    wchar_t p[MAX_PATH + 16];
    hb_rec r;
    DWORD n = 0;
    HANDLE f;
    int ok = 0;
    if (hb_path(p, MAX_PATH + 16)) return 0;
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (ReadFile(f, &r, sizeof r, &n, NULL) && n == sizeof r && r.magic == HB_MAGIC && r.check == check_of(&r)) {
        *span = r.span;
        *ts = (time_t)r.ts;
        ok = 1;
    }
    CloseHandle(f);
    return ok;
}

/* After a crash or power loss the open span's end_ts in the database is stale: apply the last heartbeat. */
int stc_hb_recover(sqlite3 *db, time_t now)
{
    sqlite3_int64 id;
    time_t ts;
    sqlite3_stmt *s;
    int changed = 0;
    if (!stc_hb_read(&id, &ts) || id <= 0 || ts > now) return 0;
    if (sqlite3_prepare_v2(db, "UPDATE span SET end_ts=?1 WHERE id=?2 AND end_ts<?1 AND start_ts<=?1 AND ?1-start_ts<=?3",
                           -1, &s, 0))
        return 0;
    sqlite3_bind_int64(s, 1, ts);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_bind_int64(s, 3, STC_MAX_SPAN + 600);
    sqlite3_step(s);
    changed = sqlite3_changes(db);
    sqlite3_finalize(s);
    return changed;
}
