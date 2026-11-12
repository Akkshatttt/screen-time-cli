#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ins.h"

int args_parse(int argc, char **argv, int first, int default_days, stc_args *a)
{
    int i, days = default_days;
    time_t now = time(NULL);
    memset(a, 0, sizeof *a);
    a->top = 12;
    for (i = first; i < argc; i++) {
        const char *s = argv[i];
        if (!strcmp(s, "today")) { days = 1; a->days_set = 1; }
        else if (!strcmp(s, "week")) { days = 7; a->days_set = 1; }
        else if (!strcmp(s, "month")) { days = 30; a->days_set = 1; }
        else if (!strcmp(s, "spans") || !strcmp(s, "daily")) snprintf(a->what, sizeof a->what, "%s", s);
        else if (!strcmp(s, "--json")) a->json = 1;
        else if (!strcmp(s, "--format") && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "csv") && strcmp(v, "json")) { puts("--format takes: csv | json"); return -1; }
            snprintf(a->fmt, sizeof a->fmt, "%s", v);
        } else if (!strcmp(s, "--out") && i + 1 < argc) snprintf(a->out, sizeof a->out, "%s", argv[++i]);
        else if (!strcmp(s, "--days") && i + 1 < argc) {
            days = atoi(argv[++i]);
            a->days_set = 1;
            if (days < 1 || days > 3650) { puts("--days must be between 1 and 3650"); return -1; }
        } else if (!strcmp(s, "--app") && i + 1 < argc) snprintf(a->app, sizeof a->app, "%s", argv[++i]);
        else if (!strcmp(s, "--top") && i + 1 < argc) {
            a->top = atoi(argv[++i]);
            if (a->top < 1) a->top = 1;
        } else if (!strcmp(s, "--by") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "category") || !strcmp(v, "categories")) a->by_cat = 1;
            else if (strcmp(v, "app")) { puts("--by takes: app | category"); return -1; }
        } else {
            printf("unknown option: %s\n", s);
            return -1;
        }
    }
    a->days = days;
    a->from = stc_day_start(now, -(days - 1));
    a->to = now + 1;
    return 0;
}

time_t prev_from(const stc_args *a) { return stc_day_start(a->from, -a->days); }
