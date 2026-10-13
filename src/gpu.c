// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// GPU: Adreno 623 at devfreq/3d00000.gpu, the only devfreq domain on the board.
// cur_freq is in **Hz**, not kHz - 479/599/780/877 MHz.
//
// There is no busy-percentage source on this target. devfreq exposes no
// utilisation counter, so GPU load is deliberately absent rather than faked from
// residency.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

static struct src cur_freq;

static int gpu_open(void)
{
    src_init(&cur_freq);
    src_open(&cur_freq, GPU_DEVFREQ "/cur_freq");
    return src_have(&cur_freq) ? 0 : -1;
}

static void gpu_sample(struct sample *s)
{
    uint64_t hz = 0;

    if (src_u64(&cur_freq, &hz) != 0)
        return;                         /* gpu_ok stays 0 */
    s->gpu_mhz = hz / 1000000;
    s->gpu_ok  = 1;
}

static void gpu_close(void) { src_close(&cur_freq); }

const struct sampler sampler_gpu = {
    .name = "gpu", .open = gpu_open, .read = gpu_sample, .close = gpu_close,
};
