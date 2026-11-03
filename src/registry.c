// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
// SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
//
// The one list of samplers. It is here rather than in main.c because the tests kept a
// second copy of it, and the copy drifted.

#include "sampler.h"

/* Order affects which subsystem opens first, nothing else: the output order is emit.c's. */
const struct sampler *const qcstats_samplers[] = {
    &sampler_mem,
    &sampler_uptime,
    &sampler_cpu,
    &sampler_gpu,
    &sampler_thermal,
    &sampler_power,
    &sampler_fan,
    &sampler_throttle,
    &sampler_net,
    &sampler_npu,
    &sampler_gpu_busy,      /* opt-in; writes to tracefs, see src/gputrace.c */
};

const size_t qcstats_nsamplers = sizeof qcstats_samplers / sizeof qcstats_samplers[0];
