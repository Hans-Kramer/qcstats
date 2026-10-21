// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
// SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
//
// The three units nothing linked against.
//
// cpu_pct() is the clearest case: cpu.h says it is declared there rather than kept static
// "so it should be reachable by linking against it, not by #including the translation unit"
// - a header documenting testability for a function no test called. Halving every percentage
// the tool reports passed the whole suite.
//
// envid.c had no test at all, and its by-key lookup is load-bearing: its own comment says a
// line added ahead of BUILD_ID in a future image must not change the answer.
//
// The throttle summation adds eight per-CPU columns of /proc/interrupts, and the captured
// board was idle, so every count in the fixture is zero - a broken sum is still zero. It
// needs a tree with numbers in it, which is the first synthetic fixture in this repo.

#define _GNU_SOURCE
#include "../src/cpu.h"
#include "../src/envid.h"
#include "../src/qcstats.h"
#include "../src/sampler.h"
#include "t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char dir[64];

static void put(const char *rel, const char *content)
{
    char path[320], cmd[640];
    FILE *f;
    char *slash;

    snprintf(path, sizeof path, "%s/%s", dir, rel);
    if ((slash = strrchr(path, '/')) != NULL) {
        *slash = '\0';
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
        if (system(cmd) != 0)
            exit(2);
        *slash = '/';
    }
    if ((f = fopen(path, "w")) == NULL) {
        perror(path);
        exit(2);
    }
    fputs(content, f);
    fclose(f);
}

/* ---- cpu_pct: the only arithmetic in the tool ------------------------------------- */

static void t_cpu_pct_is_a_percentage_of_elapsed_time(void)
{
    struct cpu_times a, b;

    /* Half the elapsed jiffies busy. */
    a.busy = 100; a.total = 200;
    b.busy = 150; b.total = 300;
    T_EQ_INT(cpu_pct(&a, &b), 50, "half of the elapsed time busy reads 50%");

    a.busy = 0; a.total = 0;
    b.busy = 100; b.total = 100;
    T_EQ_INT(cpu_pct(&a, &b), 100, "all of it reads 100%");

    a.busy = 0; a.total = 0;
    b.busy = 0; b.total = 100;
    T_EQ_INT(cpu_pct(&a, &b), 0, "none of it reads 0%");

    /* Rounding is half-up, which is what the header promises. 1/3 is 33.3 and 2/3 is 66.7. */
    a.busy = 0; a.total = 0;
    b.busy = 1; b.total = 3;
    T_EQ_INT(cpu_pct(&a, &b), 33, "a third rounds down to 33");
    b.busy = 2; b.total = 3;
    T_EQ_INT(cpu_pct(&a, &b), 67, "two thirds round up to 67");
    b.busy = 1; b.total = 2;
    T_EQ_INT(cpu_pct(&a, &b), 50, "and an exact half is 50, not 49");

    /* No time elapsed: 0 rather than a division by zero. A frozen /proc/stat - which is
     * exactly what a captured fixture is - lands here on every sample. */
    a.busy = 500; a.total = 1000;
    b.busy = 500; b.total = 1000;
    T_EQ_INT(cpu_pct(&a, &b), 0, "no elapsed time is 0%, not a crash");

    /* A counter that went BACKWARDS. /proc/stat is monotonic, but a cpu that was offlined
     * and came back is not, and an unsigned subtraction would wrap to a huge percentage. */
    a.busy = 500; a.total = 1000;
    b.busy = 100; b.total = 200;
    {
        int p = cpu_pct(&a, &b);
        T_TRUE(p >= 0 && p <= 100, "a counter that went backwards still reads 0-100%");
    }
}

static void t_cpu_read_times_zeroes_what_it_cannot_read(void)
{
    struct src st;
    struct cpu_times t[NCPU];

    /* The header's promise: "zeroes every entry first, so a short or unreadable file
     * yields zeros rather than stale values". Stale values would be worse than zeros here,
     * because a stale pair still produces a plausible-looking percentage. */
    for (int i = 0; i < NCPU; i++) {
        t[i].busy = 12345;
        t[i].total = 99999;
    }
    src_init(&st);                      /* never opened: absent */
    cpu_read_times(&st, t);
    for (int i = 0; i < NCPU; i++) {
        T_EQ_U64(t[i].busy, 0, "an unreadable /proc/stat zeroes busy");
        T_EQ_U64(t[i].total, 0, "and total");
    }
}

/* ---- envid: which kernel and image produced a stream ----------------------------- */

static void t_envid_reads_both_identifiers(void)
{
    struct envid e;

    put("etc/buildinfo", "IMAGE_NAME=whatever\nBUILD_ID=20260710-235\nOTHER=x\n");
    put("proc/sys/kernel/osrelease", "6.8.0-1084-qcom\n");
    io_set_sysroot(dir);
    memset(&e, 0, sizeof e);
    envid_read(&e);
    io_set_sysroot("");

    T_EQ_INT(e.have_build_id, 1, "the build id is found");
    T_EQ_STR(e.build_id, "20260710-235", "and is the value, without the key");
    T_EQ_INT(e.have_kernel, 1, "the kernel release is found");
    T_EQ_STR(e.kernel, "6.8.0-1084-qcom", "and carries no trailing newline");
}

static void t_envid_finds_build_id_wherever_it_sits(void)
{
    struct envid e;

    /* envid.c's own comment: "a line added ahead of BUILD_ID in a future image must not
     * change the answer". So the lookup is by key, and a new first line must not shift it.
     * Breaking that made the whole field go absent, silently, with nothing to notice. */
    put("etc/buildinfo", "NEW_LINE_THE_VENDOR_ADDED=1\nIMAGE=x\nBUILD_ID=20260729-248\n");
    io_set_sysroot(dir);
    memset(&e, 0, sizeof e);
    envid_read(&e);
    io_set_sysroot("");

    T_EQ_INT(e.have_build_id, 1, "a BUILD_ID further down the file is still found");
    T_EQ_STR(e.build_id, "20260729-248", "with the right value");

    /* A key that merely CONTAINS the name is not the key. */
    put("etc/buildinfo", "NOT_BUILD_ID=nonsense\nOTHER_BUILD_ID=also-no\n");
    io_set_sysroot(dir);
    memset(&e, 0, sizeof e);
    envid_read(&e);
    io_set_sysroot("");
    T_EQ_INT(e.have_build_id, 0, "a key that only contains BUILD_ID is not a match");
}

static void t_envid_absent_is_absent(void)
{
    struct envid e;
    char cmd[256];

    /* Nothing invented: an unreadable source leaves the field absent and empty, which is
     * what the meta line then reports as missing rather than as "". */
    snprintf(cmd, sizeof cmd, "rm -f '%s/etc/buildinfo' '%s/proc/sys/kernel/osrelease'",
             dir, dir);
    if (system(cmd) != 0)
        T_FAIL("%s", "cannot remove the fixture files");
    io_set_sysroot(dir);
    memset(&e, 0, sizeof e);
    envid_read(&e);
    io_set_sysroot("");

    T_EQ_INT(e.have_build_id, 0, "an absent buildinfo is absent");
    T_EQ_STR(e.build_id, "", "and empty");
    T_EQ_INT(e.have_kernel, 0, "an absent osrelease is absent");
    T_EQ_STR(e.kernel, "", "and empty");
}

/* ---- the throttle summation, against numbers ------------------------------------- */

/* /proc/interrupts as the board lays it out: a header naming the CPUs, then one line per
 * interrupt with one count per CPU. The counts here are deliberately all different, so a
 * sum that takes only the first column, or stops early, cannot come out right by luck. */
static void write_interrupts(void)
{
    put("proc/interrupts",
        "           CPU0       CPU1       CPU2       CPU3       CPU4       CPU5"
        "       CPU6       CPU7\n"
        " 11:          1          2          3          4          5          6"
        "          7          8   GICv3  23 Level     arch_timer\n"
        "223:         10         20         30         40         50         60"
        "         70         80   Interrupt dcvsh-irq-0\n"
        "224:        100        200        300        400        500        600"
        "        700        800   Interrupt dcvsh-irq-2\n"
        "225:       1000       2000       3000       4000       5000       6000"
        "       7000       8000   Interrupt dcvsh-irq-4\n");
}

static void t_throttle_sums_every_cpu_column(void)
{
    struct sample s;

    write_interrupts();
    io_set_sysroot(dir);
    memset(&s, 0, sizeof s);
    T_EQ_INT(sampler_throttle.open(), 0, "the sampler opens against this tree");
    sampler_throttle.read(&s);
    sampler_throttle.close();
    io_set_sysroot("");

    /* 10+20+...+80 = 360 for policy 0, and ten and a hundred times that for the others.
     * Taking only CPU0 would give 10, 100 and 1000; stopping at four columns would give
     * 100, 1000 and 10000. Neither can be mistaken for the right answer. */
    T_EQ_INT(s.thr_ok, 1, "a readable /proc/interrupts is a reading");
    T_EQ_U64(s.thr[0], 360, "policy 0's events are the sum of all eight CPU columns");
    T_EQ_U64(s.thr[1], 3600, "and policy 1's");
    T_EQ_U64(s.thr[2], 36000, "and policy 2's");
}

int main(void)
{
    char cmd[128];

    snprintf(dir, sizeof dir, "/tmp/qcstats-t_units.%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "cannot create %s\n", dir);
        return 2;
    }

    t_cpu_pct_is_a_percentage_of_elapsed_time();
    t_cpu_read_times_zeroes_what_it_cannot_read();
    t_envid_reads_both_identifiers();
    t_envid_finds_build_id_wherever_it_sits();
    t_envid_absent_is_absent();
    t_throttle_sums_every_cpu_column();

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: %s was left behind\n", dir);
    return t_summary("units");
}
