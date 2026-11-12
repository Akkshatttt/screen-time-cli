/* today: a one-screen glance at the day so far. Pace against yesterday at the same time, what is running now,
 * a 24-hour strip of the dominant category per hour, top apps, and how steady attention has been. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

typedef struct { char app[48]; int state; long secs; int have; } now_info;

static void last_span(sqlite3 *db, now_info *n)
{
    sqlite3_stmt *s;
    memset(n, 0, sizeof *n);
    if (sqlite3_prepare_v2(db, "SELECT e.name, s.state, s.start_ts, s.end_ts, s.id FROM span s"
                               " JOIN exe e ON e.id=s.exe_id ORDER BY s.id DESC LIMIT 1", -1, &s, 0)) return;
    if (sqlite3_step(s) == SQLITE_ROW) {
        time_t end = (time_t)sqlite3_column_int64(s, 3);
        sqlite3_int64 hid;
        time_t hts;
        if (stc_hb_read(&hid, &hts) && hid == sqlite3_column_int64(s, 4) && hts > end) end = hts;   /* live open span */
        snprintf(n->app, sizeof n->app, "%s", (const char *)sqlite3_column_text(s, 0));
        n->state = sqlite3_column_int(s, 1) % 3;
        n->secs = (long)(end - sqlite3_column_int64(s, 2));
        n->have = 1;
    }
    sqlite3_finalize(s);
}

static time_t first_active(sqlite3 *db, time_t from, time_t to)
{
    sqlite3_stmt *s;
    time_t t = 0;
    if (sqlite3_prepare_v2(db, "SELECT MIN(start_ts) FROM span WHERE start_ts>=?1 AND start_ts<?2 AND state<2", -1, &s, 0)) return 0;
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_type(s, 0) != SQLITE_NULL) t = (time_t)sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return t;
}

static long ctot(const profile *p, int i) { return p->cat_act[i] + p->cat_pas[i]; }

/* One letter per category for the hour strip: first unused letter of its name. */
static void symbols(const profile *p, char *sym)
{
    int i, k;
    char used[256] = { 0 };
    for (i = 0; i < p->ncat; i++) {
        sym[i] = '?';
        for (k = 0; p->cat_name[i][k]; k++) {
            unsigned char ch = (unsigned char)toupper((unsigned char)p->cat_name[i][k]);
            if (isalnum(ch) && !used[ch]) { used[ch] = 1; sym[i] = (char)ch; break; }
        }
    }
}

int cmd_today(sqlite3 *db, const stc_args *a)
{
    profile cur, prev;
    focus_result fr;
    switch_result sw;
    now_info now;
    time_t pf = prev_from(a), first;
    long total, ptotal, hrs[24], top;
    int i, h, ord[MAXCAT], n, shown, aw = 22, bw;
    char sym[MAXCAT], b1[96], b2[96], b3[96], b4[96], sp[512], bb[RD_BAR], lbl[48];
    struct tm tm = *localtime(&a->from);
    static const char *ST[] = { "active", "passive", "away" };

    profile_compute(db, a->from, a->to, a->app, &cur);
    profile_compute(db, pf, pf + (a->to - a->from), a->app, &prev);   /* yesterday up to the same time of day */
    profile_sort(&cur);
    focus_compute(db, a->from, a->to, a->app, &fr);
    switches_compute(db, a->from, a->to, &sw);
    last_span(db, &now);
    first = first_active(db, a->from, a->to);
    total = cur.act + cur.pas;
    ptotal = prev.act + prev.pas;
    for (h = 0; h < 24; h++) { int d; hrs[h] = 0; for (d = 0; d < 7; d++) hrs[h] += cur.hm[d][h]; }

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"today\",\"from\":%I64d,\"to\":%I64d,\"total\":{\"active\":%ld,\"passive\":%ld},"
               "\"yesterday_by_now\":{\"active\":%ld,\"passive\":%ld},\"first_activity\":%I64d,\"hours\":[",
               (long long)a->from, (long long)a->to, cur.act, cur.pas, prev.act, prev.pas, (long long)first);
        for (h = 0; h < 24; h++) printf("%s%ld", h ? "," : "", hrs[h]);
        printf("],\"now\":{\"app\":");
        rd_jstr(now.app);
        printf(",\"label\":");
        rd_jstr(stc_disp(now.app));
        printf(",\"state\":\"%s\",\"seconds\":%ld},\"categories\":[", ST[now.state], now.secs);
        for (i = 0; i < cur.ncat; i++) {
            printf("%s{\"name\":", i ? "," : "");
            rd_jstr(cur.cat_name[i]);
            printf(",\"active\":%ld,\"passive\":%ld}", cur.cat_act[i], cur.cat_pas[i]);
        }
        printf("],\"items\":[");
        for (i = 0; i < cur.napps && i < 5; i++) {
            printf("%s{\"name\":", i ? "," : "");
            rd_jstr(cur.apps[i].name);
            printf(",\"label\":");
            rd_jstr(cur.apps[i].disp);
            printf(",\"active\":%ld,\"passive\":%ld}", cur.apps[i].act, cur.apps[i].pas);
        }
        printf("],\"focus\":{\"sessions\":%d,\"longest_session\":%ld,\"focus_score\":%.4f,\"switches\":%ld}}\n", fr.sessions,
               fr.longest_session_s, fr.screen_s ? (double)fr.focus_s / fr.screen_s : 0.0, sw.switches);
        profile_free(&cur); profile_free(&prev);
        return 0;
    }

    strftime(lbl, sizeof lbl, "%a %d %b", &tm);
    rd_title("today", lbl);
    if (!total) {
        printf("\n   %sno activity recorded yet today%s%s\n\n", K(K_DIM), K(K_RESET), a->app[0] ? " for that app" : "");
        profile_free(&cur); profile_free(&prev);
        return 0;
    }

    printf("\n   %sSo far%s   %s%s%s     %s  %svs yesterday at this time%s\n", K(K_DIM), K(K_RESET), K(K_BOLD),
           rd_dur(b1, sizeof b1, total), K(K_RESET), rd_trend(b2, sizeof b2, total, ptotal), K(K_DIM), K(K_RESET));
    printf("   %sActive%s   %s%s%s    %sPassive%s  %s%s%s", K(K_DIM), K(K_RESET), K(K_ACT), rd_dur(b1, sizeof b1, cur.act),
           K(K_RESET), K(K_DIM), K(K_RESET), K(K_PAS), rd_dur(b2, sizeof b2, cur.pas), K(K_RESET));
    if (first) { struct tm ft = *localtime(&first); printf("    %sstarted%s %02d:%02d", K(K_DIM), K(K_RESET), ft.tm_hour, ft.tm_min); }
    printf("\n");
    if (now.have)
        printf("   %sNow%s      %s%s%s  %s%s for %s%s\n", K(K_DIM), K(K_RESET), K(K_BOLD),
               rd_trunc(b3, sizeof b3, stc_disp(now.app), 28), K(K_RESET), K(K_DIM), ST[now.state],
               rd_dur(b4, sizeof b4, now.secs), K(K_RESET));

    /* hour strip: height = how much, letter = dominant category */
    symbols(&cur, sym);
    rd_section("Timeline");
    printf("   %s\n   ", rd_spark(sp, sizeof sp, hrs, 24));
    for (h = 0; h < 24; h++) {
        int c, best = -1;
        for (c = 0; c < cur.ncat; c++) if (cur.cat_hour[c][h] > 0 && (best < 0 || cur.cat_hour[c][h] > cur.cat_hour[best][h])) best = c;
        printf("%s%c%s", K(K_DIM), best < 0 ? ' ' : sym[best], K(K_RESET));
    }
    printf("\n   %s00    06    12    18   23%s\n   ", K(K_DIM), K(K_RESET));
    n = 0;
    {   int idx[MAXCAT];
        for (i = 0; i < cur.ncat; i++) idx[i] = i;
        for (i = 1; i < cur.ncat; i++) {   /* largest first */
            int v = idx[i], j;
            for (j = i - 1; j >= 0 && ctot(&cur, idx[j]) < ctot(&cur, v); j--) idx[j + 1] = idx[j];
            idx[j + 1] = v;
        }
        for (i = 0; i < cur.ncat; i++) ord[n++] = idx[i];
    }
    for (i = 0; i < n && i < 6; i++)
        printf("%s%s%c%s %s %.0f%%%s", i ? "   " : "", K(K_BOLD), sym[ord[i]], K(K_RESET), cur.cat_name[ord[i]],
               100.0 * ctot(&cur, ord[i]) / total, "");
    printf("\n");

    rd_section("Top apps");
    bw = rd_width - 3 - 2 - aw - 9 - 6 - 4;
    if (bw > 36) bw = 36;
    if (bw < 10) bw = 10;
    shown = cur.napps < 5 ? cur.napps : 5;
    top = shown ? cur.apps[0].act + cur.apps[0].pas : 0;
    for (i = 0; i < shown; i++) {
        const app_t *ap = &cur.apps[i];
        long t = ap->act + ap->pas;
        double sc = top ? 1.0 / top : 0;
        printf("  %s%d%s  %-*s %8s  %s %s%3.0f%%%s\n", K(K_DIM), i + 1, K(K_RESET), aw, rd_trunc(b3, sizeof b3, ap->disp, aw),
               rd_dur(b1, sizeof b1, t), rd_bar(bb, sizeof bb, ap->act * sc, ap->pas * sc, bw), K(K_DIM), 100.0 * t / total,
               K(K_RESET));
    }

    rd_section("Attention");
    printf("   %sSessions%s %d   %slongest%s %s   %sfocus%s %.0f%%   %sswitches%s %ld %s(%.0f per active hour)%s\n", K(K_DIM),
           K(K_RESET), fr.sessions, K(K_DIM), K(K_RESET), rd_dur(b1, sizeof b1, fr.longest_session_s), K(K_DIM), K(K_RESET),
           fr.screen_s ? 100.0 * fr.focus_s / fr.screen_s : 0.0, K(K_DIM), K(K_RESET), sw.switches, K(K_DIM),
           sw.active_s ? sw.switches / (sw.active_s / 3600.0) : 0.0, K(K_RESET));
    printf("   %sfor patterns over days, press r (report)%s\n\n", K(K_DIM), K(K_RESET));
    profile_free(&cur);
    profile_free(&prev);
    return 0;
}
