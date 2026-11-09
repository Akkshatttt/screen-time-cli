/* heatmap: weekday x hour-of-day grid, average minutes per matching day. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ins.h"
#include "../render.h"

static const char *WD[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

int cmd_heatmap(sqlite3 *db, const stc_args *a)
{
    profile p;
    int occ[7] = { 0 }, d, h;
    double avg[7][24], max = 0;
    long hsum[24] = { 0 }, dsum[7] = { 0 };
    char b[64], lbl[64], hb[5][40];
    int bi = 0, bd = 0;

    profile_compute(db, a->from, a->to, a->app, &p);
    for (d = 0; d < p.ndays; d++) {
        struct tm tm = *localtime(&p.dayb[d]);
        occ[(tm.tm_wday + 6) % 7]++;
    }
    for (d = 0; d < 7; d++)
        for (h = 0; h < 24; h++) {
            avg[d][h] = occ[d] ? (double)p.hm[d][h] / occ[d] : 0;
            if (avg[d][h] > max) max = avg[d][h];
            hsum[h] += p.hm[d][h];
            dsum[d] += p.hm[d][h];
        }
    for (h = 1; h < 24; h++) if (hsum[h] > hsum[bi]) bi = h;
    for (d = 1; d < 7; d++) if (dsum[d] * (occ[bd] ? occ[bd] : 1) > dsum[bd] * (occ[d] ? occ[d] : 1) && occ[d]) bd = d;

    if (a->json) {
        printf("{\"schema\":1,\"command\":\"heatmap\",\"days\":%d,\"weekday_occurrences\":[", a->days);
        for (d = 0; d < 7; d++) printf("%s%d", d ? "," : "", occ[d]);
        printf("],\"seconds\":[");
        for (d = 0; d < 7; d++) {
            printf("%s[", d ? "," : "");
            for (h = 0; h < 24; h++) printf("%s%ld", h ? "," : "", p.hm[d][h]);
            printf("]");
        }
        printf("]}\n");
        profile_free(&p);
        return 0;
    }

    snprintf(lbl, sizeof lbl, "last %d days", a->days);
    rd_title("heatmap", lbl);
    if (max <= 0) { printf("\n   %sno activity recorded in this range%s\n\n", K(K_DIM), K(K_RESET)); profile_free(&p); return 0; }

    printf("\n       %s", K(K_DIM));
    for (h = 0; h < 24; h++) printf("%02d ", h);
    printf("%s\n", K(K_RESET));
    for (d = 0; d < 7; d++) {
        printf("   %s%s%s  ", K(K_DIM), WD[d], K(K_RESET));
        for (h = 0; h < 24; h++) {
            double r = avg[d][h] / max;
            int lv = avg[d][h] <= 0 ? 0 : r < 0.15 ? 1 : r < 0.4 ? 2 : r < 0.7 ? 3 : 4;
            printf("%s ", rd_heat(b, sizeof b, lv));
        }
        printf("\n");
    }
    printf("\n   %sless%s %s %s %s %s %s %smore%s   %s(darkest = %.0f min/hour, average per day)%s\n", K(K_DIM), K(K_RESET),
           rd_heat(hb[0], sizeof hb[0], 0), rd_heat(hb[1], sizeof hb[1], 1), rd_heat(hb[2], sizeof hb[2], 2),
           rd_heat(hb[3], sizeof hb[3], 3), rd_heat(hb[4], sizeof hb[4], 4), K(K_DIM), K(K_RESET), K(K_DIM), max / 60, K(K_RESET));

    rd_section("Highlights");
    printf("   busiest hour     %s%02d:00 - %02d:00%s   %s on average per day\n", K(K_BOLD), bi, (bi + 1) % 24, K(K_RESET),
           rd_dur(b, sizeof b, hsum[bi] / p.ndays));
    printf("   busiest weekday  %s%s%s   %s on average\n\n", K(K_BOLD), WD[bd], K(K_RESET),
           rd_dur(b, sizeof b, occ[bd] ? dsum[bd] / occ[bd] : 0));
    profile_free(&p);
    return 0;
}
