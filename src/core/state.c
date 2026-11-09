#include "stc.h"

/* Recent input = active. No input but media (fullscreen / audio) = passive.
 * No input, no media: still active (reading) until away_s, then away. Locked = away. */
int stc_classify(unsigned idle_s, int media, int locked, unsigned active_s, unsigned away_s)
{
    if (locked) return STC_AWAY;
    if (idle_s < active_s) return STC_ACTIVE;
    if (media) return STC_PASSIVE;
    return idle_s < away_s ? STC_ACTIVE : STC_AWAY;
}

/* Compares "x.y.z" strings (a leading v is ignored). Returns <0, 0, >0. */
int stc_ver_cmp(const char *a, const char *b)
{
    int i;
    if (*a == 'v') a++;
    if (*b == 'v') b++;
    for (i = 0; i < 3; i++) {
        int x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + *a++ - '0';
        while (*b >= '0' && *b <= '9') y = y * 10 + *b++ - '0';
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}
