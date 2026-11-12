/* screen-time-cli live: alternate-screen window. Each frame runs one existing insight with its stdout captured
 * through an in-memory pipe, then repaints in place (no clear, so no flicker). Hotkeys switch views, ':' opens a
 * command line, the current view refreshes every LIVE_REFRESH_MS. The shell screen is restored on exit. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "live.h"
#include "render.h"

#define LIVE_REFRESH_MS 5000
#define FRAME_CAP 262144

static const struct { int key; const char *view; } HOTKEYS[] = {
    { 't', "today" }, { 'r', "report" }, { 'w', "week" }, { 'm', "month" }, { 'h', "heatmap" },
    { 'e', "trends" }, { 'f', "focus" }, { 's', "switches" }, { 'o', "topics" },
};

const char *live_key_view(int ch)
{
    size_t i;
    for (i = 0; i < sizeof HOTKEYS / sizeof *HOTKEYS; i++) if (HOTKEYS[i].key == ch) return HOTKEYS[i].view;
    return NULL;
}

int live_split(char *line, char **argv, int max)
{
    int n = 0;
    char *p = strtok(line, " \t");
    while (p && n < max) { argv[n++] = p; p = strtok(NULL, " \t"); }
    return n;
}

static void run_view(sqlite3 *db, char **tok, int ntok)
{
    int i, j;
    stc_args a;
    if (!ntok) { puts("empty command"); return; }
    for (i = 0; i < N_INSIGHTS && strcmp(INSIGHTS[i].name, tok[0]); i++) ;
    if (i == N_INSIGHTS) { printf("unknown view: %s\n", tok[0]); return; }
    for (j = 1; j < ntok; j++) if (!strcmp(tok[j], "--json")) { puts("--json is not available in live"); return; }
    if (!args_parse(ntok, tok, 1, INSIGHTS[i].default_days, &a)) INSIGHTS[i].run(db, &a);
}

/* Runs the view with stdout redirected into a pipe; returns the captured bytes in buf (NUL terminated). */
static int capture(sqlite3 *db, const char *cmd, char *buf, int cap)
{
    char line[256], *tok[16];
    int fds[2], saved, n = 0, r, ntok;
    snprintf(line, sizeof line, "%s", cmd);
    ntok = live_split(line, tok, 16);
    buf[0] = 0;
    if (_pipe(fds, FRAME_CAP, _O_BINARY)) return 0;
    fflush(stdout);
    saved = _dup(1);
    _dup2(fds[1], 1);
    run_view(db, tok, ntok);
    fflush(stdout);
    _dup2(saved, 1);
    _close(saved);
    _close(fds[1]);
    while (n < cap - 1 && (r = _read(fds[0], buf + n, cap - 1 - n)) > 0) n += r;
    _close(fds[0]);
    buf[n] = 0;
    return n;
}

int live_clamp(int off, int total, int visible)
{
    int max = total > visible ? total - visible : 0;
    return off < 0 ? 0 : off > max ? max : off;
}

static int count_lines(const char *buf)
{
    int n = 0;
    const char *p;
    for (p = buf; *p; p++) if (*p == '\n') n++;
    return n + (p > buf && p[-1] != '\n');
}

/* Paints lines off.. of buf into the rows above the footer row. */
static void draw(const char *buf, int rows, int cols, const char *footer, int show_cursor, int off)
{
    const char *p = buf;
    int line = 0;
    fputs("\x1b[H", stdout);
    while (off-- > 0 && (p = strchr(p, '\n')) != NULL) p++;
    if (!p) p = buf + strlen(buf);
    while (*p && line < rows - 1) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len && p[len - 1] == '\r') len--;
        fwrite(p, 1, len, stdout);
        fputs("\x1b[K\n", stdout);
        line++;
        if (!e) break;
        p = e + 1;
    }
    fputs("\x1b[0m\x1b[J", stdout);
    printf("\x1b[%d;1H\x1b[K%s%.*s%s%s", rows, show_cursor ? "" : K(K_DIM), cols - 1, footer, K(K_RESET),
           show_cursor ? "\x1b[?25h" : "\x1b[?25l");
    fflush(stdout);
}

int cmd_live(sqlite3 *db, int argc, char **argv)
{
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE), hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD imode, omode;
    static char buf[FRAME_CAP];
    char cur[256] = "today", cmd[256] = "";
    int cl = 0, editing = 0, quit = 0, i, scroll = 0;

    if (!GetConsoleMode(hin, &imode) || !GetConsoleMode(hout, &omode) || !SetConsoleMode(hout, omode | 0x0004)) {
        puts("live needs an interactive terminal (stdin and stdout must be a console).");
        return 1;
    }
    if (argc > 2) {
        cur[0] = 0;
        for (i = 2; i < argc; i++) {
            size_t l = strlen(cur);
            snprintf(cur + l, sizeof cur - l, "%s%s", l ? " " : "", argv[i]);
        }
    }
    SetConsoleMode(hin, imode & ~(DWORD)(0x0001 | 0x0002 | 0x0004)); /* no Ctrl+C handling, line input, echo */
    fputs("\x1b[?1049h\x1b[?25l", stdout);

    while (!quit) {
        CONSOLE_SCREEN_BUFFER_INFO ci;
        char footer[400], info[48] = "";
        int rows = 25, cols = 80, dirty = 0, total, visible;
        rd_init(); /* re-reads the width and colors for this frame */
        if (GetConsoleScreenBufferInfo(hout, &ci)) {
            rows = ci.srWindow.Bottom - ci.srWindow.Top + 1;
            cols = ci.srWindow.Right - ci.srWindow.Left + 1;
        }
        capture(db, cur, buf, FRAME_CAP);
        total = count_lines(buf);
        visible = rows > 2 ? rows - 1 : 1;
        scroll = live_clamp(scroll, total, visible);
        if (total > visible) snprintf(info, sizeof info, " lines %d-%d of %d (arrows, PgUp/PgDn)  ", scroll + 1, scroll + visible, total);
        if (editing) snprintf(footer, sizeof footer, ":%s", cmd);
        else snprintf(footer, sizeof footer, "%s t today  r report  w week  m month  h heatmap  e trends  f focus  s switches  o topics  : command  q quit   [%s]", info, cur);
        draw(buf, rows, cols, footer, editing, scroll);

        while (!dirty && !quit) {
            INPUT_RECORD ir[32];
            DWORD nr, k;
            if (WaitForSingleObject(hin, LIVE_REFRESH_MS) == WAIT_TIMEOUT) break;
            if (!ReadConsoleInputW(hin, ir, 32, &nr)) { quit = 1; break; }
            for (k = 0; k < nr; k++) {
                int ch;
                const char *v;
                if (ir[k].EventType == WINDOW_BUFFER_SIZE_EVENT) { dirty = 1; continue; }
                if (ir[k].EventType != KEY_EVENT || !ir[k].Event.KeyEvent.bKeyDown) continue;
                ch = ir[k].Event.KeyEvent.uChar.UnicodeChar;
                if (ch == 3) { quit = 1; break; }
                if (editing) {
                    if (ch == '\r') {
                        if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) quit = 1;
                        else if (cl) { snprintf(cur, sizeof cur, "%s", cmd); scroll = 0; }
                        editing = 0; dirty = 1;
                    } else if (ir[k].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) { editing = 0; dirty = 1; }
                    else if (ch == 8) { if (cl) cmd[--cl] = 0; dirty = 1; }
                    else if (ch >= 32 && ch < 127 && cl < 250) { cmd[cl++] = (char)ch; cmd[cl] = 0; dirty = 1; }
                } else if (ch == ':') { editing = 1; cl = 0; cmd[0] = 0; dirty = 1; }
                else if (ch == 'q' || ch == 'Q') quit = 1;
                else if (ch == ' ' || ir[k].Event.KeyEvent.wVirtualKeyCode == VK_F5) dirty = 1;
                else if ((v = live_key_view(ch)) != NULL) { snprintf(cur, sizeof cur, "%s", v); scroll = 0; dirty = 1; }
                else {   /* scrolling when the view is taller than the window */
                    WORD vk = ir[k].Event.KeyEvent.wVirtualKeyCode;
                    int page = visible > 2 ? visible - 1 : 1;
                    if (vk == VK_UP) scroll--;
                    else if (vk == VK_DOWN) scroll++;
                    else if (vk == VK_PRIOR) scroll -= page;
                    else if (vk == VK_NEXT) scroll += page;
                    else if (vk == VK_HOME) scroll = 0;
                    else if (vk == VK_END) scroll = 1 << 30;
                    else continue;
                    dirty = 1;
                }
            }
        }
    }
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    SetConsoleMode(hin, imode);
    return 0;
}
