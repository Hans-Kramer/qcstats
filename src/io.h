// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Platform access layer: sysroot-relative paths, held-open sources, and the
// resolve-by-name lookups that sysfs forces on us. Everything that touches the
// filesystem lives here; the samplers above it only read values.

#ifndef QCSTATS_IO_H
#define QCSTATS_IO_H

#include <stdint.h>
#include <stddef.h>

#define PATH_MAXLEN     768

#define SYS_CPU         "/sys/devices/system/cpu"
#define GPU_DEVFREQ     "/sys/class/devfreq/3d00000.gpu"
#define THERMAL_DIR     "/sys/class/thermal"
#define PROC_STAT       "/proc/stat"
#define PROC_MEMINFO    "/proc/meminfo"
#define PROC_INTERRUPTS "/proc/interrupts"

// hwmon indices are NOT stable across boots - resolve by the name attribute.
#define HWMON_DIR       "/sys/class/hwmon"
#define HWMON_POWER     "ina232"    /* i2c-12 0x40, 2 mOhm shunt, 20 V rail */
#define HWMON_FAN       "emc2305"   /* i2c-13 0x2f, 11 states, 0-255 PWM    */

/* QCSTATS_SYSROOT relocates every source path onto a captured fixture tree, so the
 * sampler runs unchanged on a non-Ventuno host. Empty = read the real system. */
#define ENV_SYSROOT     "QCSTATS_SYSROOT"

/* Run configuration, set from argv before any sampler opens. */
struct config {
    const char **zones;         /* selected zone names */
    size_t       nzones;
    int          zones_explicit; /* --zones given: name a bad zone on stderr */
    int          all_zones;
};
extern struct config cfg;
extern const char   *sysroot;

void io_set_sysroot(const char *dir);

/* A source held open for the life of the run. pread(fd, ..., 0) returns *fresh*
 * content on procfs and sysfs - verified on /proc/{stat,meminfo,interrupts} and
 * cpufreq/scaling_cur_freq - so there is no reason to reopen thousands of files a
 * second. fd < 0 means absent, and it stays absent: a sysfs attribute missing at
 * startup does not appear mid-run. Callers must still tolerate a read failing. */
struct src { int fd; };

void src_init(struct src *s);
void src_close(struct src *s);
void src_open(struct src *s, const char *fmt, ...);
void src_open_abs(struct src *s, const char *dir, const char *attr);
int  src_str(const struct src *s, char *buf, size_t len);
// For callers that parse the whole file: fails rather than return a truncated read.
// 0 = whole file, -1 = unreadable, -2 = filled the buffer. Both negatives are failures.
int  src_str_all(const struct src *s, char *buf, size_t len);
int  src_u64(const struct src *s, uint64_t *out);
static inline int src_have(const struct src *s) { return s->fd >= 0; }
/* Whether a read succeeds *now* - an fd can be open and still yield nothing. */
int src_str_ok(const struct src *s);

/* Build a sysroot-prefixed path. For LITERAL source paths only - hwmon_find() and
 * the zone helpers hand back already-prefixed dirs; prefixing those twice breaks. */
void spath(char *out, size_t len, const char *fmt, ...);

int  read_file_str(const char *path, char *buf, size_t len);
int  hwmon_find(const char *want, char *out, size_t len);

/* Thermal zones, resolved in one pass over the thermal_zone type attributes. */
struct zone_tab { char type[64]; int idx; };
size_t zone_table(struct zone_tab *t, size_t max);
int    zone_lookup(const struct zone_tab *t, size_t n, const char *want);

#endif /* QCSTATS_IO_H */
