// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// NPU (Hexagon NSP) utilisation and clock.
//
// The NSP has no usable sysfs surface - no clock in clk_summary, no devfreq, and no
// frequency OPP table - so this is the only way to read it. (debugfs opp/nsp0 and
// opp/nsp1 do exist with ten entries each, but they carry rate_hz = 0 and genpd level
// corners only, so they are not a clock table.) It goes over FastRPC to the DSP-side
// "sysmon" profiling service, which answers on QCS8275: verified on the board, with
// q6_util tracking a real Hexagon workload from 0.2 % to 64-73 % and q6_clock stepping
// 768 MHz -> 1497.6 MHz.
//
// WHY THERE IS NO THIRD-PARTY CODE OR BUILD DEPENDENCY HERE
//
// The FastRPC types, constants and the five sysmonquery_* wire formats are declared
// below from the published ABI, and libcdsprpc.so.1 is resolved with dlopen at
// runtime. So qcstats still links only libc, needs no headers from qualcomm/fastrpc,
// and a board without the library simply reports n/a - the same graceful degradation
// as a missing sysfs file. This marshalling was measured against the vendor's
// generated stub over 400 interleaved calls: identical invariants, and a
// median latency delta of -0.4 us (-0.03 %), because the cost is the ~1.12 ms FastRPC
// round-trip and not the handful of struct writes around it.
//
// TWO THINGS THAT BITE
//
//  1. A query costs ~1.12 ms AND LOADS THE DSP. Polling back-to-back put q6_util at
//     ~50 % on an otherwise idle board - entirely self-inflicted. So this sampler
//     rate-limits itself to NPU_MIN_PERIOD_MS regardless of --interval, and holds the
//     last reading in between - bounded by that period, since a query is attempted as
//     soon as it elapses.
//  2. The first window after init is partial - one run reported q6_clock = 1 - so the
//     first reading is taken and discarded during open().

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <dlfcn.h>

/* ---- the slice of the FastRPC ABI we use, declared from the published spec ---- */

typedef uint64_t qc_handle64;

struct qc_buf { void *pv; size_t nLen; };

/* Only the buf member is ever used; the union's other members exist so the size and
 * alignment match what the library expects. */
union qc_arg {
    struct qc_buf buf;
    uint32_t      h;
    qc_handle64   h64;
    struct { int32_t fd; uint32_t offset; } dma;
};

/* domain comes FIRST in this struct - getting the order wrong silently sets the
 * wrong field. */
struct qc_process_type { int domain; int process_type; };

#define QC_SCALARS(nAttr, nMethod, nIn, nOut, noIn, noOut) \
    ((((uint32_t)(nAttr)   & 0x7u)  << 29) | \
     (((uint32_t)(nMethod) & 0x1fu) << 24) | \
     (((uint32_t)(nIn)     & 0xffu) << 16) | \
     (((uint32_t)(nOut)    & 0xffu) <<  8) | \
     (((uint32_t)(noIn)    & 0x0fu) <<  4) | \
      ((uint32_t)(noOut)   & 0x0fu))

#define QC_PROCESS_TYPE_SIGNED      0   /* enum fastrpc_process_type */
#define QC_CONTROL_UNSIGNED_MODULE  2   /* enum session_control_req_id */
#define QC_RPCMEM_DEFAULT_HEAP      (-1)
#define QC_RPCMEM_DEFAULT_FLAGS     1u
#define QC_RPCMEM_HEAP_NONCOHERENT  0x08000000u

#define CDSP_DOMAIN_ID  3
#define NPU_ID          0u
#define MID_INIT        2
#define MID_PROFDATA    3
#define MID_DEINIT      4

#define NPU_URI \
    "file:///libsysmonquery_skel.so?sysmonquery_skel_handle_invoke&_modver=1.0&_dom=cdsp"

#define LIBCDSPRPC       "libcdsprpc.so.1"
#define NPU_MIN_PERIOD_MS 1000      /* never query the DSP faster than this */

/* What the DSP writes back. 14 floats + 1 uint; only four fields carry meaning. */
struct npu_prof {
    float        q6_utilization;
    unsigned int q6_clock;          /* kHz */
    float        reserved0;
    float        hvx_utilization;
    float        hmx_utilization;
    float        reserved[9];
};

static struct {
    int   enabled;                  /* --npu given */
    void *lib;
    qc_handle64 h;
    int   opened;
    struct npu_prof *prof;          /* rpcmem, shared with the DSP */

    int  (*handle64_open)(const char *, qc_handle64 *);
    int  (*handle64_invoke)(qc_handle64, uint32_t, union qc_arg *);
    int  (*handle64_close)(qc_handle64);
    int  (*session_control)(uint32_t, void *, uint32_t);
    void *(*rpcmem_alloc)(int, uint32_t, int);
    void  (*rpcmem_free)(void *);

    /* last good reading, and when we took it */
    double last_ms;
    int    have;
    struct { double q6_pct, hvx_pct, hmx_pct; uint64_t khz; } val;
} st;

void npu_enable(void)
{
    st.enabled = 1;
}

static double mono_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---- the three wire formats ------------------------------------------------- */

/* init and deinit: one input buffer holding a single scalar. */
static int npu_scalar_call(uint32_t mid, unsigned int npu_flag)
{
    union qc_arg pra[1];
    uint32_t prim_in[1];

    prim_in[0] = npu_flag;
    pra[0].buf.pv   = prim_in;
    pra[0].buf.nLen = sizeof prim_in;
    return st.handle64_invoke(st.h, QC_SCALARS(0, mid, 1, 0, 0, 0), pra);
}

/* get_profdata: one input buffer (length, flag), one output scalar (no_metrics),
 * one output buffer the DSP fills. */
static int npu_query(struct npu_prof *out, int *no_metrics)
{
    union qc_arg pra[3];
    uint32_t prim_in[2];
    uint32_t prim_rout[1] = {0};
    int rc;

    prim_in[0] = (uint32_t)sizeof *out;
    prim_in[1] = NPU_ID;
    pra[0].buf.pv   = prim_in;
    pra[0].buf.nLen = sizeof prim_in;
    pra[1].buf.pv   = prim_rout;
    pra[1].buf.nLen = sizeof prim_rout;
    pra[2].buf.pv   = out;
    pra[2].buf.nLen = sizeof *out;

    rc = st.handle64_invoke(st.h, QC_SCALARS(0, MID_PROFDATA, 1, 2, 0, 0), pra);
    if (rc == 0 && no_metrics)
        *no_metrics = (int)prim_rout[0];
    return rc;
}

static void npu_teardown(void)
{
    if (st.prof && st.rpcmem_free) {
        st.rpcmem_free(st.prof);
        st.prof = NULL;
    }
    if (st.opened) {
        npu_scalar_call(MID_DEINIT, NPU_ID);
        st.handle64_close(st.h);
        st.opened = 0;
    }
    if (st.lib) {
        dlclose(st.lib);
        st.lib = NULL;
    }
}

static int npu_open(void)
{
    struct qc_process_type pt = {0};
    int nm = 0;

    if (!st.enabled)
        return -1;                  /* not asked for: absent, not broken */

    st.lib = dlopen(LIBCDSPRPC, RTLD_NOW | RTLD_LOCAL);
    if (!st.lib) {
        fprintf(stderr, "qcstats: --npu needs %s: %s\n", LIBCDSPRPC, dlerror());
        return -1;
    }
    st.handle64_open   = dlsym(st.lib, "remote_handle64_open");
    st.handle64_invoke = dlsym(st.lib, "remote_handle64_invoke");
    st.handle64_close  = dlsym(st.lib, "remote_handle64_close");
    st.session_control = dlsym(st.lib, "remote_session_control");
    st.rpcmem_alloc    = dlsym(st.lib, "rpcmem_alloc");
    st.rpcmem_free     = dlsym(st.lib, "rpcmem_free");
    if (!st.handle64_open || !st.handle64_invoke || !st.handle64_close
        || !st.session_control || !st.rpcmem_alloc || !st.rpcmem_free) {
        fprintf(stderr, "qcstats: --npu: %s lacks the FastRPC entry points\n", LIBCDSPRPC);
        npu_teardown();
        return -1;
    }

    /* Must precede the open, or a production-signed DSP image refuses the session. */
    pt.domain       = CDSP_DOMAIN_ID;
    pt.process_type = QC_PROCESS_TYPE_SIGNED;
    st.session_control(QC_CONTROL_UNSIGNED_MODULE, &pt, (uint32_t)sizeof pt);

    if (st.handle64_open(NPU_URI, &st.h) != 0) {
        fprintf(stderr, "qcstats: --npu: the DSP refused the sysmon session "
                        "(skel missing, or no fastrpc access)\n");
        npu_teardown();
        return -1;
    }
    st.opened = 1;

    if (npu_scalar_call(MID_INIT, NPU_ID) != 0) {
        fprintf(stderr, "qcstats: --npu: sysmon init failed\n");
        npu_teardown();
        return -1;
    }

    /* The DSP writes across the FastRPC boundary, so this must be shareable
     * non-coherent memory; plain malloc is not reachable from the DSP. */
    st.prof = st.rpcmem_alloc(QC_RPCMEM_DEFAULT_HEAP,
                              QC_RPCMEM_DEFAULT_FLAGS | QC_RPCMEM_HEAP_NONCOHERENT,
                              (int)sizeof *st.prof);
    if (!st.prof) {
        fprintf(stderr, "qcstats: --npu: rpcmem_alloc failed\n");
        npu_teardown();
        return -1;
    }

    /* Burn the first window: it is partial, and has been seen to report
     * q6_clock = 1. */
    memset(st.prof, 0, sizeof *st.prof);
    npu_query(st.prof, &nm);
    st.last_ms = mono_ms();
    st.have = 0;
    return 0;
}

static void npu_sample(struct sample *s)
{
    double now = mono_ms();

    if (!st.prof)
        return;                     /* npu_ok stays 0 */

    /* Rate-limit: querying every tick would load the DSP and inflate the very
     * number we are reporting. */
    if (now - st.last_ms >= (double)NPU_MIN_PERIOD_MS) {
        int nm = 0;

        memset(st.prof, 0, sizeof *st.prof);
        st.last_ms = now;
        if (npu_query(st.prof, &nm) == 0 && nm > 0) {
            st.val.q6_pct  = (double)st.prof->q6_utilization;
            st.val.hvx_pct = (double)st.prof->hvx_utilization;
            st.val.hmx_pct = (double)st.prof->hmx_utilization;
            st.val.khz     = st.prof->q6_clock;
            st.have = 1;
        } else {
            st.have = 0;            /* a failed query is absent, not zero */
        }
    }

    /* Between queries the held reading stands: a windowed average, at most one period
     * old because the next attempt fires as soon as the period elapses. A failed
     * attempt clears st.have above, so nothing is held past the failure that found it. */
    if (st.have) {
        s->npu_q6_pct  = st.val.q6_pct;
        s->npu_hvx_pct = st.val.hvx_pct;
        s->npu_hmx_pct = st.val.hmx_pct;
        s->npu_q6_khz  = st.val.khz;
        s->npu_ok      = 1;
    }
}

static void npu_close(void)
{
    npu_teardown();
    st.have = 0;
}

const struct sampler sampler_npu = {
    .name  = "npu",
    .open  = npu_open,
    .read  = npu_sample,
    .close = npu_close,
};
