/* trends: this period vs the previous equal one, weekly rhythm, night usage, biggest movers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

static const char *WD[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

typedef struct { const app_t *cur, *prev; long diff; } mover;

static int by_abs_diff(const void *x, const void *y)
{
    long a = labs(((const mover *)x)->diff), b = labs(((const mover *)y)->diff);
    return a < b ? 1 : a > b ? -1 : 0;
}

int cmd_trends(sqlite3 *db, const stc_args *a)
{
    profile cur, prev;
    time_t pf = prev_from(a);
    long wd_sum[7] = { 0 }, night = 0, all = 0, wk = 0, we = 0;
    int wd_n[7] = { 0 }, d, h, i, nwk = 0, nwe = 0;
    char bb[RD_BAR];
    char b1[64], b2[64], b3[64], b4[64], lbl[64];
    mover mv[256];
    int nm = 0;
    long total, ptotal, maxavg = 0;

    profile_compute(db, a->from, a->to, a->app, &cur);
    profile_compute(db, pf, pf + (a->to - a->from), a->app, &prev);
    total = cur.act + cur.pas;
    ptotal = prev.act + prev.pas;

    for (d = 0; d < cur.ndays; d++) {
        struct tm tm = *localtime(&cur.dayb[d]);
        int w = (tm.tm_wday + 6) % 7;
        long v = cur.day_act[d] + cur.day_pas[d];
        wd_sum[w] += v;
        wd_n[w]++;
        if (w >= 5) { we += v; nwe++; } else { wk += v; nwk++; }
    }
    for (d = 0; d < 7; d++) for (h = 0; h < 24; h++) { all += cur.hm[d][h]; if (h < 5) night += cur.hm[d][h]; }
    for (i = 0; i < cur.napps && nm < 256; i++) {
        mv[nm].cur = &cur.apps[i];
        mv[nm].prev = profile_find(&prev, cur.apps[i].name);
        mv[nm].diff = (cur.apps[i].act + cur.apps[i].pas) - (mv[nm].prev ? mv[nm].prev->act + mv[nm].prev->pas : 0);
        nm++;
    }
    qsort(mv, nm, sizeof mv[0], by_abs_diff);

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"trends\",\"days\":%d,\"total\":%ld,\"previous_total\":%ld,"
               "\"night_share\":%.4f,\"weekday_avg\":[", a->days, total, ptotal, all ? (double)night / all : 0.0);
        for (d = 0; d < 7; d++) printf("%s%ld", d ? "," : "", wd_n[d] ? wd_sum[d] / wd_n[d] : 0L);
        printf("],\"movers\":[");
        for (i = 0; i < nm && i < a->top; i++) {
            printf("%s{\"name\":", i ? "," : "");
            rd_jstr(mv[i].cur->name);
            printf(",\"diff\":%ld}", mv[i].diff);
        }
        printf("]}\n");
        goto done;
    }

    snprintf(lbl, sizeof lbl, "%s vs previous %s", a->days == 1 ? "today" : "this period", a->days == 1 ? "day" : "period");
    rd_title("trends", lbl);
    if (!total && !ptotal) { printf("\n   %sno activity recorded in these periods%s\n\n", K(K_DIM), K(K_RESET)); goto done; }

    printf("\n   %sScreen time%s  %s%s%s  %s   %sbefore: %s%s\n", K(K_DIM), K(K_RESET), K(K_BOLD), rd_dur(b1, sizeof b1, total),
           K(K_RESET), rd_trend(b2, sizeof b2, total, ptotal), K(K_DIM), rd_dur(b3, sizeof b3, ptotal), K(K_RESET));
    printf("   %sPer day%s      %s   %sbefore: %s%s\n", K(K_DIM), K(K_RESET), rd_dur(b1, sizeof b1, total / cur.ndays),
           K(K_DIM), rd_dur(b3, sizeof b3, ptotal / (prev.ndays ? prev.ndays : 1)), K(K_RESET));

    rd_section("Biggest changes");
    for (i = 0; i < nm && i < 6; i++) {
        long t = mv[i].cur->act + mv[i].cur->pas, pv = mv[i].prev ? mv[i].prev->act + mv[i].prev->pas : 0;
        if (mv[i].diff == 0) break;
        rd_dur(b3, sizeof b3, labs(mv[i].diff));
        printf("  %-24s %8s  %s%s%-8s%s  %s\n", rd_trunc(b1, sizeof b1, mv[i].cur->name, 24), rd_dur(b2, sizeof b2, t),
               K(mv[i].diff > 0 ? K_UP : K_DOWN), mv[i].diff > 0 ? "+" : "-", b3, K(K_RESET),
               rd_trend(b4, sizeof b4, t, pv));
    }

    if (cur.ndays >= 7) {
        rd_section("Weekly rhythm (average per day)");
        for (d = 0; d < 7; d++) { long v = wd_n[d] ? wd_sum[d] / wd_n[d] : 0; if (v > maxavg) maxavg = v; }
        for (d = 0; d < 7; d++) {
            long v = wd_n[d] ? wd_sum[d] / wd_n[d] : 0;
            printf("  %s%s%s  %s  %s\n", K(K_DIM), WD[d], K(K_RESET), rd_bar(bb, sizeof bb, maxavg ? (double)v / maxavg : 0, 0, 30),
                   rd_dur(b2, sizeof b2, v));
        }
        if (nwk && nwe)
            printf("\n   weekday avg %s%s%s   weekend avg %s%s%s\n", K(K_BOLD), rd_dur(b1, sizeof b1, wk / nwk), K(K_RESET),
                   K(K_BOLD), rd_dur(b2, sizeof b2, we / nwe), K(K_RESET));
    }
    rd_section("Late night");
    printf("   %s%.0f%%%s of screen time falls between 00:00 and 05:00 (%s)\n\n", K(K_BOLD), all ? 100.0 * night / all : 0.0,
           K(K_RESET), rd_dur(b1, sizeof b1, night));
done:
    profile_free(&cur);
    profile_free(&prev);
    return 0;
}
