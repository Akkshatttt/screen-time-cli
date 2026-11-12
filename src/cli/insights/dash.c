/* report: the dashboard. Joins fields that normally live apart (category x hour, category x focus, passive share,
 * weekday vs weekend, switches into each category) into scores, plain-language findings and anomalies.
 * dash_compute is pure (testable); cmd_report only prints. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

#define BLOCK_GAP 60        /* same category within this gap continues a stretch (as in focus.c) */
#define SHORT_HOP 45        /* a glance at another category shorter than this does not break a stretch */
#define LINK_GAP 300        /* a longer pause between two spans is not a switch (as in switches.c) */
#define FOCUS_MIN 1500      /* a stretch this long counts as deep work */

static const char *WD[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

/* ---- single-category stretches and switches into each category ---- */
typedef struct {
    catstat *cs; int *ncs;
    const char *filter;
    signed char *excat; int cex;                /* exe id -> catstat index + 1 */
    sqlite3_int64 last_exe; time_t last_end;
    int have, cat; time_t st, en;
} cctx;

static int cs_index(cctx *c, const char *name)
{
    int i;
    for (i = 0; i < *c->ncs; i++) if (!strcmp(c->cs[i].name, name)) return i;
    if (*c->ncs == MAXCAT) return MAXCAT - 1;
    snprintf(c->cs[*c->ncs].name, sizeof c->cs[0].name, "%s", name);
    return (*c->ncs)++;
}

static void close_stretch(cctx *c)
{
    catstat *x;
    long d;
    if (!c->have) return;
    x = &c->cs[c->cat];
    d = (long)(c->en - c->st);
    x->stretches++;
    x->stretch_s += d;
    if (d > x->longest) x->longest = d;
    if (d >= FOCUS_MIN) x->focus_s += d;
    c->have = 0;
}

static void on_seg(void *ctx, const stc_seg *s)
{
    cctx *c = ctx;
    long dur = (long)(s->end - s->start);
    int ci;
    if (s->state == STC_AWAY) return;
    if (c->filter && c->filter[0] && !stc_app_match(s->name, c->filter)) return;
    if (s->exe >= c->cex) {
        int n = (int)s->exe + 64;
        c->excat = realloc(c->excat, n);
        memset(c->excat + c->cex, 0, n - c->cex);
        c->cex = n;
    }
    ci = c->excat[s->exe] - 1;
    if (ci < 0) { ci = cs_index(c, stc_category(s->name)); c->excat[s->exe] = (signed char)(ci + 1); }
    if (c->last_exe && s->exe != c->last_exe && s->start - c->last_end <= LINK_GAP) c->cs[ci].sw_in++;
    if (c->have && s->start - c->en <= BLOCK_GAP) {
        if (ci == c->cat) { c->en = s->end; goto done; }
        if (dur < SHORT_HOP) goto done;
    }
    close_stretch(c);
    c->have = 1; c->cat = ci; c->st = s->start; c->en = s->end;
done:
    c->last_exe = s->exe;
    c->last_end = s->end;
}

/* ---- helpers over a profile's category arrays ---- */
static long ctot(const profile *p, int i) { return p->cat_act[i] + p->cat_pas[i]; }

static int cfind(const profile *p, const char *n)
{
    int i;
    for (i = 0; i < p->ncat; i++) if (!strcmp(p->cat_name[i], n)) return i;
    return -1;
}

static int peak_hour(const profile *p, int c)
{
    int h, b = 0;
    for (h = 1; h < 24; h++) if (p->cat_hour[c][h] > p->cat_hour[c][b]) b = h;
    return b;
}

/* Category indexes, largest first. Returns the count. */
static int order(const profile *p, int *ord)
{
    int i, j, n = p->ncat;
    for (i = 0; i < n; i++) ord[i] = i;
    for (i = 1; i < n; i++) {
        int v = ord[i];
        for (j = i - 1; j >= 0 && ctot(p, ord[j]) < ctot(p, v); j--) ord[j + 1] = ord[j];
        ord[j + 1] = v;
    }
    return n;
}

static long night_s(const profile *p)
{
    long n = 0;
    int d, h;
    for (d = 0; d < 7; d++) for (h = 0; h < 5; h++) n += p->hm[d][h];
    return n;
}

static const catstat *cs_find(const dash *d, const char *name)
{
    int i;
    for (i = 0; i < d->ncs; i++) if (!strcmp(d->cs[i].name, name)) return &d->cs[i];
    return NULL;
}

static void addf(dash *d, const char *fmt, ...)
{
    va_list ap;
    if (d->nfind >= 6) return;
    va_start(ap, fmt);
    vsnprintf(d->find[d->nfind++], sizeof d->find[0], fmt, ap);
    va_end(ap);
}

static int day_is_partial(const profile *p) { return p->to < p->dayb[p->ndays]; }

static void day_name(time_t t, char *out, size_t n)
{
    struct tm tm = *localtime(&t);
    snprintf(out, n, "%s %02d", WD[(tm.tm_wday + 6) % 7], tm.tm_mday);
}

static void findings(dash *d)
{
    const profile *cur = &d->cur, *prev = &d->prev;
    long total = cur->act + cur->pas, ptotal = prev->act + prev->pas;
    int ord[MAXCAT], n, i;
    char b1[48], b2[48];
    if (!total) return;
    n = order(cur, ord);

    /* 1. what dominates, and the shift */
    {
        int c = ord[0], pj = cfind(prev, cur->cat_name[c]);
        double sh = 100.0 * ctot(cur, c) / total;
        char shift[64] = "";
        if (pj >= 0 && ptotal > 0) {
            double dp = sh - 100.0 * ctot(prev, pj) / ptotal;
            if (fabs(dp) >= 3) snprintf(shift, sizeof shift, " (%+.0f pts vs the previous period)", dp);
        }
        if (n > 1 && ctot(cur, ord[1]) * 10 >= total)
            addf(d, "%s leads at %.0f%% of screen time%s, then %s at %.0f%%.", cur->cat_name[c], sh, shift,
                 cur->cat_name[ord[1]], 100.0 * ctot(cur, ord[1]) / total);
        else addf(d, "%s is %.0f%% of screen time%s.", cur->cat_name[c], sh, shift);
    }
    /* 2. when the two biggest categories peak */
    {
        int k, shown = 0;
        char line[160] = "";
        for (k = 0; k < n && k < 2; k++) {
            int c = ord[k];
            size_t l = strlen(line);
            if (ctot(cur, c) < 1800) continue;
            snprintf(line + l, sizeof line - l, "%s%s %s %02d:00", shown ? ", " : "", cur->cat_name[c],
                     shown ? "at" : "peaks at", peak_hour(cur, c));
            shown++;
        }
        if (shown) addf(d, "%s.", line);
    }
    /* 3. the most lean-back category */
    {
        int best = -1;
        double bs = 0.3;
        for (i = 0; i < n; i++) {
            double ps = ctot(cur, i) >= 1800 ? (double)cur->cat_pas[i] / ctot(cur, i) : 0;
            if (ps >= bs) { bs = ps; best = i; }
        }
        if (best >= 0) addf(d, "%.0f%% of %s is passive: playing or on screen without input.", 100 * bs, cur->cat_name[best]);
    }
    /* 4. late night */
    {
        long ns = night_s(cur);
        double sh = 100.0 * ns / total, dp = 0;
        char shift[64] = "";
        if (ptotal > 0) dp = sh - 100.0 * night_s(prev) / ptotal;
        if (ptotal > 0 && fabs(dp) >= 3) snprintf(shift, sizeof shift, ", %s %.0f pts", dp > 0 ? "up" : "down", fabs(dp));
        if (total >= 3600 && (sh >= 8 || fabs(dp) >= 5))
            addf(d, "Late-night use (00:00-05:00) is %.0f%% of screen time%s.", sh, shift);
    }
    /* 5. weekend vs weekday */
    {
        long wd = 0, we = 0;
        int nwd = 0, nwe = 0, k, last = cur->ndays - (day_is_partial(cur) ? 1 : 0);
        for (k = 0; k < last; k++) {
            struct tm tm = *localtime(&cur->dayb[k]);
            long v = cur->day_act[k] + cur->day_pas[k];
            if (v <= 0) continue;
            if (tm.tm_wday == 0 || tm.tm_wday == 6) { we += v; nwe++; } else { wd += v; nwd++; }
        }
        if (nwd && nwe) {
            double r = ((double)we / nwe) / ((double)wd / nwd);
            if (r >= 1.3 || r <= 0.7)
                addf(d, "Weekend days average %s against %s on weekdays (%.1fx).", rd_dur(b1, sizeof b1, we / nwe),
                     rd_dur(b2, sizeof b2, wd / nwd), r);
        }
    }
    /* 6. where focus holds and where it breaks */
    {
        int hi = -1, lo = -1;
        long hav = 0, lav = 0;
        for (i = 0; i < d->ncs; i++) {
            int c = cfind(cur, d->cs[i].name);
            long av;
            if (c < 0 || ctot(cur, c) < 1800 || !d->cs[i].stretches) continue;
            av = d->cs[i].stretch_s / d->cs[i].stretches;
            if (hi < 0 || av > hav) { hi = i; hav = av; }
            if (lo < 0 || av < lav) { lo = i; lav = av; }
        }
        if (hi >= 0 && lo >= 0 && hi != lo && hav >= 2 * lav)
            addf(d, "%s holds your longest stretches (avg %s); %s is the most fragmented (avg %s).", d->cs[hi].name,
                 rd_dur(b1, sizeof b1, hav), d->cs[lo].name, rd_dur(b2, sizeof b2, lav));
    }
}

static void anomalies(dash *d)
{
    const profile *cur = &d->cur, *prev = &d->prev;
    int i, k, last = cur->ndays - (day_is_partial(cur) ? 1 : 0), cnt = 0;
    double sum = 0, sq = 0, mean = 0, sd = 0;
    char b1[48], dn[24];
    char *used = calloc((size_t)(last > cur->napps ? last : cur->napps) + 1, 1);   /* already reported */
    for (i = 0; i < last; i++) {
        long v = cur->day_act[i] + cur->day_pas[i];
        if (v > 0) { sum += v; sq += (double)v * v; cnt++; }
    }
    d->s_consist = -1;
    if (cnt >= 4) {
        mean = sum / cnt;
        sd = sqrt(sq / cnt - mean * mean);
        if (mean > 0) d->s_consist = (int)lround(100 * (sd / mean >= 1 ? 0 : 1 - sd / mean));
    }
    for (k = 0; k < 3 && sd > 0; k++) {     /* up to three days furthest from the mean, beyond 1.5 sd */
        int best = -1;
        double bz = 1.5;
        for (i = 0; i < last; i++) {
            long v = cur->day_act[i] + cur->day_pas[i];
            double z = v > 0 && !used[i] && fabs(v - mean) >= 0.25 * mean ? fabs(v - mean) / sd : 0;   /* and at least 25% off */
            if (z > bz) { bz = z; best = i; }
        }
        if (best < 0) break;
        used[best] = 1;
        day_name(cur->dayb[best], dn, sizeof dn);
        snprintf(d->anom[d->nanom++], sizeof d->anom[0], "%s was %s, %.1fx your daily average.", dn,
                 rd_dur(b1, sizeof b1, cur->day_act[best] + cur->day_pas[best]), (cur->day_act[best] + cur->day_pas[best]) / mean);
    }
    memset(used, 0, (size_t)(last > cur->napps ? last : cur->napps) + 1);
    for (k = 0; k < 3 && d->nanom < 6; k++) {   /* apps well above or below the previous period */
        int best = -1;
        long bd = 0;
        for (i = 0; i < cur->napps; i++) {
            const app_t *pv = profile_find(prev, cur->apps[i].name);
            long t = cur->apps[i].act + cur->apps[i].pas, pt = pv ? pv->act + pv->pas : 0, dl;
            int up = t >= 1200 && t >= 2 * pt && (pt >= 1200 || t >= 3600), down = pt >= 1200 && t * 2 <= pt;
            if (used[i] || !(up || down)) continue;
            dl = t > pt ? t - pt : pt - t;
            if (dl > bd) { bd = dl; best = i; }
        }
        if (best < 0) break;
        used[best] = 1;
        {   const app_t *ap = &cur->apps[best], *pv = profile_find(prev, ap->name);
            long t = ap->act + ap->pas, pt = pv ? pv->act + pv->pas : 0;
            if (pt > 0) snprintf(d->anom[d->nanom++], sizeof d->anom[0], "%s is %s, %.1fx the previous period.",
                                 ap->disp, rd_dur(b1, sizeof b1, t), (double)t / pt);
            else snprintf(d->anom[d->nanom++], sizeof d->anom[0], "%s is new: %s, none in the previous period.",
                          ap->disp, rd_dur(b1, sizeof b1, t));
        }
    }
    free(used);
}

int dash_compute(sqlite3 *db, const stc_args *a, dash *d)
{
    time_t pf = prev_from(a);
    cctx c;
    long total;
    int i;
    memset(d, 0, sizeof *d);
    profile_compute(db, a->from, a->to, a->app, &d->cur);
    profile_compute(db, pf, pf + (a->to - a->from), a->app, &d->prev);
    profile_sort(&d->cur);
    focus_compute(db, a->from, a->to, a->app, &d->fr);
    switches_compute(db, a->from, a->to, &d->sw);
    memset(&c, 0, sizeof c);
    c.cs = d->cs; c.ncs = &d->ncs; c.filter = a->app;
    stc_scan(db, a->from, a->to, 0, on_seg, &c);
    close_stretch(&c);
    free(c.excat);

    total = d->cur.act + d->cur.pas;
    d->s_focus = d->fr.screen_s ? (int)lround(100.0 * d->fr.focus_s / d->fr.screen_s) : -1;
    d->frag = d->sw.active_s ? d->sw.switches / (d->sw.active_s / 3600.0) : 0;
    d->s_variety = -1;
    if (total > 0) {                        /* entropy of the category shares; 6 evenly used categories = 100 */
        double h = 0;
        for (i = 0; i < d->cur.ncat; i++) {
            double p = (double)ctot(&d->cur, i) / total;
            if (p > 0) h -= p * log(p);
        }
        d->s_variety = (int)lround(100 * (h / log(6) > 1 ? 1 : h / log(6)));
    }
    findings(d);
    anomalies(d);
    return 0;
}

void dash_free(dash *d)
{
    profile_free(&d->cur);
    profile_free(&d->prev);
}

/* ---- printing ---- */
static void range_text(const stc_args *a, char *out, size_t n)
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

static void json_extras(const stc_args *a, const dash *d)
{
    int i;
    (void)a;
    printf(",\"scores\":{\"focus\":%d,\"fragmentation\":%.2f,\"variety\":%d,\"consistency\":%d},\"categories\":[",
           d->s_focus, d->frag, d->s_variety, d->s_consist);
    for (i = 0; i < d->cur.ncat; i++) {
        const catstat *cs = cs_find(d, d->cur.cat_name[i]);
        int pj = cfind(&d->prev, d->cur.cat_name[i]);
        printf("%s{\"name\":", i ? "," : "");
        rd_jstr(d->cur.cat_name[i]);
        printf(",\"active\":%ld,\"passive\":%ld,\"previous\":%ld,\"stretches\":%ld,\"longest_stretch\":%ld,"
               "\"focus_seconds\":%ld,\"switches_in\":%ld,\"weekend\":%ld,\"by_hour\":[",
               d->cur.cat_act[i], d->cur.cat_pas[i], pj >= 0 ? ctot(&d->prev, pj) : 0L, cs ? cs->stretches : 0L,
               cs ? cs->longest : 0L, cs ? cs->focus_s : 0L, cs ? cs->sw_in : 0L, d->cur.cat_wk[i][1]);
        {   int h;
            for (h = 0; h < 24; h++) printf("%s%ld", h ? "," : "", d->cur.cat_hour[i][h]); }
        printf("]}");
    }
    printf("],\"findings\":[");
    for (i = 0; i < d->nfind; i++) { printf("%s", i ? "," : ""); rd_jstr(d->find[i]); }
    printf("],\"anomalies\":[");
    for (i = 0; i < d->nanom; i++) { printf("%s", i ? "," : ""); rd_jstr(d->anom[i]); }
    printf("]}\n");
}

static const char *grade(double frag)
{
    return frag < 15 ? "calm" : frag < 30 ? "busy" : "scattered";
}

static void score_line(const char *name, int v, const char *note)
{
    char bb[RD_BAR];
    if (v < 0) { printf("   %s%-14s%s %s%s%s\n", K(K_DIM), name, K(K_RESET), K(K_DIM), "not enough data yet", K(K_RESET)); return; }
    printf("   %s%-14s%s %s%3d%s  %s  %s%s%s\n", K(K_DIM), name, K(K_RESET), K(K_BOLD), v, K(K_RESET),
           rd_bar(bb, sizeof bb, v / 100.0, 0, 16), K(K_DIM), note, K(K_RESET));
}

int cmd_report(sqlite3 *db, const stc_args *a0)
{
    stc_args a = *a0;
    dash *d;
    sqlite3_stmt *st;
    int shrunk = 0, i, n, ord[MAXCAT], aw = 22, bw, shown;
    char lbl[96], b1[96], b2[96], b3[96], b4[96], bb[RD_BAR], hb[48];
    long total, ptotal;

    if (!a.days_set && !sqlite3_prepare_v2(db, "SELECT MIN(start_ts) FROM span", -1, &st, 0)) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
            time_t first = (time_t)sqlite3_column_int64(st, 0), now = time(NULL);
            int have = 1;
            while (have < 3650 && stc_day_start(first, have) <= now) have++;
            if (have < a.days) { a.days = have; a.from = stc_day_start(now, -(have - 1)); shrunk = 1; }
        }
        sqlite3_finalize(st);
    }
    d = malloc(sizeof *d);
    dash_compute(db, &a, d);

    if (a.json) {
        if (a.by_cat) { profile_to_categories(&d->cur); profile_to_categories(&d->prev); }
        breakdown_json(&a, &d->cur, &d->prev, 1);
        json_extras(&a, d);
        dash_free(d); free(d);
        return 0;
    }

    total = d->cur.act + d->cur.pas;
    ptotal = d->prev.act + d->prev.pas;
    range_text(&a, lbl, sizeof lbl);
    rd_title("report", lbl);
    if (!total) {
        printf("\n   %sno activity recorded in this range%s%s\n\n", K(K_DIM), K(K_RESET), a.app[0] ? " for that app" : "");
        dash_free(d); free(d);
        return 0;
    }
    printf("\n   %sTotal%s    %s%s%s     %s  %svs %s%s\n", K(K_DIM), K(K_RESET), K(K_BOLD), rd_dur(b1, sizeof b1, total),
           K(K_RESET), rd_trend(b2, sizeof b2, total, ptotal), K(K_DIM), a.days == 1 ? "yesterday" : "previous period", K(K_RESET));
    printf("   %sActive%s   %s%s%s    %sPassive%s  %s%s%s", K(K_DIM), K(K_RESET), K(K_ACT), rd_dur(b1, sizeof b1, d->cur.act),
           K(K_RESET), K(K_DIM), K(K_RESET), K(K_PAS), rd_dur(b2, sizeof b2, d->cur.pas), K(K_RESET));
    if (a.days > 1) printf("    %savg%s %s/day", K(K_DIM), K(K_RESET), rd_dur(b3, sizeof b3, total / a.days));
    printf("\n");
    if (shrunk) printf("   %sonly %d day%s recorded so far, so the 7-day default was shortened%s\n", K(K_DIM), a.days,
                       a.days == 1 ? "" : "s", K(K_RESET));

    if (d->nfind) {
        rd_section("What stands out");
        for (i = 0; i < d->nfind; i++) printf("  %s%s%s %s\n", K(K_ACT), rd_unicode ? "\xe2\x80\xa2" : "-", K(K_RESET), d->find[i]);
    }

    rd_section("Scores");
    snprintf(b1, sizeof b1, "screen time in 25+ min single-app blocks");
    score_line("Focus", d->s_focus, b1);
    printf("   %s%-14s%s %s%3.0f%s  %s%s, switches per active hour%s\n", K(K_DIM), "Fragmentation", K(K_RESET), K(K_BOLD),
           d->frag, K(K_RESET), K(K_DIM), grade(d->frag), K(K_RESET));
    score_line("Variety", d->s_variety, "how evenly time spreads across categories");
    score_line("Consistency", d->s_consist, "how steady the days are (needs 4 full days)");

    /* categories */
    rd_section("Categories");
    n = order(&d->cur, ord);
    bw = rd_width - 3 - 2 - aw - 9 - 6 - 11;
    if (bw > 36) bw = 36;
    if (bw < 10) bw = 10;
    shown = n < 8 ? n : 8;
    for (i = 0; i < shown; i++) {
        int c = ord[i], pj = cfind(&d->prev, d->cur.cat_name[c]);
        long t = ctot(&d->cur, c), top = ctot(&d->cur, ord[0]);
        double sc = top ? 1.0 / top : 0;
        printf("  %-*s %8s  %s %s%3.0f%%%s  %s\n", aw, rd_trunc(b3, sizeof b3, d->cur.cat_name[c], aw),
               rd_dur(b1, sizeof b1, t), rd_bar(bb, sizeof bb, d->cur.cat_act[c] * sc, d->cur.cat_pas[c] * sc, bw),
               K(K_DIM), 100.0 * t / total, K(K_RESET),
               rd_trend(b4, sizeof b4, t, pj >= 0 ? ctot(&d->prev, pj) : 0));
    }
    rd_legend();

    /* when each category peaks */
    rd_section("Rhythm: when each category peaks");
    printf("  %*s%-12s%-12s%-12s%-12s\n", 15, "", "00", "06", "12", "18");
    for (i = 0; i < n && i < 5; i++) {
        int c = ord[i], h;
        long mx = 0;
        for (h = 0; h < 24; h++) if (d->cur.cat_hour[c][h] > mx) mx = d->cur.cat_hour[c][h];
        printf("  %-14s ", rd_trunc(b3, sizeof b3, d->cur.cat_name[c], 14));
        for (h = 0; h < 24; h++) {
            long v = d->cur.cat_hour[c][h];
            printf("%s", rd_heat(hb, sizeof hb, v > 0 && mx ? 1 + (int)((v * 3) / mx) : 0));
        }
        printf("\n");
    }
    if (a.days >= 7) {
        int nwe = 0, k, any = 0;
        for (k = 0; k < d->cur.ndays; k++) { struct tm tm = *localtime(&d->cur.dayb[k]); if (tm.tm_wday == 0 || tm.tm_wday == 6) nwe++; }
        printf("\n   %sshare of each category's time on weekends (even use = %.0f%%)%s\n   ", K(K_DIM), 100.0 * nwe / d->cur.ndays, K(K_RESET));
        for (i = 0; i < n && i < 4; i++) {
            int c = ord[i];
            long w = d->cur.cat_wk[c][0] + d->cur.cat_wk[c][1];
            if (w <= 0) continue;
            printf("%s%s %.0f%%", any++ ? "   " : "", d->cur.cat_name[c], 100.0 * d->cur.cat_wk[c][1] / w);
        }
        printf("\n");
    }

    /* focus by category */
    rd_section("Focus by category");
    printf("   %s%-16s %12s %9s %7s %12s%s\n", K(K_DIM), "", "avg stretch", "longest", "deep", "switches/hr", K(K_RESET));
    for (i = 0; i < n && i < 6; i++) {
        int c = ord[i];
        const catstat *cs = cs_find(d, d->cur.cat_name[c]);
        long t = ctot(&d->cur, c);
        if (t < 600 || !cs || !cs->stretches) continue;
        snprintf(b1, sizeof b1, "%.0f", d->cur.cat_act[c] >= 900 ? cs->sw_in / (d->cur.cat_act[c] / 3600.0) : -1.0);
        printf("   %-16s %12s %9s %6.0f%% %12s\n", rd_trunc(b3, sizeof b3, d->cur.cat_name[c], 16),
               rd_dur(b2, sizeof b2, cs->stretch_s / cs->stretches), rd_dur(b4, sizeof b4, cs->longest),
               100.0 * cs->focus_s / t, d->cur.cat_act[c] >= 900 ? b1 : "-");
    }
    printf("   %sstretch = time in one category without leaving it; deep = share in stretches of 25 min or more%s\n",
           K(K_DIM), K(K_RESET));

    if (d->nanom) {
        rd_section("Unusual");
        for (i = 0; i < d->nanom; i++) printf("  %s%s%s %s\n", K(K_UP), rd_unicode ? "\xe2\x80\xa2" : "-", K(K_RESET), d->anom[i]);
    }

    if (!a.by_cat) {
        rd_section("Top apps");
        shown = d->cur.napps < (a.top < 8 ? a.top : 8) ? d->cur.napps : (a.top < 8 ? a.top : 8);
        for (i = 0; i < shown; i++) {
            const app_t *ap = &d->cur.apps[i];
            long t = ap->act + ap->pas, top = d->cur.apps[0].act + d->cur.apps[0].pas;
            double sc = top ? 1.0 / top : 0;
            if (t <= 0) continue;
            printf("  %s%2d%s  %-*s %8s  %s %s%3.0f%%%s\n", K(K_DIM), i + 1, K(K_RESET), aw,
                   rd_trunc(b3, sizeof b3, ap->disp, aw), rd_dur(b1, sizeof b1, t),
                   rd_bar(bb, sizeof bb, ap->act * sc, ap->pas * sc, bw), K(K_DIM), 100.0 * t / total, K(K_RESET));
        }
        if (d->cur.napps > shown) printf("      %s+ %d more apps; see: screen-time-cli week%s\n", K(K_DIM), d->cur.napps - shown, K(K_RESET));
    }
    breakdown_daily(&d->cur);
    printf("\n");
    dash_free(d);
    free(d);
    return 0;
}
