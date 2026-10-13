// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// qcstats - shared data model for the Arduino Ventuno Q sampler
// (Qualcomm QCS8275 / Dragonwing IQ-8275, board "monza").

#ifndef QCSTATS_H
#define QCSTATS_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ *
 * Verified topology - THREE cpufreq policies.
 *   policy0: CPU 0-1  Cortex-A78C   902.4 - 2112.0 MHz  cap 915
 *   policy2: CPU 2-3  Cortex-A78C   940.8 - 2361.6 MHz  cap 1024
 *   policy4: CPU 4-7  Cortex-A55    844.8 - 1958.4 MHz  cap 446
 * Anything keyed on cpu0 alone silently misses two thirds of the machine.
 * ------------------------------------------------------------------ */
#define NCPU            8
#define NPOLICY         3
extern const int   POLICY_CPU[NPOLICY];
extern const char *POLICY_LABEL[NPOLICY];

/* JSON output schema version. This is a CONTRACT with a consumer: bump it only when
 * a change would break an existing consumer - a removed or renamed field, or a changed
 * type or unit. Adding a new field is NOT breaking, because absent-vs-null means a
 * consumer already has to tolerate fields it does not know. See docs/json-schema-v1.md. */
#define QCSTATS_JSON_SCHEMA 1

#define ZONES_MAX       64      /* 48 exist; the scan bound is deliberately higher */

/* One reading, collected before any emitter runs. Two reasons it is a struct and
 * not interleaved printfs: every emitter must describe the *same* sample, and each
 * source gets read exactly once per tick. */
struct sample {
    double   t;                         /* CLOCK_REALTIME, seconds */

    uint64_t ram_used_mb, ram_total_mb;
    int      ram_ok;

    struct {
        uint64_t mhz;
        int      mhz_ok;
        int      first, last;
        int      pct[NCPU];             /* indexed by absolute cpu id */
        int      online[NCPU];
    } pol[NPOLICY];
    /* Load needs two /proc/stat readings. Without them, 0% is indistinguishable
     * from a genuinely idle machine, so it must not be reported as a value. */
    int      load_ok;

    uint64_t gpu_mhz;
    int      gpu_ok;
    /* True busy %, from the devfreq_monitor tracepoint. Needs --gpu-busy and root;
     * absent otherwise, since no sysfs export of GPU utilisation exists. */
    int      gpu_busy_pct;
    int      gpu_busy_ok;

    /* One slot per *resolved* zone, always filled: a read that fails sets ok=0
     * rather than compacting the array, which would shift CSV columns mid-run and
     * silently drop a JSON key. */
    struct { char label[64]; double c; int ok; } zone[ZONES_MAX];
    int      nzone;

    uint64_t pwr_mw, pwr_mv, pwr_ma;
    int      pwr_ok;                    /* the sensor itself was found */
    /* Per attribute: the three are separate sysfs files and fail independently.
     * Reporting 0 mW beside a live voltage and current is worse than n/a. */
    int      pwr_mw_ok, pwr_mv_ok, pwr_ma_ok;

    uint64_t fan_duty_pct, fan_rpm;
    int      fan_ok, fan_rpm_valid;
    /* 0 rpm legitimately means the fan is OFF, so an unreadable tachometer must
     * not present itself as one. Same for duty. */
    int      fan_duty_ok, fan_rpm_ok;

    uint64_t thr[NPOLICY];
    /* 0 events is a meaningful reading - it means nothing throttled. An
     * unreadable /proc/interrupts must not claim it. */
    int      thr_ok;

    /* NPU (Hexagon NSP). No sysfs surface exists at all, so these come from the
     * DSP-side sysmon service over FastRPC - see src/npu.c. Opt-in via --npu.
     * All four arrive in one struct from one call, so one validity flag covers them
     * (unlike power, whose three attributes are separate files). */
    double   npu_q6_pct, npu_hvx_pct, npu_hmx_pct;
    uint64_t npu_q6_khz;
    int      npu_ok;
};

#endif /* QCSTATS_H */
