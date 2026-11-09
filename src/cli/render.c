#include "../core/wincompat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

int rd_color, rd_unicode, rd_width = 80;

void rd_set(int color, int unicode, int width)
{
    rd_color = color;
    rd_unicode = unicode;
    rd_width = width;
}

void rd_init(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    CONSOLE_SCREEN_BUFFER_INFO ci;
    int con = GetConsoleMode(h, &mode) != 0;
    int vt = con && SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
    rd_unicode = con;
    rd_color = vt && !getenv("NO_COLOR");
    rd_width = 80;
    if (con) {
        SetConsoleOutputCP(CP_UTF8);
        if (GetConsoleScreenBufferInfo(h, &ci)) rd_width = ci.srWindow.Right - ci.srWindow.Left + 1;
    }
    if (getenv("FORCE_COLOR")) { rd_unicode = 1; rd_color = !getenv("NO_COLOR"); } /* e.g. piping through a pager */
    if (rd_width > 100) rd_width = 100;
    if (rd_width < 60) rd_width = 60;
}

const char *K(const char *code) { return rd_color ? code : ""; }

char *rd_dur(char *out, size_t cap, long s)
{
    if (s >= 3600) snprintf(out, cap, "%ldh %02ldm", s / 3600, s % 3600 / 60);
    else if (s >= 60) snprintf(out, cap, "%ldm", s / 60 + (s % 60 >= 30));
    else snprintf(out, cap, "%lds", s);
    return out;
}

static const char *EIGHTHS[] = { "", "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d", "\xe2\x96\x8c",
                                 "\xe2\x96\x8b", "\xe2\x96\x8a", "\xe2\x96\x89", "\xe2\x96\x88" };

char *rd_bar(char *out, size_t cap, double act, double pas, int width)
{
    int c;
    long au, pu;
    size_t n = 0;
    if (act < 0) act = 0;
    if (pas < 0) pas = 0;
    if (act + pas > 1) { double f = 1 / (act + pas); act *= f; pas *= f; }
    au = lround(act * width * 8);
    pu = lround(pas * width * 8);
    out[0] = 0;
    for (c = 0; c < width && n + 40 < cap; c++) {
        long fa = au - 8L * c, fp = au + pu - 8L * c;
        if (fa < 0) fa = 0;
        if (fa > 8) fa = 8;
        if (fp > 8) fp = 8;
        if (fp < 0) fp = 0;
        fp -= fa;
        if (!rd_unicode) {
            n += snprintf(out + n, cap - n, "%c", fa >= 4 ? '#' : fp >= 4 ? '+' : '.');
        } else if (fa > 0) {
            n += snprintf(out + n, cap - n, "%s%s%s", K(K_ACT), EIGHTHS[fa], K(K_RESET));
        } else if (fp >= 4) {
            n += snprintf(out + n, cap - n, "%s\xe2\x96\x92%s", K(K_PAS), K(K_RESET));
        } else {
            n += snprintf(out + n, cap - n, "%s\xc2\xb7%s", K(K_DIM), K(K_RESET));
        }
    }
    return out;
}

char *rd_spark(char *out, size_t cap, const long *v, int n)
{
    static const char *U[] = { "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
                               "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88" };
    static const char A[] = "._-~=*#@";
    long max = 0;
    int i;
    size_t o = 0;
    for (i = 0; i < n; i++) if (v[i] > max) max = v[i];
    out[0] = 0;
    for (i = 0; i < n && o + 24 < cap; i++) {
        int lv = max ? (int)((v[i] * 7 + max / 2) / max) : 0;
        if (v[i] <= 0) o += snprintf(out + o, cap - o, "%s%s%s", K(K_DIM), rd_unicode ? U[0] : ".", K(K_RESET));
        else if (rd_unicode) o += snprintf(out + o, cap - o, "%s%s%s", K(K_ACT), U[lv], K(K_RESET));
        else o += snprintf(out + o, cap - o, "%c", A[lv]);
    }
    return out;
}

char *rd_heat(char *out, size_t cap, int level)
{
    static const char *COL[] = { K_DIM, "\x1b[34m", "\x1b[36m", "\x1b[96m", "\x1b[1;97m" };
    static const char *UG[] = { "\xc2\xb7\xc2\xb7", "\xe2\x96\x91\xe2\x96\x91", "\xe2\x96\x92\xe2\x96\x92",
                                "\xe2\x96\x93\xe2\x96\x93", "\xe2\x96\x88\xe2\x96\x88" };
    static const char *AG[] = { "  ", "..", "::", "**", "##" };
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    snprintf(out, cap, "%s%s%s", K(COL[level]), rd_unicode ? UG[level] : AG[level], K(K_RESET));
    return out;
}

char *rd_trend(char *out, size_t cap, long cur, long prev)
{
    if (prev <= 0) {
        if (cur <= 0) snprintf(out, cap, "%s-%s", K(K_DIM), K(K_RESET));
        else snprintf(out, cap, "%snew%s", K(K_DIM), K(K_RESET));
    } else {
        double p = 100.0 * (cur - prev) / prev;
        if (p > -3 && p < 3) snprintf(out, cap, "%s%s%s", K(K_DIM), rd_unicode ? "\xe2\x80\x93" : "-", K(K_RESET));
        else if (p > 0) snprintf(out, cap, "%s%s +%.0f%%%s", K(K_UP), rd_unicode ? "\xe2\x96\xb2" : "^", p, K(K_RESET));
        else snprintf(out, cap, "%s%s %.0f%%%s", K(K_DOWN), rd_unicode ? "\xe2\x96\xbc" : "v", p, K(K_RESET));
    }
    return out;
}

char *rd_trunc(char *out, size_t cap, const char *s, int maxcols)
{
    size_t len = strlen(s), keep;
    int cols = 0;
    size_t i;
    for (i = 0; i < len; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) cols++;
    if (cols <= maxcols && len < cap) { strcpy(out, s); return out; }
    keep = 0; cols = 0;
    for (i = 0; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) { if (cols == maxcols - 1) break; cols++; }
        keep = i + 1;
    }
    while (keep && ((unsigned char)s[keep] & 0xC0) == 0x80) keep--;
    if (keep + 4 >= cap) keep = cap - 5;
    memcpy(out, s, keep);
    strcpy(out + keep, rd_unicode ? "\xe2\x80\xa6" : "~");
    return out;
}

static void rule(int n)
{
    int i;
    printf("%s", K(K_DIM));
    for (i = 0; i < n; i++) fputs(rd_unicode ? "\xe2\x94\x80" : "-", stdout);
    printf("%s", K(K_RESET));
}

void rd_title(const char *left, const char *right)
{
    int l = (int)strlen(left), r = right ? (int)strlen(right) : 0, pad = rd_width - 2 - l - r;
    printf("\n %s%s%s", K(K_TITLE), left, K(K_RESET));
    if (right) printf("%*s%s%s%s", pad > 1 ? pad : 1, "", K(K_DIM), right, K(K_RESET));
    printf("\n ");
    rule(rd_width - 2);
    printf("\n");
}

void rd_section(const char *name)
{
    printf("\n %s%s%s ", K(K_BOLD), name, K(K_RESET));
    rule(rd_width - 4 - (int)strlen(name));
    printf("\n");
}

void rd_legend(void)
{
    printf("   %s%s%s active   %s%s%s passive\n", K(K_ACT), rd_unicode ? "\xe2\x96\x88" : "#", K(K_RESET),
           K(K_PAS), rd_unicode ? "\xe2\x96\x92" : "+", K(K_RESET));
}

void rd_jstr(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
