/* topics: what the time was spent on, from window titles (keyword rules in config.ini [topics], with defaults). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ins.h"
#include "../render.h"

#define MAXL 512

/* Per-app and per-topic seconds are kept in flat arrays; a title is matched against the rules once, then cached by
 * its id, so a year of spans costs one rule evaluation per distinct title, not per span. */
typedef struct { char label[64]; long sec; int topic; } entry;
typedef struct {
    const stc_kv *rules; int nr;
    const char *filter;
    long total;
    long topic_sec[32];
    long *exe_sec; char (*exe_name)[48]; int cexe;
    signed char *title_topic; int ctitle;      /* -2 unknown, -1 no topic, else rule index */
} tctx;

static void on_seg(void *ctx, const stc_seg *s)
{
    tctx *c = ctx;
    long d = (long)(s->end - s->start);
    int t = -1;
    if (s->state == STC_AWAY) return;
    if (c->filter && c->filter[0] && !stc_stristr(s->name, c->filter)) return;
    c->total += d;
    if (s->title) {
        if (s->title_id >= c->ctitle) {
            int n = (int)s->title_id + 256;
            c->title_topic = realloc(c->title_topic, n);
            memset(c->title_topic + c->ctitle, -2, n - c->ctitle);
            c->ctitle = n;
        }
        t = c->title_topic[s->title_id];
        if (t == -2) {
            t = stc_topic_of(s->title, c->rules, c->nr);
            if (t > 31) t = -1;
            c->title_topic[s->title_id] = (signed char)t;
        }
    }
    if (t >= 0) { c->topic_sec[t] += d; return; }
    if (s->exe >= c->cexe) {
        int n = (int)s->exe + 64;
        c->exe_sec = realloc(c->exe_sec, n * sizeof(long));
        c->exe_name = realloc(c->exe_name, n * 48);
        memset(c->exe_sec + c->cexe, 0, (n - c->cexe) * sizeof(long));
        memset(c->exe_name + c->cexe, 0, (n - c->cexe) * 48);
        c->cexe = n;
    }
    if (!c->exe_name[s->exe][0]) { strncpy(c->exe_name[s->exe], s->name, 47); }
    c->exe_sec[s->exe] += d;
}

static int by_sec(const void *x, const void *y)
{
    long a = ((const entry *)x)->sec, b = ((const entry *)y)->sec;
    return a < b ? 1 : a > b ? -1 : 0;
}

int cmd_topics(sqlite3 *db, const stc_args *a)
{
    tctx c;
    static entry e[MAXL];
    int n = 0;
    char bb[RD_BAR];
    char b1[64], b3[96], lbl[64];
    int i, shown;
    long max;

    memset(&c, 0, sizeof c);
    c.rules = stc_topics(&c.nr);
    c.filter = a->app;
    stc_scan(db, a->from, a->to, 1, on_seg, &c);
    for (i = 0; i < c.nr && i < 32; i++)
        if (c.topic_sec[i] > 0 && n < MAXL) {
            snprintf(e[n].label, sizeof e[n].label, "%s", c.rules[i].key);
            e[n].sec = c.topic_sec[i]; e[n].topic = 1; n++;
        }
    for (i = 0; i < c.cexe; i++)
        if (c.exe_sec[i] > 0 && n < MAXL) {
            snprintf(e[n].label, sizeof e[n].label, "other: %s", c.exe_name[i]);
            e[n].sec = c.exe_sec[i]; e[n].topic = 0; n++;
        }
    free(c.exe_sec); free(c.exe_name); free(c.title_topic);
    qsort(e, n, sizeof e[0], by_sec);
    shown = n < a->top ? n : a->top;

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"topics\",\"days\":%d,\"total\":%ld,\"items\":[", a->days, c.total);
        for (i = 0; i < shown; i++) {
            printf("%s{\"label\":", i ? "," : "");
            rd_jstr(e[i].label);
            printf(",\"matched\":%s,\"seconds\":%ld}", e[i].topic ? "true" : "false", e[i].sec);
        }
        printf("]}\n");
        return 0;
    }

    snprintf(lbl, sizeof lbl, "%s", a->days == 1 ? "today" : "");
    if (a->days > 1) snprintf(lbl, sizeof lbl, "last %d days", a->days);
    rd_title("topics", lbl);
    if (!c.total) { printf("\n   %sno activity recorded in this range%s\n\n", K(K_DIM), K(K_RESET)); return 0; }

    max = e[0].sec;
    printf("\n");
    for (i = 0; i < shown; i++) {
        int matched = e[i].topic;
        printf("  %s%-26s%s %8s  %s %s%3.0f%%%s\n", matched ? K(K_BOLD) : K(K_DIM),
               rd_trunc(b3, sizeof b3, e[i].label, 26), K(K_RESET), rd_dur(b1, sizeof b1, e[i].sec),
               rd_bar(bb, sizeof bb, max ? (double)e[i].sec / max : 0, 0, 24), K(K_DIM), 100.0 * e[i].sec / c.total, K(K_RESET));
    }
    printf("\n   %stopics come from window titles; edit [topics] in config.ini (Name=keyword|keyword)%s\n\n", K(K_DIM), K(K_RESET));
    return 0;
}
