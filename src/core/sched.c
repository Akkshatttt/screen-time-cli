/* Pure scheduling rules for the daemon (no Win32), so they can be unit tested. */
#include "stc.h"

/* ---- media probe cadence ----
 * The audio probe is the most expensive thing the daemon does, so it runs at most once per heartbeat, only while the
 * user has been idle, and right away when the foreground app changes (last_probe_s reset to 0 by the caller). */
int stc_probe_due(time_t now_s, time_t last_probe_s, unsigned beat_s)
{
    if (!last_probe_s) return 1;
    return now_s - last_probe_s >= (time_t)(beat_s > 2 ? beat_s - 2 : 0);
}
