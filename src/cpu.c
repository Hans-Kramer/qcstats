// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// CPU: per-core utilisation from /proc/stat deltas, plus per-POLICY frequency.
// Frequency is per policy, not per core - three policies, not one.

#define _GNU_SOURCE
#include "cpu.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The previous reading lives here rather than in main(): the delta is this
 * module's business, and nothing else needs to know it keeps history. */
static struct {
    struct src freq[NPOLICY];
    struct src online[NCPU];
    struct src stat;
    struct cpu_times prev[NCPU];
    int    primed;
} st;

void cpu_read_times(const struct src *stat, struct cpu_times t[NCPU])
{
    static char blob[8192];
    char *line, *save = NULL;

    memset(t, 0, sizeof(struct cpu_times) * NCPU);
    if (src_str(stat, blob, sizeof blob) < 0)
        return;
    for (line = strtok_r(blob, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned cpu;
        uint64_t v[10] = {0};
        if (sscanf(line, "cpu%u %lu %lu %lu %lu %lu %lu %lu %lu",
                   &cpu, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 5)
            continue;
        if (cpu >= NCPU)
            continue;
        uint64_t idle = v[3] + v[4];               /* idle + iowait */
        uint64_t tot  = 0;
        for (int i = 0; i < 8; i++)
            tot += v[i];
        t[cpu].total = tot;
        t[cpu].busy  = tot - idle;
    }
}

int cpu_pct(const struct cpu_times *a, const struct cpu_times *b)
{
    uint64_t dt = b->total - a->total;
    uint64_t db = b->busy  - a->busy;

    if (!dt)
        return 0;
    return (int)((db * 100 + dt / 2) / dt);
}

static int cpu_open(void)
{
    src_init(&st.stat);
    for (int p = 0; p < NPOLICY; p++)
        src_init(&st.freq[p]);
    for (int c = 0; c < NCPU; c++)
        src_init(&st.online[c]);

    src_open(&st.stat, PROC_STAT);
    for (int p = 0; p < NPOLICY; p++)
        src_open(&st.freq[p], SYS_CPU "/cpufreq/policy%d/scaling_cur_freq",
                 POLICY_CPU[p]);
    /* Absent means online. On this board cpu0 DOES have an "online" attribute (644,
     * reads 1), unlike the synthetic fixture that first suggested otherwise - but the
     * absent-means-online default is kept, because it costs nothing and a kernel that
     * hides the node for a non-hotpluggable cpu0 is entirely plausible. */
    for (int c = 0; c < NCPU; c++)
        src_open(&st.online[c], SYS_CPU "/cpu%d/online", c);

    /* Prime the delta. Without this the first sample would compare against zeros
     * and report every core at 100%. */
    if (src_have(&st.stat)) {
        cpu_read_times(&st.stat, st.prev);
        st.primed = 1;
    }
    return src_have(&st.stat) ? 0 : -1;
}

static void cpu_sample(struct sample *s)
{
    struct cpu_times cur[NCPU];
    int have = src_have(&st.stat) && src_str_ok(&st.stat);

    cpu_read_times(&st.stat, cur);
    /* Frequencies come from cpufreq and are independent of /proc/stat, so they are
     * still reported when load is not. */
    s->load_ok = have && st.primed;
    /* A zero delta is not a measurement. Between two reads of a live /proc/stat something
     * always advances, so this cannot happen on a board; against a CAPTURED tree it always
     * does, and reporting 0% there would be a percentage nobody measured - the one output
     * this sampler must never produce. Found by the first fixture run. */
    if (s->load_ok) {
        int moved = 0;

        for (int c = 0; c < NCPU; c++)
            if (cur[c].total != st.prev[c].total)
                moved = 1;
        s->load_ok = moved;
    }
    for (int p = 0; p < NPOLICY; p++) {
        int first = POLICY_CPU[p];
        int last  = (p + 1 < NPOLICY) ? POLICY_CPU[p + 1] - 1 : NCPU - 1;
        uint64_t khz = 0;

        s->pol[p].first = first;
        s->pol[p].last  = last;
        if (src_u64(&st.freq[p], &khz) == 0) {
            s->pol[p].mhz    = khz / 1000;
            s->pol[p].mhz_ok = 1;
        }
        for (int c = first; c <= last && c < NCPU; c++) {
            uint64_t online = 1;        /* absent attribute = online (cpu0) */
            src_u64(&st.online[c], &online);
            s->pol[p].online[c] = online ? 1 : 0;
            s->pol[p].pct[c] = (online && s->load_ok)
                             ? cpu_pct(&st.prev[c], &cur[c]) : 0;
        }
    }
    memcpy(st.prev, cur, sizeof st.prev);
    if (have)
        st.primed = 1;
}

static void cpu_close(void)
{
    src_close(&st.stat);
    for (int p = 0; p < NPOLICY; p++)
        src_close(&st.freq[p]);
    for (int c = 0; c < NCPU; c++)
        src_close(&st.online[c]);
}

const struct sampler sampler_cpu = {
    .name  = "cpu",
    .open  = cpu_open,
    .read  = cpu_sample,
    .close = cpu_close,
};
