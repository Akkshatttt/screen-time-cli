/* Commands that read the database. Add an insight = one file + one line here. */
#include "ins.h"

const insight INSIGHTS[] = {
    { "report",   1,  "per-app time with bars and trends  [--days N] [--app X] [--by category] [--top N] [--json]", cmd_report },
    { "today",    1,  "shortcut: report for today", cmd_report },
    { "week",     7,  "shortcut: report for the last 7 days", cmd_report },
    { "month",    30, "shortcut: report for the last 30 days", cmd_report },
    { "heatmap",  30, "weekday x hour-of-day usage grid", cmd_heatmap },
    { "trends",   7,  "this period vs the previous one, weekly rhythm, night usage", cmd_trends },
    { "focus",    7,  "sessions, breaks, longest single-app blocks, focus score", cmd_focus },
    { "switches", 7,  "app switching rate, by hour, most common hops", cmd_switches },
    { "topics",   7,  "time by topic from window titles (config.ini [topics])", cmd_topics },
};
const int N_INSIGHTS = (int)(sizeof INSIGHTS / sizeof *INSIGHTS);
