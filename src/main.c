// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// qcstats - system telemetry sampler for the Arduino Ventuno Q
// (Qualcomm QCS8275 / Dragonwing IQ-8275, board "monza").
//
// Contains no third-party source. Every path was verified on hardware; rather than
// naming a kernel in a comment that nothing keeps true, each run reports the kernel and
// image it actually saw - see envid.c.
// Read-only by design - clock/fan control belongs to qcctl.
// Unprivileged: uses scaling_cur_freq (444), not cpuinfo_cur_freq (400).
//
// This file is argument handling and the sample loop. Every subsystem lives behind
// the sampler interface in sampler.h, so nothing here knows how a sensor is read.

#define _GNU_SOURCE
#include "emit.h"
#include "envid.h"
#include "io.h"
#include "qcstats.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define MIN_INTERVAL_MS 50

/* --gpu-busy enables a tracepoint, so an interrupted run MUST still restore it.
 * Without this, Ctrl-C would leave the board altered - the one thing the project's
 * board rules forbid. */
static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

static void install_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
}

/* Output field order comes from the emitters, not from this list, so the order
 * here only affects which subsystem opens first. */
static const struct sampler *const SAMPLERS[] = {
    &sampler_mem,
    &sampler_cpu,
    &sampler_gpu,
    &sampler_thermal,
    &sampler_power,
    &sampler_fan,
    &sampler_throttle,
    &sampler_npu,
    &sampler_gpu_busy,      /* opt-in; writes to tracefs, see src/gputrace.c */
};
#define NSAMPLERS (sizeof SAMPLERS / sizeof SAMPLERS[0])

enum out_mode { OUT_HUMAN, OUT_JSON, OUT_CSV };

/* strtoul() silently wraps a leading '-', so "-2" would pass as a huge value with
 * errno clear. Require a plain digit string. One validator for every numeric flag,
 * so --count and --interval cannot drift apart. */
static int parse_uint(const char *str, unsigned long *out)
{
    char *end;

    if (!str || str[0] < '0' || str[0] > '9')
        return -1;
    errno = 0;
    *out = strtoul(str, &end, 10);
    return (errno || *end) ? -1 : 0;
}

static const char *zone_argv[ZONES_MAX];

/* Split "a,b,c" in place. Rejects empty elements (",a", "a,,b", "a,") rather than
 * skipping them the way strtok would - a typo should be reported, not ignored. */
static int zones_from_list(char *list)
{
    size_t n = 0;
    char *p = list;

    for (;;) {
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';
        if (!*p)
            return -1;                  /* empty element */
        if (n >= ZONES_MAX)
            return -2;                  /* more names than zones can exist */
        zone_argv[n++] = p;
        if (!comma)
            break;
        p = comma + 1;
    }
    cfg.zones  = zone_argv;
    cfg.nzones = n;
    return 0;
}

/* out is stdout when the user asked for help and stderr when this is a usage error, for
 * the same reason version() uses stdout: `qcstats --help | less` has to show something. */
static void usage(const char *argv0, FILE *out)
{
    fprintf(out,
        "usage: %s [--plain|--json|--csv] [--count N] [--interval MS]\n"
        "       %*s [--zones LIST | --all-zones] [--logfile P] [interval_ms]\n"
        "  --plain       compact single-line form, for reading (default)\n"
        "  --json        one JSON object per sample, one per line (NDJSON).\n"
        "                Absent sources are null, never the string \"n/a\".\n"
        "  --csv         CSV with a header row; empty field = absent, as JSON null\n"
        "  --count N     emit N samples then exit; default 0 = run until killed\n"
        "  --interval MS sampling interval; same as the positional form\n"
        "  --zones LIST  comma-separated zone list, e.g. cpuss-0-0,gpuss-0.\n"
        "                The short printed name or the full *-thermal type works.\n"
        "  --all-zones   every thermal zone, ordered by index (48 on this board)\n"
        "  --npu         report Hexagon NSP utilisation and clock over FastRPC.\n"
        "                Needs libcdsprpc.so.1 and fastrpc access; polls at 1 Hz\n"
        "                because a query costs ~1.12 ms and loads the DSP itself.\n"
        "  --gpu-busy    report true GPU busy %%. NEEDS ROOT, and is the one thing\n"
        "                qcstats writes: it enables the devfreq_monitor tracepoint\n"
        "                in a private trace instance, restored on exit.\n"
        "  --logfile P   write samples to P instead of stdout. TRUNCATES P; stderr\n"
        "                notices still go to stderr. (A CSV header must not land\n"
        "                mid-file, which is why this truncates rather than appends.)\n"
        "  -h, --help    print this and exit\n"
        "  -V, --version print the version and the JSON schema version, then exit\n"
        "  interval_ms   default 1000, floor %d\n"
        "\n"
        "env:\n"
        "  %s=<dir>  read sysfs/procfs under <dir> instead of / (a captured tree)\n",
        argv0, (int)strlen(argv0), "", MIN_INTERVAL_MS, ENV_SYSROOT);
}

/* QCSTATS_VERSION comes from the Makefile so a release needs no source edit. "unknown" is
 * the honest answer for a hand-compiled binary, and deliberately not a plausible-looking
 * number - the same rule this tool applies to a reading it could not take. */
#ifndef QCSTATS_VERSION
#define QCSTATS_VERSION "unknown"
#endif

/* stdout, unlike usage() above, because -V output is meant to be read by a script.
 *
 * TWO values, because they answer different questions and move independently: the first
 * is this binary's own version, which changes whenever the code does; the second is the
 * CONSUMER CONTRACT, which is bumped only for a breaking change and is the one a reader
 * of a saved stream actually needs. Reporting only the first would invite treating a
 * version bump as a schema bump.
 *
 * Name-then-value on each line, so `qcstats -V | head -1 | cut -d' ' -f2` is stable. */
static void version(void)
{
    printf("qcstats %s\n", QCSTATS_VERSION);
    printf("json-schema %d\n", QCSTATS_JSON_SCHEMA);
}

int main(int argc, char *argv[])
{
    unsigned long interval_ms = 1000;
    unsigned long count = 0;            /* 0 = until killed */
    enum out_mode mode = OUT_HUMAN;
    static struct envid envi;   /* borrowed by emit.c for the life of the run */
    char *zone_list = NULL;
    const char *logpath = NULL;
    FILE *logf = NULL;
    struct sample s;
    const char *env = getenv(ENV_SYSROOT);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0)
            mode = OUT_JSON;
        else if (strcmp(argv[i], "--plain") == 0)
            mode = OUT_HUMAN;
        else if (strcmp(argv[i], "--csv") == 0)
            mode = OUT_CSV;
        else if (strcmp(argv[i], "--count") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "qcstats: --count needs a value\n");
                return 2;
            }
            if (parse_uint(argv[++i], &count) < 0 || !count) {
                fprintf(stderr, "qcstats: --count wants a positive integer, got '%s'\n",
                        argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--interval") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "qcstats: --interval needs a value\n");
                return 2;
            }
            if (parse_uint(argv[++i], &interval_ms) < 0 || !interval_ms) {
                fprintf(stderr, "qcstats: --interval wants a positive integer (ms), "
                                "got '%s'\n", argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--zones") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "qcstats: --zones needs a comma-separated list\n");
                return 2;
            }
            zone_list = argv[++i];
            cfg.zones_explicit = 1;
        }
        else if (strcmp(argv[i], "--all-zones") == 0)
            cfg.all_zones = 1;
        else if (strcmp(argv[i], "--gpu-busy") == 0)
            gpu_busy_enable();
        else if (strcmp(argv[i], "--npu") == 0)
            npu_enable();
        else if (strcmp(argv[i], "--logfile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "qcstats: --logfile needs a path\n");
                return 2;
            }
            logpath = argv[++i];
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0], stdout);
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            version();
            return 0;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            /* positional form, kept for convenience. Same validator as --interval. */
            if (parse_uint(argv[i], &interval_ms) < 0 || !interval_ms) {
                fprintf(stderr, "qcstats: bad interval '%s'\n", argv[i]);
                return 2;
            }
        } else {
            fprintf(stderr, "qcstats: unknown argument '%s'\n", argv[i]);
            usage(argv[0], stderr);
            return 2;
        }
    }

    if (cfg.all_zones && zone_list) {
        fprintf(stderr, "qcstats: --zones and --all-zones are mutually exclusive\n");
        return 2;
    }

    if (env && *env) {
        io_set_sysroot(env);
        fprintf(stderr, "qcstats: reading under %s=%s\n", ENV_SYSROOT, sysroot);
    }

    /* Zone selection needs the sysroot live: names resolve against that tree. */
    if (zone_list) {
        int rc = zones_from_list(zone_list);
        if (rc == -1) {
            fprintf(stderr, "qcstats: --zones has an empty entry\n");
            return 2;
        }
        if (rc == -2) {
            fprintf(stderr, "qcstats: --zones takes at most %d names\n", ZONES_MAX);
            return 2;
        }
    }

    if (interval_ms < MIN_INTERVAL_MS)
        interval_ms = MIN_INTERVAL_MS;

    /* Opened before the samplers so a bad path fails immediately rather than after
     * the first interval has already elapsed. */
    if (logpath) {
        logf = fopen(logpath, "w");
        if (!logf) {
            fprintf(stderr, "qcstats: cannot write '%s': %s\n",
                    logpath, strerror(errno));
            return 2;
        }
        emit_to(logf);
    }

    /* Open everything once. A sampler returning -1 is absent, not broken: its
     * fields stay unset and the emitters report n/a. Nothing here aborts. */
    install_handlers();

    /* Read the environment once, before anything is emitted. It is two small files and a
     * property of the RUN, not of a sample - reading it per sample would be pure waste.
     * Named envi, not env: main() already has a local `env` holding the sysroot. */
    envid_read(&envi);
    emit_set_env(&envi);

    for (size_t i = 0; i < NSAMPLERS; i++)
        SAMPLERS[i]->open();

    /* Absolute monotonic deadlines, not usleep(interval) after the work. Sleeping
     * for the interval ADDS the sampling cost to it: measured on the board, 60
     * samples at 50ms took 3.67s instead of 3.00s - a 22% longer cadence than
     * asked for. At 1000ms the error is ~2%, but it compounds over a long run. */
    struct timespec next;
    if (clock_gettime(CLOCK_MONOTONIC, &next) != 0)
        next.tv_sec = next.tv_nsec = 0;

    /* The first sample sleeps before emitting: cpu_pct() needs two /proc/stat
     * readings, so there is nothing truthful to print before one interval. */
    for (unsigned long n = 0; (!count || n < count) && !stop_flag; n++) {
        struct timespec ts, now;

        next.tv_nsec += (long)(interval_ms % 1000) * 1000000L;
        next.tv_sec  += (time_t)(interval_ms / 1000);
        if (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        /* If a sample overran its slot, do not try to catch up in a burst - that
         * would sample faster than requested for a while. Reset the cadence. */
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0
            && (now.tv_sec > next.tv_sec
                || (now.tv_sec == next.tv_sec && now.tv_nsec > next.tv_nsec)))
            next = now;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
            if (stop_flag)
                goto done;              /* but a stop signal must not be ignored */

        memset(&s, 0, sizeof s);
        s.t = (clock_gettime(CLOCK_REALTIME, &ts) == 0)
            ? (double)ts.tv_sec + (double)ts.tv_nsec / 1e9 : 0.0;
        for (size_t i = 0; i < NSAMPLERS; i++)
            SAMPLERS[i]->read(&s);

        /* The CSV column set comes from the resolved sources, so the header can
         * only be written once a sample exists to describe. */
        if (mode == OUT_CSV && n == 0)
            emit_csv_header(&s);
        /* Provenance, once per run. CSV carries it as columns instead - see emit.c. */
        if (n == 0 && mode == OUT_JSON)
            emit_meta_json();
        else if (n == 0 && mode == OUT_HUMAN)
            emit_meta_human();
        if (mode == OUT_JSON)
            emit_json(&s);
        else if (mode == OUT_CSV)
            emit_csv(&s);
        else
            emit_human(&s);
        /* Flush every sample: a log is worthless if a kill loses the tail. */
        fflush(logf ? logf : stdout);
    }

done:
    for (size_t i = 0; i < NSAMPLERS; i++)
        SAMPLERS[i]->close();
    if (logf)
        fclose(logf);
    return 0;
}
