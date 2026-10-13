// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Board input power: an INA232 on i2c-12 at 0x40, 2 mOhm shunt.
//
// The ~20 V rail is USB-C PD, not 5 V, and is negotiated by a cypd6129 on the same bus.
// Idle reads 10.3-11.3 W at 20.07 V; about 13.0 W with the fan at 40 %.
//
// CONFIG_SENSORS_INA2XX=m: the driver is a module and may simply not be loaded, so
// an absent sensor is a normal state on a working board, not a fault.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>

static struct { struct src w, v, a; int ok; } st;

static int power_open(void)
{
    char dir[PATH_MAXLEN];

    src_init(&st.w);
    src_init(&st.v);
    src_init(&st.a);
    if (hwmon_find(HWMON_POWER, dir, sizeof dir) != 0) {
        fprintf(stderr, "qcstats: %s hwmon not found (CONFIG_SENSORS_INA2XX=m "
                        "- module may not be loaded)\n", HWMON_POWER);
        return -1;
    }
    src_open_abs(&st.w, dir, "power1_input");
    src_open_abs(&st.v, dir, "in1_input");
    src_open_abs(&st.a, dir, "curr1_input");
    st.ok = 1;
    return 0;
}

static void power_sample(struct sample *s)
{
    uint64_t uw = 0, mv = 0, ma = 0;

    if (!st.ok)
        return;
    /* The three are separate files and fail independently. Each carries its own
     * flag: reporting 0 mW next to a live voltage and current is worse than n/a. */
    s->pwr_mw_ok = (src_u64(&st.w, &uw) == 0);
    s->pwr_mv_ok = (src_u64(&st.v, &mv) == 0);
    s->pwr_ma_ok = (src_u64(&st.a, &ma) == 0);
    s->pwr_mw = uw / 1000;
    s->pwr_mv = mv;
    s->pwr_ma = ma;
    s->pwr_ok = 1;
}

static void power_close(void)
{
    src_close(&st.w);
    src_close(&st.v);
    src_close(&st.a);
    st.ok = 0;
}

const struct sampler sampler_power = {
    .name  = "power",
    .open  = power_open,
    .read  = power_sample,
    .close = power_close,
};
