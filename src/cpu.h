// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// cpu_pct() is the only arithmetic in the tool, so it is declared here rather than
// kept static: it should be reachable by linking against it, not by #including the
// translation unit.

#ifndef QCSTATS_CPU_H
#define QCSTATS_CPU_H

#include "io.h"
#include "qcstats.h"

struct cpu_times { uint64_t busy, total; };

/* Parse /proc/stat into per-cpu busy/total. Zeroes every entry first, so a short
 * or unreadable file yields zeros rather than stale values. */
void cpu_read_times(const struct src *stat, struct cpu_times t[NCPU]);

/* Busy percentage between two readings, rounded half-up. Returns 0 when no time
 * elapsed, rather than dividing by zero. */
int  cpu_pct(const struct cpu_times *a, const struct cpu_times *b);

#endif /* QCSTATS_CPU_H */
