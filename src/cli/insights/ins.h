/* Shared types for reports and insights. Each insight is one file: compute (testable) + print (text and --json).
 * To add an insight: write insights/<name>.c with `int cmd_<name>(sqlite3 *, const stc_args *)` and add one line to
 * the table in registry.c. */
#ifndef INS_H
#define INS_H
#include "../../core/stc.h"

typedef struct {
    time_t from, to;        /* range [from,to) */
    int days;               /* number of calendar days requested (1 = today) */
    int json;               /* --json */
    int by_cat;             /* --by category */
    int top;                /* --top N rows */
    char app[64];           /* --app NAME filter (substring, case-insensitive) */
} stc_args;

/* Parses the options after the command name. default_days used when none given. Returns 0 or -1 (message printed). */
int args_parse(int argc, char **argv, int first, int default_days, stc_args *a);

typedef struct { sqlite3_int64 id; char name[48]; long act, pas; } app_t;

typedef struct {
    time_t from, to;
    int ndays;
    time_t *dayb;                   /* ndays+1 local midnights */
    long *day_act, *day_pas;        /* per local day */
    app_t *apps; int napps, capps;  /* unsorted until profile_sort */
    int *idx; int cidx;             /* exe id -> apps index + 1 */
    long act, pas;
    long hm[7][24];                 /* seconds by weekday (Mon=0) and hour, active+passive */
    const char *filter;
    int last_day;
} profile;

int profile_compute(sqlite3 *db, time_t from, time_t to, const char *app_filter, profile *p);
void profile_sort(profile *p);                  /* apps by total time, descending */
void profile_to_categories(profile *p);         /* replaces apps by their categories */
const app_t *profile_find(const profile *p, const char *name);
void profile_free(profile *p);
time_t prev_from(const stc_args *a);            /* start of the equally long previous window */

/* Each returns 0 on success. */
int cmd_report(sqlite3 *db, const stc_args *a);
int cmd_heatmap(sqlite3 *db, const stc_args *a);
int cmd_trends(sqlite3 *db, const stc_args *a);
int cmd_focus(sqlite3 *db, const stc_args *a);
int cmd_switches(sqlite3 *db, const stc_args *a);
int cmd_topics(sqlite3 *db, const stc_args *a);

typedef struct {
    const char *name;
    int default_days;
    const char *help;
    int (*run)(sqlite3 *, const stc_args *);
} insight;
extern const insight INSIGHTS[];
extern const int N_INSIGHTS;

/* ---- computations exposed for tests ---- */
typedef struct {
    int sessions;
    long total_session_s, longest_session_s;
    time_t longest_session_at;
    long total_break_s; int breaks;
    long focus_s;                   /* time inside single-app blocks >= 25 min */
    long screen_s;
    struct { char name[48]; time_t at; long dur; } blocks[5];   /* longest single-app blocks */
    int nblocks;
} focus_result;
void focus_compute(sqlite3 *db, time_t from, time_t to, const char *filter, focus_result *r);

#define SW_PAIRS 8
typedef struct {
    long switches;
    long active_s;
    long by_hour[24];
    struct { char from[48], to[48]; long n; } pairs[SW_PAIRS];
    int npairs;
} switch_result;
void switches_compute(sqlite3 *db, time_t from, time_t to, switch_result *r);
#endif
