/* switches: how often attention moves between apps, when, and the most common hops (co-use). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

#define TABLE 8192          /* open-addressing table; distinct hops beyond 4096 are ignored */
#define LINK_GAP 300        /* a longer pause between two spans is not a switch */

typedef struct { sqlite3_int64 a, b; long n; char na[48], nb[48]; int used; } hop;
typedef struct {
    switch_result *r;
    hop *t; int distinct;
    sqlite3_int64 last_exe; time_t last_end; char last_name[48];
    time_t h_from, h_to; int h;      /* cached local hour: localtime() once per hour, not per switch */
} sctx;

static void on_seg(void *ctx, const stc_seg *s)
{
    sctx *c = ctx;
    if (s->state == STC_AWAY) return;
    if (s->state == STC_ACTIVE) c->r->active_s += (long)(s->end - s->start);
    if (c->last_exe && s->exe != c->last_exe && s->start - c->last_end <= LINK_GAP) {
        unsigned h = (unsigned)((c->last_exe * 40503u) ^ (s->exe * 2654435761u)) % TABLE;
        int tries = 0;
        c->r->switches++;
        if (s->start < c->h_from || s->start >= c->h_to) {
            struct tm tm = *localtime(&s->start);
            c->h = tm.tm_hour;
            c->h_from = s->start - (tm.tm_min * 60 + tm.tm_sec);
            c->h_to = c->h_from + 3600;
        }
        c->r->by_hour[c->h]++;
        while (c->t[h].used && !(c->t[h].a == c->last_exe && c->t[h].b == s->exe) && tries++ < TABLE) h = (h + 1) % TABLE;
        if (!c->t[h].used && c->distinct < TABLE / 2) {
            c->t[h].used = 1;
            c->t[h].a = c->last_exe;
            c->t[h].b = s->exe;
            snprintf(c->t[h].na, sizeof c->t[h].na, "%s", c->last_name);
            snprintf(c->t[h].nb, sizeof c->t[h].nb, "%s", s->name);
            c->distinct++;
        }
        if (c->t[h].used) c->t[h].n++;
    }
    if (c->last_exe != s->exe) { /* copy the name only when the app changes: this runs once per span */
        strncpy(c->last_name, s->name, sizeof c->last_name - 1);
        c->last_name[sizeof c->last_name - 1] = 0;
    }
    c->last_exe = s->exe;
    c->last_end = s->end;
}

void switches_compute(sqlite3 *db, time_t from, time_t to, switch_result *r)
{
    sctx c;
    int i, k;
    memset(r, 0, sizeof *r);
    memset(&c, 0, sizeof c);
    c.r = r;
    c.t = calloc(TABLE, sizeof(hop));
    stc_scan(db, from, to, 0, on_seg, &c);
    for (k = 0; k < SW_PAIRS; k++) {            /* top hops by selection */
        int best = -1;
        for (i = 0; i < TABLE; i++)
            if (c.t[i].used && c.t[i].n > 0 && (best < 0 || c.t[i].n > c.t[best].n)) best = i;
        if (best < 0) break;
        snprintf(r->pairs[k].from, sizeof r->pairs[k].from, "%s", c.t[best].na);
        snprintf(r->pairs[k].to, sizeof r->pairs[k].to, "%s", c.t[best].nb);
        r->pairs[k].n = c.t[best].n;
        r->npairs++;
        c.t[best].n = 0;
    }
    free(c.t);
}

int cmd_switches(sqlite3 *db, const stc_args *a)
{
    switch_result r;
    char b1[64], b2[64], sp[512], lbl[64];
    int i;
    double per_h;

    switches_compute(db, a->from, a->to, &r);
    per_h = r.active_s ? r.switches / (r.active_s / 3600.0) : 0;

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"switches\",\"days\":%d,\"switches\":%ld,\"per_active_hour\":%.2f,\"by_hour\":[",
               a->days, r.switches, per_h);
        for (i = 0; i < 24; i++) printf("%s%ld", i ? "," : "", r.by_hour[i]);
        printf("],\"hops\":[");
        for (i = 0; i < r.npairs; i++) {
            printf("%s{\"from\":", i ? "," : "");
            rd_jstr(r.pairs[i].from);
            printf(",\"to\":");
            rd_jstr(r.pairs[i].to);
            printf(",\"count\":%ld}", r.pairs[i].n);
        }
        printf("]}\n");
        return 0;
    }

    snprintf(lbl, sizeof lbl, "%s", a->days == 1 ? "today" : "");
    if (a->days > 1) snprintf(lbl, sizeof lbl, "last %d days", a->days);
    rd_title("switches", lbl);
    if (!r.switches) { printf("\n   %sno app switches recorded in this range%s\n\n", K(K_DIM), K(K_RESET)); return 0; }

    printf("\n   %sSwitches%s  %s%ld%s   %s%.1f per active hour%s %s(%s of active time)%s\n", K(K_DIM), K(K_RESET), K(K_BOLD),
           r.switches, K(K_RESET), K(K_BOLD), per_h, K(K_RESET), K(K_DIM), rd_dur(b1, sizeof b1, r.active_s), K(K_RESET));
    printf("   %slow = deep focus, high = fragmented attention%s\n", K(K_DIM), K(K_RESET));

    rd_section("By hour of day");
    printf("   %s\n   %s00    06    12    18   23%s\n", rd_spark(sp, sizeof sp, r.by_hour, 24), K(K_DIM), K(K_RESET));

    rd_section("Most common hops");
    for (i = 0; i < r.npairs; i++)
        printf("  %-20s %s%s%s %-20s %s%5ld%s\n", rd_trunc(b1, sizeof b1, r.pairs[i].from, 20), K(K_DIM),
               rd_unicode ? "\xe2\x86\x92" : "->", K(K_RESET), rd_trunc(b2, sizeof b2, r.pairs[i].to, 20),
               K(K_BOLD), r.pairs[i].n, K(K_RESET));
    printf("\n");
    return 0;
}
