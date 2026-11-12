/* `label`: give an exe a friendly name and put it in a category or a topic. Saved in labels.ini (this program owns that
 * file; config.ini is never written). Interactive picker with no arguments, direct form otherwise. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../core/stc.h"
#include "render.h"
#include "label.h"

#define MAXROWS 30

typedef struct { char name[48]; long sec; } lrow;
typedef struct { lrow *r; int n, max; } lrows;

static void on_app(void *ctx, const char *name, long act, long pas)
{
    lrows *l = ctx;
    if (l->n >= l->max) return;
    snprintf(l->r[l->n].name, sizeof l->r[0].name, "%s", name);
    l->r[l->n++].sec = act + pas;
}

static int collect(sqlite3 *db, int days, lrow *rows, int max)
{
    lrows l = { rows, 0, max };
    time_t now = time(NULL);
    if (db) stc_per_app(db, stc_day_start(now, -days), now + 1, on_app, &l);
    return l.n;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
}

static int ask(const char *prompt, char *buf, size_t n)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) { printf("\n"); return -1; }
    trim(buf);
    return 0;
}

/* Maps an answer to a stc_label_set argument: "" keeps (NULL), "-" clears (""), else the text; too long = error. */
static const char *field(char *in, size_t max, int *bad)
{
    if (!in[0]) return NULL;
    if (!strcmp(in, "-")) return "";
    if (strlen(in) > max) { printf(" %stoo long (max %u characters), left unchanged%s\n", K(K_BAD), (unsigned)max, K(K_RESET)); *bad = 1; return NULL; }
    return in;
}

static void list_rows(const lrow *rows, int n)
{
    char b1[48], b2[48], b3[48], b4[32];
    int i;
    printf("\n   %s%3s  %-24s %-18s %-14s %-12s %s%s\n", K(K_DIM), "#", "exe", "label", "category", "topic", "30 days", K(K_RESET));
    for (i = 0; i < n; i++) {
        const char *lb = stc_label_get(rows[i].name), *tp = stc_label_topic(rows[i].name);
        printf("   %s%3d%s  %-24s %-18s %-14s %-12s %s\n", K(K_DIM), i + 1, K(K_RESET), rd_trunc(b1, sizeof b1, rows[i].name, 24),
               lb ? rd_trunc(b2, sizeof b2, lb, 18) : "", rd_trunc(b3, sizeof b3, stc_category(rows[i].name), 14),
               tp ? tp : "", rd_dur(b4, sizeof b4, rows[i].sec));
    }
    printf("\n");
}

/* Picks the one known exe whose name contains arg (exact match wins). Returns 1 and fills out, else 0 and says why. */
static int resolve(sqlite3 *db, const char *arg, char *out)
{
    lrow rows[200];
    int i, n = collect(db, 365, rows, 200), hits = 0, last = -1;
    for (i = 0; i < n; i++) {
        if (!_stricmp(rows[i].name, arg)) { snprintf(out, 48, "%s", rows[i].name); return 1; }
        if (stc_stristr(rows[i].name, arg)) { hits++; last = i; }
    }
    if (hits == 1) { snprintf(out, 48, "%s", rows[last].name); return 1; }
    if (hits > 1) {
        printf("%s matches several apps; use the full exe name:\n", arg);
        for (i = 0; i < n; i++) if (stc_stristr(rows[i].name, arg)) printf("  %s\n", rows[i].name);
        return 0;
    }
    if (n) { printf("no app like \"%s\" was recorded in the last year (run: screen-time-cli label, to see the list)\n", arg); return 0; }
    snprintf(out, 48, "%s", arg);   /* no data to check against: trust the name */
    return 1;
}

static int direct(sqlite3 *db, int argc, char **argv)
{
    char exe[48];
    const char *lbl = NULL, *cat = NULL, *topic = NULL;
    int i = 3;
    if (!strcmp(argv[2], "--clear")) {
        if (argc < 4) { puts("usage: label --clear <exe>"); return 1; }
        if (!resolve(db, argv[3], exe)) return 1;
        stc_label_set(exe, "", "", "");
        printf(" %s cleared %s\n", rd_unicode ? "\xe2\x9c\x93" : "ok", exe);
        return 0;
    }
    if (!resolve(db, argv[2], exe)) return 1;
    if (i < argc && strncmp(argv[i], "--", 2)) { lbl = argv[i]; i++; }
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--category") && i + 1 < argc) cat = argv[++i];
        else if (!strcmp(argv[i], "--topic") && i + 1 < argc) topic = argv[++i];
        else { printf("unknown option: %s\n", argv[i]); return 1; }
    }
    if (!lbl && !cat && !topic) { puts("usage: label <exe> [name] [--category C] [--topic T]   (use - to clear a field)"); return 1; }
    if ((lbl && strlen(lbl) > 40) || (cat && strlen(cat) > 30) || (topic && strlen(topic) > 60)) { puts("name too long"); return 1; }
    if (lbl && !strcmp(lbl, "-")) lbl = "";
    if (cat && !strcmp(cat, "-")) cat = "";
    if (topic && !strcmp(topic, "-")) topic = "";
    if (stc_label_set(exe, lbl, cat, topic)) { puts("could not write labels.ini"); return 1; }
    printf(" %s saved: %s  %s%s  category %s%s\n", rd_unicode ? "\xe2\x9c\x93" : "ok", exe, stc_disp(exe), K(K_DIM), stc_category(exe), K(K_RESET));
    return 0;
}

/* Numbered picker: choose an app, then answer label, category and topic (blank keeps, - clears). */
int cmd_label(sqlite3 *db, int argc, char **argv)
{
    lrow rows[MAXROWS];
    char names[32][48];
    int n;
    if (argc > 2) return direct(db, argc, argv);
    if (!db) { puts("no data recorded yet, so there is nothing to label"); return 1; }
    n = collect(db, 30, rows, MAXROWS);
    if (!n) { puts("no apps recorded in the last 30 days"); return 1; }
    rd_title("label", "name apps, assign categories and topics");
    for (;;) {
        char in[160], prompt[200], exe[48], la[160], ca[160], ta[160];
        const char *lbl, *cat, *topic, *cur;
        int i, k, nc, nt, bad = 0;
        const stc_kv *tr;
        list_rows(rows, n);
        if (ask(" number to edit (blank to finish): ", in, sizeof in) || !in[0]) break;
        k = atoi(in);
        if (k < 1 || k > n) { puts(" not in the list"); continue; }
        snprintf(exe, sizeof exe, "%s", rows[k - 1].name);
        printf("\n %s%s%s\n", K(K_BOLD), exe, K(K_RESET));

        cur = stc_label_get(exe);
        snprintf(prompt, sizeof prompt, " label [%s] (blank keeps, - clears): ", cur ? cur : "none");
        if (ask(prompt, la, sizeof la)) break;
        lbl = field(la, 40, &bad);

        nc = stc_category_names(names, 31);
        printf(" categories:");
        for (i = 0; i < nc; i++) printf(" %d=%s", i + 1, names[i]);
        snprintf(prompt, sizeof prompt, "\n category [%s] (number or new name, blank keeps, - clears): ", stc_category(exe));
        if (ask(prompt, ca, sizeof ca)) break;
        k = atoi(ca);
        if (k >= 1 && k <= nc && !ca[strspn(ca, "0123456789")]) cat = names[k - 1];
        else cat = field(ca, 30, &bad);

        tr = stc_topics(&nt);
        printf(" topics:");
        for (i = 0; i < nt; i++) printf(" %s%s", tr[i].key, i + 1 < nt ? "," : "");
        cur = stc_label_topic(exe);
        snprintf(prompt, sizeof prompt, "\n topic [%s] (name, blank keeps, - clears): ", cur ? cur : "none");
        if (ask(prompt, ta, sizeof ta)) break;
        topic = field(ta, 60, &bad);

        if (stc_label_set(exe, lbl, cat, topic)) puts(" could not write labels.ini");
        else printf(" %s saved\n", rd_unicode ? "\xe2\x9c\x93" : "ok");
    }
    printf("\n");
    return 0;
}
