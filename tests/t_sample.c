// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
// SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
//
// Every sampler, against a captured board, with no board present. The fixture in
// tests/fixtures/ is a photograph of a Ventuno Q's read surface - 37 KB of sysfs and procfs
// text, taken by tests/capture-fixture.sh and named for the image and kernel it came from.
//
// What this proves is narrow and worth being clear about: it proves the code turns THAT
// shape into THIS output. It cannot prove the shape is still what a kernel presents, which
// is why the thermal and fan behaviour of the board remains a hardware check.

#define _GNU_SOURCE
#include "../src/qcstats.h"
#include "../src/sampler.h"
#include "../src/emit.h"
#include "../src/io.h"
#include "t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FIXTURE "tests/fixtures/monza-20260710-235-6.8.0-1084"

static char buf[16384];

static const struct sampler *const ALL[] = {
    &sampler_mem, &sampler_cpu, &sampler_gpu, &sampler_thermal,
    &sampler_power, &sampler_fan, &sampler_throttle, &sampler_npu,
};

/* Sample `times` and return the last line's JSON. Twice is the interesting number: the
 * load percentage needs two readings of /proc/stat, and a fixture's second reading is
 * identical to its first. */
static const char *sample_json(int times)
{
    struct sample s;
    FILE *f = tmpfile();
    long n;

    if (!f) {
        T_FAIL("%s", "cannot open a temp file");
        buf[0] = '\0';
        return buf;
    }
    for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++)
        ALL[i]->open();
    emit_to(f);
    for (int k = 0; k < times; k++) {
        memset(&s, 0, sizeof s);
        for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++)
            ALL[i]->read(&s);
        rewind(f);
        /* Keep only the last line: the earlier samples exist to prime the load delta. */
        if (ftruncate(fileno(f), 0) != 0)
            T_FAIL("%s", "cannot truncate the sample buffer");
        emit_json(&s);
    }
    fflush(f);
    rewind(f);
    n = (long)fread(buf, 1, sizeof buf - 1, f);
    buf[n > 0 ? n : 0] = '\0';
    fclose(f);
    emit_to(stdout);
    for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++)
        ALL[i]->close();
    return buf;
}

/* Read one number out of the fixture, so expectations are DERIVED from the tree rather
 * than copied from one capture's values. The first version of this file pinned the captured
 * numbers and broke the moment the tree was re-captured minutes later - which is the wrong
 * thing to pin: what must not drift is the arithmetic, not the weather. */
static long fixture_long(const char *rel)
{
    char path[512], buf[64];
    FILE *f;
    long v = -1;

    snprintf(path, sizeof path, "%s/%s", FIXTURE, rel);
    if ((f = fopen(path, "r")) == NULL)
        return -1;
    if (fgets(buf, sizeof buf, f))
        v = strtol(buf, NULL, 10);
    fclose(f);
    return v;
}

static void t_the_transformations_hold(void)
{
    const char *j;
    char want[128];
    long mc, khz, total_kb;

    io_set_sysroot(FIXTURE);
    j = sample_json(2);
    io_set_sysroot("");

    /* Temperature: milli-C in sysfs, one decimal of C out. The conversion and the format
     * are both part of what a consumer reads. */
    mc = fixture_long("sys/class/thermal/thermal_zone37/temp");
    T_TRUE(mc > 20000 && mc < 90000, "the fixture's zone37 holds a plausible temperature");
    /* With the trailing delimiter: these are substring matches, and "36.8" alone would also
     * be found inside "368.0" - which is exactly what a broken conversion produces. Which
     * delimiter follows depends on where this zone sits in the object, so either will do. */
    {
        char alt[128];

        snprintf(want, sizeof want, "\"cpuss-0-0\":%.1f,", mc / 1000.0);
        snprintf(alt, sizeof alt, "\"cpuss-0-0\":%.1f}", mc / 1000.0);
        T_TRUE(strstr(j, want) != NULL || strstr(j, alt) != NULL,
               "the zone qcctl drives a fan from, converted and formatted");
    }

    /* Frequency: kHz in cpufreq, MHz out, truncated by integer division.
     *
     * Two things this needle has to do, both learned the hard way. The trailing comma:
     * "mhz":1017 on its own is a prefix of "mhz":1017600, so an unconverted kHz value
     * satisfies it. The leading label: without it the needle only proves the number appears
     * SOMEWHERE in the array, so swapping two policies' frequency files passes. Every
     * policy is checked, because a check on one slot says nothing about the others. */
    {
        static const struct { int pol; const char *label; } pols[] = {
            { 0, "A78C.0" }, { 2, "A78C.1" }, { 4, "A55" },
        };
        char rel[128], what[96];

        for (size_t i = 0; i < sizeof pols / sizeof pols[0]; i++) {
            snprintf(rel, sizeof rel,
                     "sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq",
                     pols[i].pol);
            khz = fixture_long(rel);
            snprintf(what, sizeof what, "the fixture holds a plausible policy%d frequency",
                     pols[i].pol);
            T_TRUE(khz > 100000, what);
            snprintf(want, sizeof want, "\"label\":\"%s\",\"mhz\":%ld,",
                     pols[i].label, khz / 1000);
            snprintf(what, sizeof what, "%s carries its OWN frequency, in MHz",
                     pols[i].label);
            T_HAS(j, want, what);
        }
    }

    /* Every other transform in the tool, each against the fixture's own numbers. The
     * suite checked these fields were PRESENT and never that they were right: dividing the
     * GPU's hertz by a thousand instead of a million, or reporting available memory as
     * used, or computing duty against 256 instead of 255, all passed. */
    {
        long hz = fixture_long("sys/class/devfreq/3d00000.gpu/cur_freq");
        long pwm = fixture_long("sys/class/hwmon/hwmon0/pwm1");
        long rpm = fixture_long("sys/class/hwmon/hwmon0/fan1_input");

        T_TRUE(hz > 1000000, "the fixture holds a plausible GPU frequency in Hz");
        snprintf(want, sizeof want, "\"gpu_mhz\":%ld,", hz / 1000000);
        T_HAS(j, want, "the GPU's hertz become megahertz, not kilohertz");

        /* 102 of 255 is 40%, and of 256 is 39: the off-by-one that a presence check
         * cannot see. */
        T_TRUE(pwm > 0 && pwm < 255, "the fixture's fan is running but not flat out");
        snprintf(want, sizeof want, "\"duty_pct\":%ld,", pwm * 100 / 255);
        T_HAS(j, want, "pwm is a percentage of 255, the hardware's full scale");

        snprintf(want, sizeof want, "\"rpm\":%ld,", rpm);
        T_HAS(j, want, "and the tachometer is reported as it reads");
    }

    /* Memory: kB in meminfo, MB out. */
    total_kb = 0;
    {
        char path[512], line[256];
        FILE *f;

        snprintf(path, sizeof path, "%s/proc/meminfo", FIXTURE);
        if ((f = fopen(path, "r")) != NULL) {
            while (fgets(line, sizeof line, f))
                if (strncmp(line, "MemTotal:", 9) == 0)
                    total_kb = strtol(line + 9, NULL, 10);
            fclose(f);
        }
    }
    T_TRUE(total_kb > 1000000, "the fixture holds a plausible MemTotal");
    snprintf(want, sizeof want, "\"total_mb\":%ld}", total_kb / 1024);
    T_HAS(j, want, "memory total in MB, not kB");

    /* USED is the one with arithmetic in it: total minus available, and reporting
     * available instead passed every check this file had. */
    {
        long avail_kb = 0;
        char path[512], line[256];
        FILE *f;

        snprintf(path, sizeof path, "%s/proc/meminfo", FIXTURE);
        if ((f = fopen(path, "r")) != NULL) {
            while (fgets(line, sizeof line, f))
                if (strncmp(line, "MemAvailable:", 13) == 0)
                    avail_kb = strtol(line + 13, NULL, 10);
            fclose(f);
        }
        T_TRUE(avail_kb > 0 && avail_kb < total_kb, "and a plausible MemAvailable");
        snprintf(want, sizeof want, "\"used_mb\":%ld,", (total_kb - avail_kb) / 1024);
        T_HAS(j, want, "used memory is total minus available, in MB");
    }

    /* The fan is an object with all three keys whenever the sensor is present - the shape
     * qcctl's parser walks - and rpm_valid is a bare boolean. */
    T_HAS(j, "\"fan\":{\"duty_pct\":", "the fan is an object");
    T_HAS(j, "\"rpm_valid\":", "with the trustworthiness flag");
    T_TRUE(strstr(j, "\"rpm_valid\":true") || strstr(j, "\"rpm_valid\":false"),
           "which is true or false, not a string or a number");
    T_HAS(j, "\"power\":{\"mw\":", "and power is an object when the sensor is present");

    /* A throttle count of 0 is a MEASUREMENT - nothing throttled - and must survive as 0
     * rather than becoming null. The opposite rule to the one below, and both matter. */
    T_HAS(j, "\"A78C.0\":0,", "a measured zero stays zero");

    /* Sources a fixture genuinely cannot carry: the NPU needs fastrpc, GPU busy needs a
     * tracepoint and root. Absent, so null - not 0. */
    T_HAS(j, "\"npu\":null", "the NPU is absent in a capture, and says so");
    T_HAS(j, "\"gpu_busy_pct\":null", "and so is the GPU busy tracepoint");
}

static void t_a_frozen_tree_reports_no_load(void)
{
    const char *j;

    /* The defect this fixture found on its first run: /proc/stat does not advance in a
     * captured tree, so the delta is zero - and a zero delta is not a measurement. It used
     * to report 0%, which is indistinguishable from a genuinely idle machine. Frequencies
     * come from cpufreq and are unaffected, so they are still reported. */
    io_set_sysroot(FIXTURE);
    j = sample_json(2);
    io_set_sysroot("");

    T_HAS(j, "\"pct\":null", "a core whose counters did not advance has no percentage");
    T_HASNT(j, "\"pct\":0", "and is not reported as 0% busy");
    {
        /* Derived and anchored for the same two reasons as above: this needle was written
         * as "mhz":1017, which the raw kHz reading also satisfies. */
        long khz = fixture_long("sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq");
        char want[64];

        snprintf(want, sizeof want, "\"label\":\"A78C.0\",\"mhz\":%ld,", khz / 1000);
        T_HAS(j, want, "while its frequency, which does not come from /proc/stat, is");
    }
}

static void t_one_reading_is_not_enough_either(void)
{
    const char *j;

    /* The same rule at the other end: the first sample has nothing to compare against. */
    io_set_sysroot(FIXTURE);
    j = sample_json(1);
    io_set_sysroot("");

    T_HAS(j, "\"pct\":null", "the first sample reports no load");
    T_HAS(j, "\"total_mb\":", "but everything that needs only one reading is there");
}

static void t_a_missing_tree_is_all_absence(void)
{
    const char *j;

    /* Not an error: every source is optional at runtime, and a sampler that finds nothing
     * reports nothing rather than failing. This is also what running on the wrong board
     * would look like. The power sampler's "ina232 hwmon not found" on stderr during this
     * case is the sampler saying so, and is expected output of the test. */
    io_set_sysroot("tests/fixtures/does-not-exist");
    j = sample_json(2);
    io_set_sysroot("");

    T_HAS(j, "\"ram\":null", "no RAM");
    T_HAS(j, "\"fan\":null", "no fan");
    T_HAS(j, "\"power\":null", "no power sensor");
    T_HAS(j, "\"gpu_mhz\":null", "no GPU");

    /* Specifically: no VALUE field becomes a zero. ":0," on its own would also match
     * "cpu":0 in the core array, which is an index and not a reading. */
    T_HASNT(j, "\"gpu_mhz\":0", "the GPU frequency is not zeroed");
    T_HASNT(j, "\"rpm\":0", "nor the fan speed");
    T_HASNT(j, "\"mhz\":0", "nor a CPU frequency");
    T_HASNT(j, "\"used_mb\":0", "nor memory in use");
}

int main(void)
{
    t_the_transformations_hold();
    t_a_frozen_tree_reports_no_load();
    t_one_reading_is_not_enough_either();
    t_a_missing_tree_is_all_absence();
    return t_summary("sample");
}
