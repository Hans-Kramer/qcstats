// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Fan: EMC2305 on i2c-13 at 0x2f.
//
// 11 cooling states, pwm = round(state * 255 / 10), max 5140 rpm. Below state 2
// (pwm 51) the tachometer misreads - state 1 reports ~3172 rpm - so the reading is
// flagged rather than silently trusted.
//
// Zero rpm usually means the fan is simply OFF, not broken.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

static struct { struct src pwm, rpm; int ok; } st;

static int fan_open(void)
{
    char dir[PATH_MAXLEN];

    src_init(&st.pwm);
    src_init(&st.rpm);
    if (hwmon_find(HWMON_FAN, dir, sizeof dir) != 0)
        return -1;                      /* no fan exposed: report nothing */
    src_open_abs(&st.pwm, dir, "pwm1");
    src_open_abs(&st.rpm, dir, "fan1_input");
    st.ok = 1;
    return 0;
}

static void fan_sample(struct sample *s)
{
    uint64_t pwm = 0, rpm = 0;

    if (!st.ok)
        return;
    /* Zero rpm means the fan is OFF - a real reading. An unreadable tachometer
     * must therefore not present itself as zero. */
    s->fan_duty_ok = (src_u64(&st.pwm, &pwm) == 0);
    s->fan_rpm_ok  = (src_u64(&st.rpm, &rpm) == 0);
    s->fan_duty_pct = pwm * 100 / 255;
    s->fan_rpm      = rpm;
    s->fan_ok       = 1;
    /* The tach misread below state 2 is only meaningful if duty was readable. */
    s->fan_rpm_valid = s->fan_duty_ok ? !(pwm > 0 && pwm < 51) : 0;
}

static void fan_close(void)
{
    src_close(&st.pwm);
    src_close(&st.rpm);
    st.ok = 0;
}

const struct sampler sampler_fan = {
    .name = "fan", .open = fan_open, .read = fan_sample, .close = fan_close,
};
