/* Pure scheduling rules for the daemon (no Win32), so they can be unit tested. */
#include "stc.h"

/* ---- title-event storm detection ----
 * An animated window title (spinner, progress) fires an event many times a second, and every event wakes the daemon.
 * More than STORM_EVENTS in STORM_WINDOW_MS means a storm: stop listening for a while, with exponential backoff. */
#define STORM_WINDOW_MS 3000u
#define STORM_EVENTS 15
#define STORM_BASE_MS 30000u
#define STORM_MAX_LEVEL 4          /* 30 s, 60 s, 120 s, 240 s, 480 s */

void stc_storm_reset(stc_storm *s) { s->win_start = 0; s->count = 0; s->level = 0; s->started = 0; }

int stc_storm_event(stc_storm *s, unsigned now_ms)
{
    if (!s->started || now_ms - s->win_start >= STORM_WINDOW_MS) {   /* unsigned math survives tick wraparound */
        s->win_start = now_ms;
        s->count = 0;
        s->started = 1;
    }
    if (++s->count > STORM_EVENTS) {
        s->count = 0;
        s->started = 0;
        return 1;
    }
    return 0;
}

unsigned stc_storm_backoff_ms(stc_storm *s)
{
    unsigned ms = STORM_BASE_MS << s->level;
    if (s->level < STORM_MAX_LEVEL) s->level++;
    return ms;
}

/* A quiet stretch after a backoff ends the escalation. */
void stc_storm_calm(stc_storm *s) { s->level = 0; }

/* ---- media probe cadence ----
 * The audio probe is the most expensive thing the daemon does, so it runs at most once per heartbeat, only while the
 * user has been idle, and right away when the foreground app changes (last_probe_s reset to 0 by the caller). */
int stc_probe_due(time_t now_s, time_t last_probe_s, unsigned beat_s)
{
    if (!last_probe_s) return 1;
    return now_s - last_probe_s >= (time_t)(beat_s > 2 ? beat_s - 2 : 0);
}
