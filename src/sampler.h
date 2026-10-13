// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// The sampler interface. Each subsystem is one of these, owns its own file
// descriptors, and writes only its own fields of struct sample.
//
// Contract, and the reason it is shaped this way:
//   open()  resolves and opens every source it needs, once. Returns 0 if the
//           subsystem is present, -1 if it is not. **A -1 is not an error** - it
//           means "report n/a and carry on". No sampler may abort a run.
//   read()  fills its fields. Must tolerate a source vanishing or returning junk
//           at any tick, and must leave its *_ok flags false when it does.
//   close() releases descriptors. Safe to call after a failed open().
//
// Every source is optional at runtime: CONFIG_SENSORS_INA2XX=m means the power
// sensor can legitimately be absent on a working board.

#ifndef QCSTATS_SAMPLER_H
#define QCSTATS_SAMPLER_H

#include "qcstats.h"

struct sampler {
    const char *name;
    int  (*open)(void);
    void (*read)(struct sample *s);
    void (*close)(void);
};

/* One instance of each per process, so state is file-scope inside each module
 * rather than handed around as an opaque pointer. */
extern const struct sampler sampler_mem;
extern const struct sampler sampler_cpu;
extern const struct sampler sampler_gpu;
extern const struct sampler sampler_thermal;
extern const struct sampler sampler_power;
extern const struct sampler sampler_fan;
extern const struct sampler sampler_throttle;
extern const struct sampler sampler_npu;
extern const struct sampler sampler_gpu_busy;

/* --gpu-busy is opt-in because it writes to the system and needs root; see
 * src/gputrace.c. gpu_busy_close_now() is idempotent and safe from a signal path,
 * so a Ctrl-C still restores the tracepoint. */
void gpu_busy_enable(void);
void gpu_busy_close_now(void);

/* --npu is opt-in: a sysmon query costs ~1.12 ms and loads the DSP, so it is never
 * enabled by default and src/npu.c rate-limits itself to 1 Hz. */
void npu_enable(void);

#endif /* QCSTATS_SAMPLER_H */
