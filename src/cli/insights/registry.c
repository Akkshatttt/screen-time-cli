/* Commands that read the database. Add an insight = one file + one line here. */
#include "ins.h"

const insight INSIGHTS[] = {
    { "report",   7,  "dashboard: category mix, scores, rhythm, focus, findings  [--days N] [--app X] [--by category] [--json]", cmd_report },
    { "today",    1,  "the day so far: pace vs yesterday, now, hourly strip, top apps", cmd_today },
    { "week",     7,  "per-app bars with trends and daily totals, last 7 days", cmd_breakdown },
    { "month",    30, "per-app bars with trends and daily totals, last 30 days", cmd_breakdown },
    { "heatmap",  30, "weekday x hour-of-day usage grid", cmd_heatmap },
    { "trends",   7,  "this period vs the previous one, weekly rhythm, night usage", cmd_trends },
    { "focus",    7,  "sessions, breaks, longest single-app blocks, focus score", cmd_focus },
    { "switches", 7,  "app switching rate, by hour, most common hops", cmd_switches },
    { "topics",   7,  "time by topic from window titles (config.ini [topics], or `label`)", cmd_topics },
    { "export",   30, "dump [spans|daily] as CSV or JSON  [--days N] [--format csv|json] [--out FILE]", cmd_export },
};
const int N_INSIGHTS = (int)(sizeof INSIGHTS / sizeof *INSIGHTS);
