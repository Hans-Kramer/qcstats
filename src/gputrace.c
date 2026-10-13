// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// GPU busy % - the ONE module in qcstats that writes to the system.
//
// Everything else here is a pure reader. This file is the deliberate, opt-in
// exception (--gpu-busy), because there is no other way to obtain the metric:
//
//   - no /sys/class/kgsl (this is upstream drm/msm, not KGSL, so no "gpubusy")
//   - no *busy* or *util* attribute under the GPU platform device or its devfreq
//   - debugfs/dri/0/gpu is a ringbuffer and register dump, no busy counter
//
// The devfreq CORE computes it - simple_ondemand cannot function without it - and
// exports it through the devfreq_monitor tracepoint:
//
//   field:unsigned long busy_time;   offset:16
//   field:unsigned long total_time;  offset:24
//   print fmt: "... load=%-2lu", (100 * busy_time) / total_time
//
// fired every polling_ms (50 ms on this board). That is a true busy metric, not a
// residency proxy.
//
// Two costs, both real: tracefs is 0700 so this needs root, and turning the
// tracepoint on is a write. Without --gpu-busy nothing here runs and qcstats keeps
// both of its original properties.
//
// We work in a PRIVATE trace instance rather than the global buffer. Toggling the
// global event would disturb whatever tracing the operator had set up, and reading
// the global trace_pipe would consume events other readers want. An instance is
// isolated, and removing the directory removes every trace of us.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define GPU_DEVFREQ_NAME "3d00000.gpu"
#define TP_PATH          "events/devfreq/devfreq_monitor/enable"

static struct {
    int  enabled;                   /* --gpu-busy given */
    int  fd;                        /* trace_pipe, O_NONBLOCK */
    char inst[PATH_MAXLEN];         /* our private instance dir, "" if none */
    struct src rt_status;           /* power/runtime_status, unprivileged read */
} st = { .fd = -1 };

void gpu_busy_enable(void)
{
    st.enabled = 1;
}

/* Compose a/b, refusing truncation: a truncated tracefs path would name a
 * different file, and here that file gets WRITTEN. Same guard as src_open_abs(). */
static int joinpath(char *out, size_t len, const char *a, const char *b)
{
    int n = snprintf(out, len, "%s/%s", a, b);

    return (n > 0 && (size_t)n < len) ? 0 : -1;
}

/* Single-shot write of a short string. The only write path in the program. */
static int write_str(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    ssize_t n;

    if (fd < 0)
        return -1;
    n = write(fd, val, strlen(val));
    close(fd);
    return (n == (ssize_t)strlen(val)) ? 0 : -1;
}

/* tracefs is mounted at both of these on this board; prefer the canonical one. */
static const char *tracefs_root(void)
{
    static const char *cands[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };

    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++)
        if (access(cands[i], R_OK | X_OK) == 0)
            return cands[i];
    return NULL;
}

static int gpu_busy_open(void)
{
    char path[PATH_MAXLEN];
    const char *root;

    if (!st.enabled)
        return -1;                  /* not asked for: absent, not broken */

    root = tracefs_root();
    if (!root) {
        fprintf(stderr, "qcstats: --gpu-busy needs tracefs, which is root-only "
                        "(0700). Re-run as root, or drop the flag.\n");
        return -1;
    }

    /* Private instance: isolates us from any tracing already in use. */
    {
        char leaf[64];
        snprintf(leaf, sizeof leaf, "instances/qcstats-%ld", (long)getpid());
        if (joinpath(st.inst, sizeof st.inst, root, leaf) != 0) {
            fprintf(stderr, "qcstats: --gpu-busy tracefs path too long\n");
            st.inst[0] = '\0';
            return -1;
        }
    }
    if (mkdir(st.inst, 0755) != 0) {
        fprintf(stderr, "qcstats: --gpu-busy cannot create %s: %s\n",
                st.inst, strerror(errno));
        st.inst[0] = '\0';
        return -1;
    }

    if (joinpath(path, sizeof path, st.inst, TP_PATH) != 0
        || write_str(path, "1") != 0) {
        fprintf(stderr, "qcstats: --gpu-busy cannot enable devfreq_monitor: %s\n",
                strerror(errno));
        rmdir(st.inst);
        st.inst[0] = '\0';
        return -1;
    }

    /* Non-blocking: a sample must never stall waiting for a trace line. */
    st.fd = (joinpath(path, sizeof path, st.inst, "trace_pipe") == 0)
          ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    if (st.fd < 0) {
        fprintf(stderr, "qcstats: --gpu-busy cannot read trace_pipe: %s\n",
                strerror(errno));
        gpu_busy_close_now();
        return -1;
    }
    /* Read-only, and the key to interpreting a silent tracepoint: devfreq polling
     * suspends with the GPU, so an idle GPU emits no events at all. Measured on
     * this board, the GPU is runtime-suspended ~97% of uptime. */
    src_init(&st.rt_status);
    src_open(&st.rt_status, "/sys/devices/platform/soc@0/3d00000.gpu/power/runtime_status");

    fprintf(stderr, "qcstats: --gpu-busy enabled devfreq_monitor in %s "
                    "(restored on exit)\n", st.inst);
    return 0;
}

/* Restore: disable the event and remove the instance. Idempotent, and safe to call
 * from the signal path in main(). */
void gpu_busy_close_now(void)
{
    char path[PATH_MAXLEN];

    if (st.fd >= 0) {
        close(st.fd);
        st.fd = -1;
    }
    src_close(&st.rt_status);
    if (st.inst[0]) {
        if (joinpath(path, sizeof path, st.inst, TP_PATH) == 0)
            write_str(path, "0");
        rmdir(st.inst);             /* removes every trace of us */
        st.inst[0] = '\0';
    }
}

static void gpu_busy_sample(struct sample *s)
{
    static char buf[65536];
    char status[32];
    ssize_t n;
    char *p, *line, *save = NULL;
    int fresh = 0, load = 0;

    if (st.fd < 0)
        return;                     /* gpu_busy_ok stays 0 */

    /* Drain what accumulated since the last tick, keeping the LAST matching line -
     * the most recent polling window, which is what we want to report. */
    while ((n = read(st.fd, buf, sizeof buf - 1)) > 0) {
        buf[n] = '\0';
        for (line = strtok_r(buf, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (!strstr(line, GPU_DEVFREQ_NAME))
                continue;
            p = strstr(line, "load=");
            if (!p)
                continue;
            load  = atoi(p + 5);
            fresh = 1;
        }
        if (n < (ssize_t)(sizeof buf - 1))
            break;                  /* drained */
    }

    if (fresh) {
        s->gpu_busy_pct = load;
        s->gpu_busy_ok  = 1;
        return;
    }

    /* No event this tick. A runtime-SUSPENDED GPU is powered down and cannot be
     * executing anything, so 0 is a fact here, not a guess - and it is the usual
     * case, since devfreq stops polling when the GPU suspends.
     *
     * If it is active and we simply have not seen a window yet, report absent. A
     * stale value must NOT be presented as current: events can be seconds apart. */
    if (src_str(&st.rt_status, status, sizeof status) == 0
        && strncmp(status, "suspended", 9) == 0) {
        s->gpu_busy_pct = 0;
        s->gpu_busy_ok  = 1;
    }
}

static void gpu_busy_close(void)
{
    gpu_busy_close_now();
}

const struct sampler sampler_gpu_busy = {
    .name  = "gpu_busy",
    .open  = gpu_busy_open,
    .read  = gpu_busy_sample,
    .close = gpu_busy_close,
};
