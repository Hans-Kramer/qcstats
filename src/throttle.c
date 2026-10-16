// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// LMh/DCVSh throttle counters from /proc/interrupts: dcvsh-irq-{0,2,4}, named per
// policy, so events are attributable to a cluster.
//
// These count EVENTS, not depth. Do not present them as a throttle level.
//
// They read 0 in virtually all normal use, and that is a true reading, not a broken
// sensor: the fan's active trip is 38 C while the CPU's own first passive trip is 126 C
// (then 130 and 135), so with a working fan there is nothing to count. A load test
// confirmed it - the counters stayed at 0 under the heaviest load available.
//
// 115 C is the first passive trip on the GPU and NSP zones, not the CPU. The lowest
// protective trip anywhere on this board is 105 C, on the two PMIC zones.
//
// dcvsh_freq_limit does NOT exist on this kernel, so nothing here reads it.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static struct src interrupts;

static int throttle_open(void)
{
    src_init(&interrupts);
    src_open(&interrupts, PROC_INTERRUPTS);
    return src_have(&interrupts) ? 0 : -1;
}

/* Said once, not every tick: a buffer too small for this machine is a standing condition,
 * and a sampler that repeats it drowns the stream it is meant to carry. */
static int warned_trunc;

static void throttle_sample(struct sample *s)
{
    int rc;
    static char blob[65536];            /* /proc/interrupts is large on 8 cores */
    char *line, *save = NULL;

    /* 0 events means nothing throttled, which is a real and important reading.
     * An unreadable /proc/interrupts must not claim it - nor a truncated one, which would
     * report 0 for every policy whose line fell past the cut. src_str_all() refuses a read
     * that filled the buffer, so the whole object goes absent instead. */
    rc = src_str_all(&interrupts, blob, sizeof blob);
    if (rc < 0) {
        /* Only for a genuine truncation. A missing attribute is routine and was
         * silent before; saying both with one sentence tells the operator neither. */
        if (rc == -2 && !warned_trunc) {
            warned_trunc = 1;
            fprintf(stderr, "qcstats: %s exceeds the %zu-byte buffer; throttle counters "
                    "reported absent\n", PROC_INTERRUPTS, sizeof blob);
        }
        return;                         /* thr_ok stays 0 */
    }
    s->thr_ok = 1;
    for (line = strtok_r(blob, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *tag = strstr(line, "dcvsh-irq-");
        char *p, *end;
        int which, slot = -1;
        uint64_t sum = 0;

        if (!tag)
            continue;
        tag += strlen("dcvsh-irq-");
        /* Must be dcvsh-irq-<number>. atoi read anything else as 0, which is cpu0's own
         * IRQ, so a name it could not parse was counted against policy0. */
        which = (int)strtol(tag, &end, 10);         /* 0, 2 or 4 */
        if (end == tag)
            continue;
        for (int i = 0; i < NPOLICY; i++)
            if (POLICY_CPU[i] == which)
                slot = i;
        if (slot < 0)
            continue;
        /* Sum the per-CPU counts that follow the IRQ number. */
        p = strchr(line, ':');
        if (p)
            for (p++; *p; ) {
                while (*p == ' ')
                    p++;
                if (*p < '0' || *p > '9')
                    break;
                sum += strtoull(p, &p, 10);
            }
        s->thr[slot] = sum;
    }
}

static void throttle_close(void) { src_close(&interrupts); }

const struct sampler sampler_throttle = {
    .name  = "throttle",
    .open  = throttle_open,
    .read  = throttle_sample,
    .close = throttle_close,
};
