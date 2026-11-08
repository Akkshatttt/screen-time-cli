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
