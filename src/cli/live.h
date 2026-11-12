/* `screen-time-cli live`: full-screen window where each command replaces the previous output. */
#ifndef LIVE_H
#define LIVE_H
#include "insights/ins.h"

/* Runs the window; argv[2..] is the starting view (default today). Returns an exit code. */
int cmd_live(sqlite3 *db, int argc, char **argv);

/* Hotkey -> view command line ("today", "report"...), or NULL when the key is not a hotkey. */
const char *live_key_view(int ch);

/* Splits line in place on blanks into at most max tokens; returns the count. */
int live_split(char *line, char **argv, int max);

/* Keeps a scroll offset inside the output: 0 .. total-visible (0 when everything fits). */
int live_clamp(int off, int total, int visible);
#endif
