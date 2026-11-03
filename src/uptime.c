// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Seconds since boot, from /proc/uptime.
//
// Its own sampler rather than a field hung off another, because it answers a
// different question from anything else here: every other reading describes the
// board's state right now, and this one describes how long it has been in any
// state at all. A consumer showing "up 3d 4h" cannot derive it from `t` - the wall
// clock says nothing about when the board started, and this board has no
// battery-backed RTC, so after a cold boot the two are unrelated until NTP lands.
//
// The file's first field is a float ("22385.42 89114.07"); only the whole seconds
// are kept. Sub-second precision in an uptime is noise, and an integer is what a
// consumer formats.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>

static struct src uptime_src;

static int uptime_open(void)
{
    src_init(&uptime_src);
    src_open(&uptime_src, PROC_UPTIME);
    return src_have(&uptime_src) ? 0 : -1;
}

static void uptime_sample(struct sample *s)
{
    char buf[64];
    double secs = 0.0;

    if (src_str(&uptime_src, buf, sizeof buf) != 0)
        return;                         /* uptime_ok stays 0 */
    /* A partial read is not a small uptime. sscanf must claim the field, and a
     * negative value is a misparse rather than a board that booted in the future. */
    if (sscanf(buf, "%lf", &secs) != 1 || secs < 0.0)
        return;
    s->uptime_s  = (uint64_t)secs;
    s->uptime_ok = 1;
}

static void uptime_close(void) { src_close(&uptime_src); }

const struct sampler sampler_uptime = {
    .name = "uptime", .open = uptime_open, .read = uptime_sample,
    .close = uptime_close,
};
