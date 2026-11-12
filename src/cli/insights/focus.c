/* focus: work sessions (gaps under 5 min), breaks, longest single-app blocks, share of time in blocks >= 25 min. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

#define SESSION_GAP 300     /* longer gap = new session */
#define BLOCK_GAP 60        /* same app within this gap continues a block */
#define SHORT_HOP 45        /* a visit to another app shorter than this does not break a block */
#define FOCUS_MIN 1500

typedef struct {
    focus_result *r;
    const char *filter;
    int in_session; time_t s_start, s_end;
    int have_block; char bname[48]; sqlite3_int64 bexe; time_t b_start, b_end;
} fctx;

static void close_block(fctx *c)
{
    long dur;
    int i, j;
    if (!c->have_block) return;
    dur = (long)(c->b_end - c->b_start);
    if (dur >= FOCUS_MIN) c->r->focus_s += dur;
    for (i = 0; i < 5; i++) {
        if (i >= c->r->nblocks || dur > c->r->blocks[i].dur) {
            for (j = 4; j > i; j--) c->r->blocks[j] = c->r->blocks[j - 1];
            snprintf(c->r->blocks[i].name, sizeof c->r->blocks[i].name, "%s", c->bname);
            c->r->blocks[i].at = c->b_start;
            c->r->blocks[i].dur = dur;
            if (c->r->nblocks < 5) c->r->nblocks++;
            break;
        }
    }
    c->have_block = 0;
}

static void close_session(fctx *c)
{
    long len;
    if (!c->in_session) return;
    len = (long)(c->s_end - c->s_start);
    c->r->sessions++;
    c->r->total_session_s += len;
    if (len > c->r->longest_session_s) { c->r->longest_session_s = len; c->r->longest_session_at = c->s_start; }
    c->in_session = 0;
}

static void on_seg(void *ctx, const stc_seg *s)
{
    fctx *c = ctx;
    long dur = (long)(s->end - s->start);
    if (s->state == STC_AWAY) return;
    if (c->filter && c->filter[0] && !stc_app_match(s->name, c->filter)) return;
    c->r->screen_s += dur;

    if (c->in_session && s->start - c->s_end > SESSION_GAP) {
        c->r->total_break_s += (long)(s->start - c->s_end);
        c->r->breaks++;
        close_session(c);
    }
    if (!c->in_session) { c->in_session = 1; c->s_start = s->start; }
    c->s_end = s->end;

    if (c->have_block && s->start - c->b_end <= BLOCK_GAP) {
        if (s->exe == c->bexe) { c->b_end = s->end; return; }
        if (dur < SHORT_HOP) return;            /* quick glance elsewhere: block continues */
    }
    close_block(c);
    c->have_block = 1;
    c->bexe = s->exe;
    strncpy(c->bname, s->name, sizeof c->bname - 1);   /* once per span: avoid snprintf */
    c->bname[sizeof c->bname - 1] = 0;
    c->b_start = s->start;
    c->b_end = s->end;
}

void focus_compute(sqlite3 *db, time_t from, time_t to, const char *filter, focus_result *r)
{
    fctx c;
    memset(r, 0, sizeof *r);
    memset(&c, 0, sizeof c);
    c.r = r;
    c.filter = filter;
    stc_scan(db, from, to, 0, on_seg, &c);
    close_block(&c);
    close_session(&c);
}

int cmd_focus(sqlite3 *db, const stc_args *a)
{
    focus_result r;
    char bb[RD_BAR];
    char b1[64], b2[64], b3[96], lbl[64];
    int i;
    double score;

    focus_compute(db, a->from, a->to, a->app, &r);
    score = r.screen_s ? (double)r.focus_s / r.screen_s : 0;

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"focus\",\"days\":%d,\"sessions\":%d,\"avg_session\":%ld,\"longest_session\":%ld,"
               "\"breaks\":%d,\"avg_break\":%ld,\"focus_score\":%.4f,\"blocks\":[", a->days, r.sessions,
               r.sessions ? r.total_session_s / r.sessions : 0L, r.longest_session_s, r.breaks,
               r.breaks ? r.total_break_s / r.breaks : 0L, score);
        for (i = 0; i < r.nblocks; i++) {
            printf("%s{\"app\":", i ? "," : "");
            rd_jstr(r.blocks[i].name);
            printf(",\"label\":");
            rd_jstr(stc_disp(r.blocks[i].name));
            printf(",\"start\":%I64d,\"seconds\":%ld}", (long long)r.blocks[i].at, r.blocks[i].dur);
        }
        printf("]}\n");
        return 0;
    }

    snprintf(lbl, sizeof lbl, "%s", a->days == 1 ? "today" : "");
    if (a->days > 1) snprintf(lbl, sizeof lbl, "last %d days", a->days);
    rd_title("focus", lbl);
    if (!r.screen_s) { printf("\n   %sno activity recorded in this range%s\n\n", K(K_DIM), K(K_RESET)); return 0; }

    {   struct tm tm = *localtime(&r.longest_session_at);
        strftime(b3, sizeof b3, "%a %d %b %H:%M", &tm); }
    printf("\n   %sSessions%s     %s%d%s     avg %s   longest %s%s%s %s(%s)%s\n", K(K_DIM), K(K_RESET), K(K_BOLD), r.sessions,
           K(K_RESET), rd_dur(b1, sizeof b1, r.sessions ? r.total_session_s / r.sessions : 0),
           K(K_BOLD), rd_dur(b2, sizeof b2, r.longest_session_s), K(K_RESET), K(K_DIM), b3, K(K_RESET));
    printf("   %sBreaks%s       %s%d%s     avg %s %s(a gap over 5 min ends a session)%s\n", K(K_DIM), K(K_RESET), K(K_BOLD),
           r.breaks, K(K_RESET), rd_dur(b1, sizeof b1, r.breaks ? r.total_break_s / r.breaks : 0), K(K_DIM), K(K_RESET));
    printf("   %sFocus score%s  %s%.0f%%%s  %s\n", K(K_DIM), K(K_RESET), K(K_BOLD), 100 * score, K(K_RESET),
           rd_bar(bb, sizeof bb, score, 0, 24));
    printf("   %sshare of screen time spent in single-app blocks of 25 min or more%s\n", K(K_DIM), K(K_RESET));

    rd_section("Longest focus blocks");
    for (i = 0; i < r.nblocks; i++) {
        struct tm tm = *localtime(&r.blocks[i].at);
        strftime(b3, sizeof b3, "%a %d %b %H:%M", &tm);
        printf("  %s%d%s  %-22s %8s   %s%s%s\n", K(K_DIM), i + 1, K(K_RESET), rd_trunc(b1, sizeof b1, stc_disp(r.blocks[i].name), 22),
               rd_dur(b2, sizeof b2, r.blocks[i].dur), K(K_DIM), b3, K(K_RESET));
    }
    printf("\n");
    return 0;
}
