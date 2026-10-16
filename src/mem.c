// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// RAM, from /proc/meminfo. MemAvailable rather than MemFree: free excludes
// reclaimable page cache and understates what a workload can actually get.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>
#include <string.h>

static struct src meminfo;

static int mem_open(void)
{
    src_init(&meminfo);
    src_open(&meminfo, PROC_MEMINFO);
    return src_have(&meminfo) ? 0 : -1;
}

/* Once per run; see throttle.c for why. */
static int warned_trunc;

static void mem_sample(struct sample *s)
{
    int rc;
    static char blob[8192];
    uint64_t total = 0, avail = 0;
    int have_total = 0, have_avail = 0;
    char *line, *save = NULL;

    /* The whole file is parsed, so a read that stopped at the buffer is not usable: the
     * fields wanted may be past the cut, and reporting what was found would be a partial
     * answer presented as a complete one. */
    rc = src_str_all(&meminfo, blob, sizeof blob);
    if (rc < 0) {
        /* Only for a genuine truncation. A missing attribute is routine and was
         * silent before; saying both with one sentence tells the operator neither. */
        if (rc == -2 && !warned_trunc) {
            warned_trunc = 1;
            fprintf(stderr, "qcstats: %s exceeds the %zu-byte buffer; memory "
                    "reported absent\n", PROC_MEMINFO, sizeof blob);
        }
        return;                         /* ram_ok stays 0 */
    }
    for (line = strtok_r(blob, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1)
            have_total = 1;
        if (sscanf(line, "MemAvailable: %lu kB", &avail) == 1)
            have_avail = 1;
    }
    /* BOTH are required. With MemAvailable missing, avail stayed 0 and "used"
     * came out equal to total - a confident claim that all RAM is in use. */
    if (!have_total || !have_avail || !total)
        return;
    s->ram_used_mb  = (total - avail) / 1024;
    s->ram_total_mb = total / 1024;
    s->ram_ok = 1;
}

static void mem_close(void) { src_close(&meminfo); }

const struct sampler sampler_mem = {
    .name = "mem", .open = mem_open, .read = mem_sample, .close = mem_close,
};
