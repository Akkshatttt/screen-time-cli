/* screen-time-cli: command line front end. Reads the DB read-only; talks to the daemon via named events. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../core/stc.h"
#include "../install/install.h"
#include "insights/ins.h"
#include "render.h"
#include "update.h"

static void ok(const char *msg) { printf(" %s%s%s %s\n", K(K_OK), rd_unicode ? "\xe2\x9c\x93" : "ok", K(K_RESET), msg); }
static void bad(const char *msg) { printf(" %s%s%s %s\n", K(K_BAD), rd_unicode ? "\xe2\x9c\x97" : "x", K(K_RESET), msg); }

static int signal_daemon(const wchar_t *name, const char *what)
{
    wchar_t nm[64];
    HANDLE h;
    stc_obj_name(nm, 64, name);
    h = OpenEventW(EVENT_MODIFY_STATE, FALSE, nm);
    if (!h) { bad("daemon is not running (start it with: screen-time-cli start)"); return 1; }
    SetEvent(h);
    CloseHandle(h);
    ok(what);
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
    if (stc_daemon_running()) { ok("daemon already running"); return 0; }
    exe_dir(dir);
    if (stc_daemon_start(dir)) { bad("could not start screen-time-daemon.exe (it must sit next to this exe)"); return 1; }
    ok("daemon started");
    return 0;
}

/* y/N prompt; anything but y/yes (or no console input) means no. */
static int confirm(const char *question)
{
    char line[16];
    printf("\n %s%s%s [y/N] ", K(K_BOLD), question, K(K_RESET));
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) { printf("\n"); return 0; }
    return line[0] == 'y' || line[0] == 'Y';
}

static int cmd_uninstall(int argc, char **argv)
{
    stc_inst in;
    char data[MAX_PATH * 3];
    int i, clean = 0, yes = 0;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--clean") || !strcmp(argv[i], "--purge")) clean = 1;
        else if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) yes = 1;
        else { printf("unknown option: %s\n", argv[i]); return 1; }
    }
    stc_inst_defaults(&in);
    if (stc_data_dir(data, sizeof data)) strcpy(data, "(unknown)");

    rd_title(clean ? "clean uninstall" : "uninstall", NULL);
    printf("\n   %sRemoves%s  program files        %ls\n", K(K_BOLD), K(K_RESET), in.dir);
    printf("            autostart, PATH and Settings > Apps entries\n");
    if (clean) {
        printf("   %sDeletes%s  %sall usage data, backups and config.ini%s\n", K(K_BAD), K(K_RESET), K(K_BAD), K(K_RESET));
        printf("            %s%s%s\n", K(K_DIM), data, K(K_RESET));
    } else {
        printf("   %sKeeps%s    your usage data and settings  %s%s%s\n", K(K_OK), K(K_RESET), K(K_DIM), data, K(K_RESET));
        printf("            %sto delete those too, run: screen-time-cli uninstall --clean%s\n", K(K_DIM), K(K_RESET));
    }
    if (!yes && !confirm(clean ? "Remove the application and permanently delete all its data?"
                               : "Remove the application (data is kept)?")) {
        printf("\n"); ok("cancelled, nothing was changed");
        return 1;
    }
    stc_uninstall(&in, clean);
    printf("\n");
    ok(clean ? "uninstalled; application and all data removed" : "uninstalled; your data was kept");
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    stc_inst in;
    wchar_t src[MAX_PATH];
    stc_inst_defaults(&in);
    exe_dir(src);
    if (!strcmp(argv[1], "install")) {
        if (stc_install(&in, src)) { bad("install failed, previous version left in place"); return 1; }
        stc_daemon_start(in.dir);
        printf("\n"); ok("installed and tracking");
        printf("   %s%ls%s\n   open a new terminal, then run: screen-time-cli status\n", K(K_DIM), in.dir, K(K_RESET));
        return 0;
    }
    rd_title("doctor", NULL);
    printf("\n");
    return stc_doctor(&in, argc > 2 && !strcmp(argv[2], "--fix"), src) ? 1 : 0;
}

static void hms(long s, char *out, size_t n)
{
    rd_dur(out, n, s);
}

static int cmd_status(sqlite3 *db)
{
    sqlite3_stmt *s;
    char paused[8] = "0", dv[32] = "", d[32];
    static const char *names[] = { "active", "passive", "away" };
    static const char *cols[] = { K_ACT, K_PAS, K_DIM };
    int running = stc_daemon_running();

    rd_title("screen-time", "status");
    if (!db) {
        printf("\n   %s%s%s %s\n\n", K(K_DIM), running ? "running" : "daemon not running", K(K_RESET),
               stc_db_error()[0] ? stc_db_error() : "no data recorded yet");
        return 0;
    }
    if (stc_meta_get(db, "paused", paused, sizeof paused) && paused[0] == '1')
        printf("\n   %s%s paused%s   resume with: screen-time-cli resume\n", K(K_UP), rd_unicode ? "\xe2\x8f\xb8" : "||", K(K_RESET));
    else if (running)
        printf("\n   %s%s tracking%s\n", K(K_OK), rd_unicode ? "\xe2\x97\x8f" : "*", K(K_RESET));
    else
        printf("\n   %s%s daemon not running%s   start it with: screen-time-cli start\n", K(K_BAD), rd_unicode ? "\xe2\x97\x8b" : "o", K(K_RESET));
    if (stc_meta_get(db, "daemon_version", dv, sizeof dv) && strcmp(dv, STC_VERSION))
        printf("   %swarning: daemon is %s, this CLI is %s. Run: screen-time-cli stop, then start%s\n", K(K_UP), dv, STC_VERSION, K(K_RESET));

    if (!sqlite3_prepare_v2(db, "SELECT e.name, t.text, s.state, s.start_ts, s.end_ts, s.id FROM span s"
                                " JOIN exe e ON e.id=s.exe_id JOIN title t ON t.id=s.title_id"
                                " ORDER BY s.id DESC LIMIT 1", -1, &s, 0)) {
        if (sqlite3_step(s) == SQLITE_ROW) {
            int st = sqlite3_column_int(s, 2) % 3;
            char title[160];
            time_t end = (time_t)sqlite3_column_int64(s, 4);
            sqlite3_int64 hid; time_t hts;
            if (stc_hb_read(&hid, &hts) && hid == sqlite3_column_int64(s, 5) && hts > end) end = hts;   /* live open span */
            hms((long)(end - sqlite3_column_int64(s, 3)), d, sizeof d);
            printf("\n   %sLast window%s  %s%s%s  %s[%s%s%s for %s]%s\n", K(K_DIM), K(K_RESET), K(K_BOLD),
                   sqlite3_column_text(s, 0), K(K_RESET), K(K_DIM), K(cols[st]), names[st], K(K_DIM), d, K(K_RESET));
            printf("                %s%s%s\n", K(K_DIM), rd_trunc(title, sizeof title, (const char *)sqlite3_column_text(s, 1), rd_width - 20),
                   K(K_RESET));
        } else printf("\n   %sno spans recorded yet%s\n", K(K_DIM), K(K_RESET));
        sqlite3_finalize(s);
    }
    printf("   %sversion %s%s\n\n", K(K_DIM), STC_VERSION, K(K_RESET));
    return 0;
}

static void usage(void)
{
    int i;
    rd_title("screen-time-cli", "v" STC_VERSION);
    printf("\n %sUsage%s  screen-time-cli <command> [options]\n", K(K_BOLD), K(K_RESET));
    rd_section("Reports and insights");
    for (i = 0; i < N_INSIGHTS; i++) printf("  %s%-13s%s %s%s%s\n", K(K_ACT), INSIGHTS[i].name, K(K_RESET), K(K_DIM), INSIGHTS[i].help, K(K_RESET));
    printf("  %soptions%s    --days N   --app NAME   --top N   --by category   --json\n", K(K_DIM), K(K_RESET));
    rd_section("Tracking");
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "status", K(K_RESET), K(K_DIM), "daemon state and the last recorded window", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "start", K(K_RESET), K(K_DIM), "launch the tracking daemon", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "stop", K(K_RESET), K(K_DIM), "stop the daemon", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "pause", K(K_RESET), K(K_DIM), "pause tracking", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "resume", K(K_RESET), K(K_DIM), "resume tracking", K(K_RESET));
    rd_section("Installation");
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "install", K(K_RESET), K(K_DIM), "install or upgrade for this user (PATH, autostart, Settings > Apps)", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "uninstall", K(K_RESET), K(K_DIM), "remove the application, keep your data  [--yes]", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "", K(K_RESET), K(K_DIM), "uninstall --clean   also delete all data and settings", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "doctor", K(K_RESET), K(K_DIM), "check the installation  [--fix]", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n", K(K_ACT), "check-update", K(K_RESET), K(K_DIM), "ask GitHub for a newer release (never installs)", K(K_RESET));
    printf("  %s%-13s%s %s%s%s\n\n", K(K_ACT), "version", K(K_RESET), K(K_DIM), "print the version", K(K_RESET));
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "help";
    sqlite3 *db;
    int rc = 0, i;

    rd_init();
    if (!strcmp(c, "version") || !strcmp(c, "--version")) { puts("screen-time-cli " STC_VERSION); return 0; }
    if (!strcmp(c, "install") || !strcmp(c, "doctor")) return cmd_install(argc, argv);
    if (!strcmp(c, "uninstall")) return cmd_uninstall(argc, argv);
    if (!strcmp(c, "check-update")) return cmd_check_update();
    if (!strcmp(c, "start")) return cmd_start();
    if (!strcmp(c, "stop")) return signal_daemon(L"Local\\screen-time-stop", "daemon stopping");
    if (!strcmp(c, "pause")) return signal_daemon(L"Local\\screen-time-pause", "tracking paused");
    if (!strcmp(c, "resume")) return signal_daemon(L"Local\\screen-time-resume", "tracking resumed");

    if (!strcmp(c, "help") || !strcmp(c, "--help") || !strcmp(c, "-h")) { usage(); return 0; }
    db = stc_db_open(NULL, 1);
    if (!strcmp(c, "status")) rc = cmd_status(db);
    else {
        for (i = 0; i < N_INSIGHTS && strcmp(INSIGHTS[i].name, c); i++) ;
        if (i == N_INSIGHTS) { printf("unknown command: %s\n", c); usage(); rc = 1; }
        else if (!db) {
            printf("\n %s%s%s\n\n", K(K_DIM), stc_db_error()[0] ? stc_db_error() : "no data recorded yet (is the daemon running?)", K(K_RESET));
            rc = 1;
        } else {
            stc_args a;
            if (args_parse(argc, argv, 2, INSIGHTS[i].default_days, &a)) rc = 1;
            else rc = INSIGHTS[i].run(db, &a);
        }
    }
    if (db) sqlite3_close(db);
    return rc;
}
