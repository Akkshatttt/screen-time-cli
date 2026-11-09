/* One pass over the spans: per-app totals, per-day totals, weekday x hour matrix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ins.h"

static void add_app(profile *p, const stc_seg *s, long d)
{
    int ai;
    if (s->exe >= p->cidx) {
        int n = (int)s->exe + 64;
        p->idx = realloc(p->idx, n * sizeof(int));
        memset(p->idx + p->cidx, 0, (n - p->cidx) * sizeof(int));
        p->cidx = n;
    }
    ai = p->idx[s->exe] - 1;
    if (ai < 0) {
        if (p->napps == p->capps) {
            p->capps = p->capps ? p->capps * 2 : 32;
            p->apps = realloc(p->apps, p->capps * sizeof(app_t));
        }
        ai = p->napps++;
        memset(&p->apps[ai], 0, sizeof(app_t));
        p->apps[ai].id = s->exe;
        snprintf(p->apps[ai].name, sizeof p->apps[ai].name, "%s", s->name);
        p->idx[s->exe] = ai + 1;
    }
    if (s->state == STC_ACTIVE) p->apps[ai].act += d; else p->apps[ai].pas += d;
}

static void on_seg(void *ctx, const stc_seg *s)
{
    profile *p = ctx;
    time_t t, e;
    int k;
    if (s->state == STC_AWAY) return;
    if (p->filter && p->filter[0] && !stc_stristr(s->name, p->filter)) return;
    add_app(p, s, (long)(s->end - s->start));
    if (s->state == STC_ACTIVE) p->act += (long)(s->end - s->start); else p->pas += (long)(s->end - s->start);

    while (p->last_day < p->ndays - 1 && s->start >= p->dayb[p->last_day + 1]) p->last_day++;
    k = p->last_day;
    for (t = s->start; t < s->end && k < p->ndays; k++, t = e) {
        e = s->end < p->dayb[k + 1] ? s->end : p->dayb[k + 1];
        if (s->state == STC_ACTIVE) p->day_act[k] += (long)(e - t); else p->day_pas[k] += (long)(e - t);
    }
    for (t = s->start; t < s->end; t = e) {
        struct tm tm = *localtime(&t);
        time_t hend = t + 3600 - (tm.tm_min * 60 + tm.tm_sec);
        e = s->end < hend ? s->end : hend;
        p->hm[(tm.tm_wday + 6) % 7][tm.tm_hour] += (long)(e - t);
    }
}

int profile_compute(sqlite3 *db, time_t from, time_t to, const char *app_filter, profile *p)
{
    int d, n = 0;
    memset(p, 0, sizeof *p);
    p->from = from;
    p->to = to;
    p->filter = app_filter;
    while (n < 4000 && stc_day_start(from, n) < to) n++;
    p->ndays = n ? n : 1;
    p->dayb = malloc((p->ndays + 1) * sizeof(time_t));
    p->day_act = calloc(p->ndays, sizeof(long));
    p->day_pas = calloc(p->ndays, sizeof(long));
    for (d = 0; d <= p->ndays; d++) p->dayb[d] = stc_day_start(from, d);
    return stc_scan(db, from, to, 0, on_seg, p);
}

static int by_total(const void *x, const void *y)
{
    const app_t *a = x, *b = y;
    long ta = a->act + a->pas, tb = b->act + b->pas;
    return ta < tb ? 1 : ta > tb ? -1 : strcmp(a->name, b->name);
}

void profile_sort(profile *p) { qsort(p->apps, p->napps, sizeof(app_t), by_total); }

void profile_to_categories(profile *p)
{
    app_t *cat = calloc(p->napps ? p->napps : 1, sizeof(app_t));
    int i, j, n = 0;
    for (i = 0; i < p->napps; i++) {
        const char *c = stc_category(p->apps[i].name);
        for (j = 0; j < n && strcmp(cat[j].name, c); j++) ;
        if (j == n) { snprintf(cat[n].name, sizeof cat[n].name, "%s", c); n++; }
        cat[j].act += p->apps[i].act;
        cat[j].pas += p->apps[i].pas;
    }
    free(p->apps);
    p->apps = cat;
    p->napps = p->capps = n ? n : 1;
    p->napps = n;
    profile_sort(p);
}

const app_t *profile_find(const profile *p, const char *name)
{
    int i;
    for (i = 0; i < p->napps; i++) if (!strcmp(p->apps[i].name, name)) return &p->apps[i];
    return NULL;
}

void profile_free(profile *p)
{
    free(p->dayb); free(p->day_act); free(p->day_pas); free(p->apps); free(p->idx);
    memset(p, 0, sizeof *p);
}
