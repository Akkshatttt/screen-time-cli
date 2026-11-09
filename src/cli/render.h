/* Terminal rendering: 16-color ANSI, Unicode blocks, ASCII/no-color fallback when piped or NO_COLOR is set. */
#ifndef RENDER_H
#define RENDER_H
#include <stddef.h>

extern int rd_color, rd_unicode, rd_width;

/* Detects the console (enables VT colors, UTF-8). Call once at startup. */
void rd_init(void);
/* Forces a mode; used by tests. */
void rd_set(int color, int unicode, int width);

/* SGR sequences, empty when color is off. */
#define K_RESET "\x1b[0m"
#define K_BOLD "\x1b[1m"
#define K_DIM "\x1b[90m"
#define K_TITLE "\x1b[1;96m"
#define K_ACT "\x1b[36m"
#define K_PAS "\x1b[35m"
#define K_UP "\x1b[33m"
#define K_DOWN "\x1b[32m"
#define K_BAD "\x1b[31m"
#define K_OK "\x1b[92m"
const char *K(const char *code);

/* A bar of up to ~40 cells needs this much room with color escapes. */
#define RD_BAR 768

/* Building blocks. All write into out (cap bytes) and return out. */
char *rd_dur(char *out, size_t cap, long seconds);                 /* 3h 05m, 42m, 35s */
char *rd_bar(char *out, size_t cap, double act, double pas, int width); /* fractions of full scale */
char *rd_spark(char *out, size_t cap, const long *v, int n);       /* one glyph per value */
char *rd_heat(char *out, size_t cap, int level);                   /* 0..4, two columns wide */
char *rd_trend(char *out, size_t cap, long cur, long prev);        /* ▲ +12% / ▼ -8% / new / - */
char *rd_trunc(char *out, size_t cap, const char *s, int maxcols); /* UTF-8 safe, adds ~ when cut */

/* Whole lines. */
void rd_title(const char *left, const char *right);                /* bold title + rule */
void rd_section(const char *name);                                 /* dim rule with a bold name */
void rd_legend(void);                                              /* active / passive swatches */
void rd_jstr(const char *s);                                       /* JSON string literal to stdout */
#endif
