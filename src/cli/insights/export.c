/* export: dump spans or daily totals as CSV or JSON, to stdout or a new file (--out; never overwrites). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ins.h"

typedef struct { FILE *f; int json; long n; } ectx;

static void iso(time_t t, char *out, size_t n)
{
    struct tm tm = *localtime(&t);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
}

/* One field: JSON string or CSV cell (quoted when needed). */
static void field(FILE *f, int json, const char *s)
{
    if (json) {
        fputc('"', f);
        for (; *s; s++) {
            unsigned char c = (unsigned char)*s;
            if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
            else if (c < 0x20) fprintf(f, "\\u%04x", c);
            else fputc(c, f);
        }
        fputc('"', f);
    } else if (strpbrk(s, ",\"\r\n")) {
        fputc('"', f);
        for (; *s; s++) { if (*s == '"') fputc('"', f); fputc(*s, f); }
        fputc('"', f);
    } else fputs(s, f);
}

static void on_seg(void *ctx, const stc_seg *s)
{
    ectx *c = ctx;
    char b1[24], b2[24];
    static const char *ST[] = { "active", "passive", "away" };
    iso(s->start, b1, sizeof b1);
    iso(s->end, b2, sizeof b2);
    if (c->json) fprintf(c->f, "%s{\"start\":\"%s\",\"end\":\"%s\",\"seconds\":%ld,\"state\":\"%s\",\"app\":", c->n ? ",\n" : "", b1, b2,
                         (long)(s->end - s->start), ST[s->state % 3]);
    else fprintf(c->f, "%s,%s,%ld,%s,", b1, b2, (long)(s->end - s->start), ST[s->state % 3]);
    field(c->f, c->json, s->name);
    fputs(c->json ? ",\"label\":" : ",", c->f);
    field(c->f, c->json, stc_disp(s->name));
    fputs(c->json ? ",\"category\":" : ",", c->f);
    field(c->f, c->json, stc_category(s->name));
    fputs(c->json ? "}" : "\n", c->f);
    c->n++;
}

int cmd_export(sqlite3 *db, const stc_args *a)
{
    ectx c = { stdout, !strcmp(a->fmt, "json"), 0 };
    int spans = !strcmp(a->what, "spans");
    if (a->out[0]) {
        FILE *t = fopen(a->out, "rb");
        if (t) { fclose(t); printf("%s already exists; choose a new file name\n", a->out); return 1; }
        c.f = fopen(a->out, "wb");
        if (!c.f) { printf("cannot write %s\n", a->out); return 1; }
    }
    if (c.json) fputs("[\n", c.f);
    if (spans) {
        if (!c.json) fputs("start,end,seconds,state,app,label,category\n", c.f);
        stc_scan(db, a->from, a->to, 0, on_seg, &c);
    } else {
        profile p;
        int i;
        if (!c.json) fputs("date,active_seconds,passive_seconds\n", c.f);
        profile_compute(db, a->from, a->to, a->app, &p);
        for (i = 0; i < p.ndays; i++) {
            struct tm tm = *localtime(&p.dayb[i]);
            if (c.json) fprintf(c.f, "%s{\"date\":\"%04d-%02d-%02d\",\"active\":%ld,\"passive\":%ld}", i ? ",\n" : "",
                                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, p.day_act[i], p.day_pas[i]);
            else fprintf(c.f, "%04d-%02d-%02d,%ld,%ld\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, p.day_act[i], p.day_pas[i]);
        }
        profile_free(&p);
    }
    if (c.json) fputs("\n]\n", c.f);
    if (c.f != stdout) { fclose(c.f); printf("wrote %s\n", a->out); }
    return 0;
}
