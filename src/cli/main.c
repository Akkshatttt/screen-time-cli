/* screen-time-cli: command line front end. Reads the DB read-only; talks to the daemon via named events. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "../install/install.h"

static void hms(long s, char *out, size_t n)
{
    if (s >= 3600) snprintf(out, n, "%ldh %02ldm", s / 3600, s % 3600 / 60);
    else if (s >= 60) snprintf(out, n, "%ldm %02lds", s / 60, s % 60);
    else snprintf(out, n, "%lds", s);
}

static int signal_daemon(const wchar_t *name, const char *what)
{
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!h) { puts("daemon is not running (try: screen-time-cli start)"); return 1; }
    SetEvent(h);
    CloseHandle(h);
    printf("%s\n", what);
    return 0;
}

static void exe_dir(wchar_t *dir)
{
    wchar_t *slash;
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
}

static int cmd_start(void)
{
    wchar_t dir[MAX_PATH];
    if (stc_daemon_running()) { puts("daemon already running"); return 0; }
    exe_dir(dir);
    if (stc_daemon_start(dir)) { puts("could not start screen-time-daemon.exe (next to this exe)"); return 1; }
    puts("daemon started");
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    stc_inst in;
    wchar_t src[MAX_PATH];
    int fix = argc > 2 && !strcmp(argv[2], "--fix");
    stc_inst_defaults(&in);
    exe_dir(src);
    if (!strcmp(argv[1], "install")) {
        if (stc_install(&in, src)) return 1;
        stc_daemon_start(in.dir);
        printf("installed to %ls\nopen a new terminal, then run: screen-time-cli status\n", in.dir);
        return 0;
    }
    if (!strcmp(argv[1], "uninstall")) {
        stc_uninstall(&in, argc > 2 && !strcmp(argv[2], "--purge"));
        puts("uninstalled (data kept unless --purge)");
        return 0;
    }
    return stc_doctor(&in, fix, src) ? 1 : 0;
}

static int cmd_status(sqlite3 *db)
{
    sqlite3_stmt *s;
    char paused[8] = "0";
    static const char *names[] = { "active", "passive", "away" };
    printf("daemon: %s\n", stc_daemon_running() ? "running" : "not running");
    if (!db) { puts("no data yet"); return 0; }
    if (stc_meta_get(db, "paused", paused, sizeof paused) && paused[0] == '1') puts("tracking: paused");
    if (sqlite3_prepare_v2(db, "SELECT e.name, t.text, s.state, s.start_ts, s.end_ts FROM span s"
                               " JOIN exe e ON e.id=s.exe_id JOIN title t ON t.id=s.title_id"
                               " ORDER BY s.id DESC LIMIT 1", -1, &s, 0))
        return 1;
    if (sqlite3_step(s) == SQLITE_ROW) {
        char d[32];
        hms((long)(sqlite3_column_int64(s, 4) - sqlite3_column_int64(s, 3)), d, sizeof d);
        printf("last: %s [%s] for %s\n      %s\n", sqlite3_column_text(s, 0),
               names[sqlite3_column_int(s, 2) % 3], d, sqlite3_column_text(s, 1));
    } else puts("no spans recorded yet");
    sqlite3_finalize(s);
    return 0;
}

static void print_app(void *ctx, const char *name, long a, long p)
{
    char t[32], ta[32], tp[32];
    long *tot = ctx;
    hms(a + p, t, sizeof t);
    hms(a, ta, sizeof ta);
    hms(p, tp, sizeof tp);
    printf("%-32.32s %10s   active %-9s passive %s\n", name, t, ta, tp);
    tot[0] += a;
    tot[1] += p;
}

static int cmd_range(sqlite3 *db, const char *label, time_t from, time_t to)
{
    long tot[2] = { 0, 0 };
    char t[32];
    if (!db) { puts("no data yet"); return 0; }
    printf("%s\n", label);
    stc_per_app(db, from, to, print_app, tot);
    hms(tot[0] + tot[1], t, sizeof t);
    printf("total %s\n", t);
    return 0;
}

static void usage(void)
{
    puts("usage: screen-time-cli <command>\n"
         "  status   daemon state and last recorded window\n"
         "  today    per-app time today\n"
         "  week     per-app time, last 7 days\n"
         "  month    per-app time, last 30 days\n"
         "  start    launch the tracking daemon\n"
         "  stop     stop the daemon\n"
         "  pause    pause tracking\n"
         "  resume   resume tracking\n"
         "  install  install for this user (PATH, autostart, Settings > Apps)\n"
         "  uninstall [--purge]  remove (--purge also deletes the data)\n"
         "  doctor [--fix]  check the installation, optionally repair it\n"
         "  version  print version");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "help";
    sqlite3 *db;
    time_t now = time(NULL);
    int rc = 0;

    SetConsoleOutputCP(CP_UTF8);
    if (!strcmp(c, "version") || !strcmp(c, "--version")) { puts("screen-time-cli " STC_VERSION); return 0; }
    if (!strcmp(c, "install") || !strcmp(c, "uninstall") || !strcmp(c, "doctor")) return cmd_install(argc, argv);
    if (!strcmp(c, "start")) return cmd_start();
    if (!strcmp(c, "stop")) return signal_daemon(L"Local\\screen-time-stop", "daemon stopping");
    if (!strcmp(c, "pause")) return signal_daemon(L"Local\\screen-time-pause", "tracking paused");
    if (!strcmp(c, "resume")) return signal_daemon(L"Local\\screen-time-resume", "tracking resumed");

    db = stc_db_open(NULL, 1);
    if (!strcmp(c, "status")) rc = cmd_status(db);
    else if (!strcmp(c, "today")) rc = cmd_range(db, "today", stc_day_start(now, 0), now + 1);
    else if (!strcmp(c, "week")) rc = cmd_range(db, "last 7 days", stc_day_start(now, -6), now + 1);
    else if (!strcmp(c, "month")) rc = cmd_range(db, "last 30 days", stc_day_start(now, -29), now + 1);
    else { usage(); rc = strcmp(c, "help") ? 1 : 0; }
    if (db) sqlite3_close(db);
    return rc;
}
