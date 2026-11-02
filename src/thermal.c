// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Thermal: every temperature sensor, resolved by its name. NEVER by index - the
// five default zones sit at indices 37, 9, 33, 5 and 8 on this board, and the
// ordering is not guaranteed across boots either.
//
// Not only thermal zones. zone_table() also returns the hwmon temperatures no
// zone covers, which on this board is ath11k_hwmon - the Wi-Fi radio, and the
// hottest sensor on it. This file does not care which tree a sensor came from:
// the table hands it a directory and an attribute.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>
#include <string.h>

static struct {
    struct { struct src temp; char label[64]; } zone[ZONES_MAX];
    size_t n;
} st;

/* Open one sensor and record the label we will print for it. */
static void attach(const struct zone_tab *z)
{
    char *dash;

    if (st.n >= ZONES_MAX)
        return;
    src_init(&st.zone[st.n].temp);
    src_open_abs(&st.zone[st.n].temp, z->dir, z->attr);
    if (!src_have(&st.zone[st.n].temp))
        return;
    /* shorten "cpuss-0-0-thermal" -> "cpuss-0-0" */
    snprintf(st.zone[st.n].label, sizeof st.zone[0].label, "%s", z->type);
    dash = strstr(st.zone[st.n].label, "-thermal");
    if (dash)
        *dash = '\0';
    st.n++;
}

static int thermal_open(void)
{
    static struct zone_tab tab[ZONES_MAX];
    size_t ntab = zone_table(tab, ZONES_MAX);

    /* --all-zones follows index order. An explicit --zones list follows the order
     * the user gave, which is why this indexes the table rather than walking it. */
    if (cfg.all_zones) {
        for (size_t i = 0; i < ntab && st.n < ZONES_MAX; i++)
            attach(&tab[i]);
    } else {
        for (size_t z = 0; z < cfg.nzones && st.n < ZONES_MAX; z++) {
            int idx = zone_lookup(tab, ntab, cfg.zones[z]);
            if (idx < 0) {
                /* Naming a sensor that does not resolve is a user error
                 * worth reporting; a default that is absent is not. */
                if (cfg.zones_explicit)
                    fprintf(stderr,
                            "qcstats: no temperature sensor matches '%s'\n",
                            cfg.zones[z]);
                continue;
            }
            /*
             * The table's own label, not the string the user typed. They
             * are allowed to abbreviate ("cpuss-0-0" for
             * "cpuss-0-0-thermal"), and printing back what was typed would
             * make two runs that sampled the same sensor disagree about
             * what it is called.
             */
            attach(&tab[idx]);
        }
    }
    return st.n ? 0 : -1;
}

static void thermal_sample(struct sample *s)
{
    uint64_t mc = 0;

    /* Every resolved zone always occupies its slot: a failed read sets ok=0 rather
     * than compacting, which would shift CSV columns mid-run and drop a JSON key. */
    for (size_t i = 0; i < st.n && i < ZONES_MAX; i++) {
        snprintf(s->zone[i].label, sizeof s->zone[0].label, "%s", st.zone[i].label);
        s->zone[i].ok = (src_u64(&st.zone[i].temp, &mc) == 0);
        s->zone[i].c  = s->zone[i].ok ? mc / 1000.0 : 0.0;
        s->nzone++;
    }
}

static void thermal_close(void)
{
    for (size_t i = 0; i < st.n; i++)
        src_close(&st.zone[i].temp);
    st.n = 0;
}

const struct sampler sampler_thermal = {
    .name  = "thermal",
    .open  = thermal_open,
    .read  = thermal_sample,
    .close = thermal_close,
};
