/* breakdown (week, month): totals, per-app (or per-category) bars with trend vs the previous equal period, daily and hourly shape. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

static const char *WD[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

static void day_label(time_t t, char *out, size_t n)
{
    struct tm tm = *localtime(&t);
    snprintf(out, n, "%s %02d", WD[(tm.tm_wday + 6) % 7], tm.tm_mday);
}

static void range_label(const stc_args *a, char *out, size_t n)
{
    struct tm f = *localtime(&a->from), t;
    time_t last = a->to - 1;
    char b1[24], b2[24];
    t = *localtime(&last);
    strftime(b1, sizeof b1, "%a %d %b", &f);
    strftime(b2, sizeof b2, "%a %d %b", &t);
    if (a->days == 1) snprintf(out, n, "today, %s", b1);
    else snprintf(out, n, "last %d days, %s - %s", a->days, b1, b2);
}

static void hour_totals(const profile *p, long h[24])
{
    int d, i;
    memset(h, 0, 24 * sizeof(long));
    for (d = 0; d < 7; d++) for (i = 0; i < 24; i++) h[i] += p->hm[d][i];
}

/* Prints the shared JSON head. more=1 leaves the object open for the caller to append fields. */
void breakdown_json(const stc_args *a, const profile *cur, const profile *prev, int more)
{
    int i;
    printf("{\"schema\":1,\"command\":\"report\",\"from\":%I64d,\"to\":%I64d,\"days\":%d,\"by\":%s,",
           (long long)a->from, (long long)a->to, a->days, a->by_cat ? "\"category\"" : "\"app\"");
    printf("\"total\":{\"active\":%ld,\"passive\":%ld},\"previous\":{\"active\":%ld,\"passive\":%ld},\"items\":[",
           cur->act, cur->pas, prev->act, prev->pas);
    for (i = 0; i < cur->napps; i++) {
        const app_t *pv = profile_find(prev, cur->apps[i].name);
        printf("%s{\"name\":", i ? "," : "");
        rd_jstr(cur->apps[i].name);
        printf(",\"label\":");
        rd_jstr(cur->apps[i].disp);
        printf(",\"active\":%ld,\"passive\":%ld,\"previous\":%ld}", cur->apps[i].act, cur->apps[i].pas,
               pv ? pv->act + pv->pas : 0L);
    }
    printf("],\"daily\":[");
    for (i = 0; i < cur->ndays; i++) {
        struct tm tm = *localtime(&cur->dayb[i]);
        printf("%s{\"date\":\"%04d-%02d-%02d\",\"active\":%ld,\"passive\":%ld}", i ? "," : "", tm.tm_year + 1900,
               tm.tm_mon + 1, tm.tm_mday, cur->day_act[i], cur->day_pas[i]);
    }
    printf("]%s", more ? "" : "}\n");
}

/* Daily section: a bar per day (14 days or fewer) or a sparkline, with average, busiest and quietest day. */
void breakdown_daily(const profile *p)
{
    char b1[96], b2[96], b3[96], b4[96], b5[96], sp[2048], bb[RD_BAR];
    int i;
    if (p->ndays < 2) return;
    {
        long maxd = 0, sum = 0, best = 0, worst = -1;
        int bi = 0, wi = 0, days_with = 0;
        long dv[3660];
        rd_section("Daily");
        for (i = 0; i < p->ndays && i < 3660; i++) {
            long v = p->day_act[i] + p->day_pas[i];
            dv[i] = v;
            if (v > maxd) maxd = v;
            sum += v;
            if (v > best) { best = v; bi = i; }
            if (v > 0) { days_with++; if (worst < 0 || v < worst) { worst = v; wi = i; } }
        }
        if (p->ndays <= 14) {
            double sc = maxd ? 1.0 / maxd : 0;
            for (i = 0; i < p->ndays; i++) {
                day_label(p->dayb[i], b3, sizeof b3);
                printf("  %s%s%s  %s  %s\n", K(K_DIM), b3, K(K_RESET),
                       rd_bar(bb, sizeof bb, p->day_act[i] * sc, p->day_pas[i] * sc, 30), rd_dur(b1, sizeof b1, dv[i]));
            }
        } else {
            int n = p->ndays < 3660 ? p->ndays : 3660;
            printf("   %s\n", rd_spark(sp, sizeof sp, dv + (n > 90 ? n - 90 : 0), n > 90 ? 90 : n));
        }
        if (days_with) {
            day_label(p->dayb[bi], b2, sizeof b2);
            day_label(p->dayb[wi], b3, sizeof b3);
            printf("   %savg%s %s/day   %sbusiest%s %s %s   %squietest%s %s %s\n", K(K_DIM), K(K_RESET),
                   rd_dur(b1, sizeof b1, sum / p->ndays), K(K_DIM), K(K_RESET), b2, rd_dur(b4, sizeof b4, best),
                   K(K_DIM), K(K_RESET), b3, rd_dur(b5, sizeof b5, worst));
        }
    }
}

int cmd_breakdown(sqlite3 *db, const stc_args *a)
{
    profile cur, prev;
    time_t pf = prev_from(a);
    char b1[96], b2[96], b3[96], b4[96], lbl[96], sp[2048], bb[RD_BAR];
    long total, ptotal, hrs[24];
    int i, shown, bw, aw;
    const char *what = a->by_cat ? "Categories" : "Apps";
    const char *vs = a->days == 1 ? "yesterday" : "previous period";

    profile_compute(db, a->from, a->to, a->app, &cur);
    profile_compute(db, pf, pf + (a->to - a->from), a->app, &prev);
    if (a->by_cat) { profile_to_categories(&cur); profile_to_categories(&prev); }
    else profile_sort(&cur);

    if (a->json) { breakdown_json(a, &cur, &prev, 0); profile_free(&cur); profile_free(&prev); return 0; }

    total = cur.act + cur.pas;
    ptotal = prev.act + prev.pas;
    range_label(a, lbl, sizeof lbl);
    rd_title("screen-time", lbl);

    if (!total) {
        printf("\n   %sno activity recorded in this range%s%s\n\n", K(K_DIM), K(K_RESET),
               a->app[0] ? " for that app" : "");
        profile_free(&cur); profile_free(&prev);
        return 0;
    }

    /* summary */
    printf("\n   %sTotal%s    %s%s%s     %s  %svs %s%s\n", K(K_DIM), K(K_RESET), K(K_BOLD), rd_dur(b1, sizeof b1, total),
           K(K_RESET), rd_trend(b2, sizeof b2, total, ptotal), K(K_DIM), vs, K(K_RESET));
    printf("   %sActive%s   %s%s%s    %sPassive%s  %s%s%s\n", K(K_DIM), K(K_RESET), K(K_ACT), rd_dur(b1, sizeof b1, cur.act),
           K(K_RESET), K(K_DIM), K(K_RESET), K(K_PAS), rd_dur(b2, sizeof b2, cur.pas), K(K_RESET));

    /* apps / categories */
    rd_section(what);
    aw = 22;
    bw = rd_width - 3 - 2 - aw - 9 - 6 - 11;
    if (bw > 36) bw = 36;
    if (bw < 10) bw = 10;
    shown = cur.napps < a->top ? cur.napps : a->top;
    for (i = 0; i < shown; i++) {
        const app_t *ap = &cur.apps[i], *pv = profile_find(&prev, ap->name);
        long t = ap->act + ap->pas, top = cur.apps[0].act + cur.apps[0].pas;
        double scale = top ? 1.0 / top : 0;
        printf("  %s%2d%s  %-*s %8s  %s %s%3.0f%%%s  %s\n", K(K_DIM), i + 1, K(K_RESET), aw,
               rd_trunc(b3, sizeof b3, ap->disp, aw), rd_dur(b1, sizeof b1, t),
               rd_bar(bb, sizeof bb, ap->act * scale, ap->pas * scale, bw), K(K_DIM), 100.0 * t / total, K(K_RESET),
               rd_trend(b4, sizeof b4, t, pv ? pv->act + pv->pas : 0));
    }
    if (cur.napps > shown) {
        long rest = 0;
        for (i = shown; i < cur.napps; i++) rest += cur.apps[i].act + cur.apps[i].pas;
        printf("      %s+ %d more, %s%s\n", K(K_DIM), cur.napps - shown, rd_dur(b1, sizeof b1, rest), K(K_RESET));
    }
    rd_legend();

    breakdown_daily(&cur);

    /* hour-of-day shape */
    hour_totals(&cur, hrs);
    rd_section("Hour of day");
    printf("   %s\n", rd_spark(sp, sizeof sp, hrs, 24));
    printf("   %s00    06    12    18   23%s\n\n", K(K_DIM), K(K_RESET));

    profile_free(&cur);
    profile_free(&prev);
    return 0;
}
