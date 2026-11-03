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

/* ---- net: classification, ordering, and absent-vs-zero -------------------------- */

static void net_iface(const char *name, const char *type, const char *oper,
                      const char *rx, const char *tx)
{
    char rel[160];

    snprintf(rel, sizeof rel, "sys/class/net/%s/type", name);        put(rel, type);
    snprintf(rel, sizeof rel, "sys/class/net/%s/operstate", name);   put(rel, oper);
    snprintf(rel, sizeof rel, "sys/class/net/%s/mtu", name);         put(rel, "1500\n");
    snprintf(rel, sizeof rel, "sys/class/net/%s/statistics/rx_bytes", name);
    put(rel, rx);
    snprintf(rel, sizeof rel, "sys/class/net/%s/statistics/tx_bytes", name);
    put(rel, tx);
}

/* The ARPHRD type identifies CAN and loopback, but a managed-mode Wi-Fi
 * station reports ARPHRD_ETHER (1) exactly as a wired port does - so the
 * wireless directory is the only thing that tells those two apart, and
 * getting it wrong labels every Wi-Fi adapter as Ethernet. */
static void t_net_classifies_by_more_than_arphrd(void)
{
    struct sample s;

    net_iface("eth0", "1\n",   "up\n",      "100\n", "200\n");
    net_iface("wlan0", "1\n",  "up\n",      "300\n", "400\n");
    put("sys/class/net/wlan0/wireless/x", "");
    net_iface("can0", "280\n", "down\n",    "0\n",   "0\n");
    net_iface("lo", "772\n",   "unknown\n", "7\n",   "7\n");
    put("sys/class/net/br0/type", "1\n");
    put("sys/class/net/br0/operstate", "down\n");
    put("sys/class/net/br0/bridge/x", "");

    io_set_sysroot(dir);
    T_REQUIRE(sampler_net.open() == 0, "net opens against a synthetic tree");
    memset(&s, 0, sizeof s);
    sampler_net.read(&s);

    T_EQ_INT(s.nnet, 5, "every interface in the tree is resolved");

    /* Ordered ethernet, wifi, can, bridge, loopback - fixed for the run,
     * because the CSV header written at startup describes these columns. */
    T_EQ_STR(s.net[0].name, "eth0", "ethernet sorts first");
    T_EQ_STR(s.net[0].kind, "ethernet", "ARPHRD_ETHER with no wireless/ is wired");
    T_EQ_STR(s.net[1].kind, "wifi", "ARPHRD_ETHER with wireless/ is Wi-Fi");
    T_EQ_STR(s.net[2].kind, "can", "ARPHRD_CAN is CAN");
    T_EQ_STR(s.net[3].kind, "bridge", "a bridge/ directory is a bridge");
    T_EQ_STR(s.net[4].kind, "loopback", "loopback sorts last");

    /* The kernel's own word, trimmed of its newline but not reduced to a
     * bool: "unknown" is what loopback reports and it is not "down". */
    T_EQ_STR(s.net[4].operstate, "unknown", "operstate is kept verbatim");
    T_EQ_STR(s.net[0].operstate, "up", "and the trailing newline is gone");

    sampler_net.close();
}

/* A counter that cannot be read must not report 0, which is exactly what an
 * idle link genuinely reports. Same rule as power's three attributes. */
static void t_uptime_is_whole_seconds_and_absent_when_missing(void)
{
    struct sample s;

    /* The file's first field is a float; a consumer formats "up 3d 4h" from whole
     * seconds, and sub-second precision in an uptime is noise. */
    put("proc/uptime", "21085.50 167000.92\n");
    io_set_sysroot(dir);
    T_REQUIRE(sampler_uptime.open() == 0, "uptime opens");
    memset(&s, 0, sizeof s);
    sampler_uptime.read(&s);
    T_TRUE(s.uptime_ok, "uptime reads");
    T_EQ_U64(s.uptime_s, 21085, "and is truncated to whole seconds, not rounded");
    sampler_uptime.close();

    /* Garbage is not a small uptime. */
    put("proc/uptime", "not-a-number\n");
    T_REQUIRE(sampler_uptime.open() == 0, "uptime opens on a readable file");
    memset(&s, 0, sizeof s);
    sampler_uptime.read(&s);
    T_TRUE(!s.uptime_ok, "an unparseable uptime is absent");
    T_EQ_U64(s.uptime_s, 0, "and no number is invented for it");
    sampler_uptime.close();
}

static void t_swap_none_configured_is_a_reading_not_absence(void)
{
    struct sample s;

    /* THE distinction. A kernel with swap support and nothing swapped on carries
     * SwapTotal: 0 - that is the reading "none configured". A kernel built without
     * swap carries no SwapTotal line at all, which is absence. Reporting both as
     * zero would tell a reader the same thing about two different machines. */
    put("proc/meminfo",
        "MemTotal:       15651824 kB\n"
        "MemAvailable:   14293884 kB\n"
        "SwapTotal:             0 kB\n"
        "SwapFree:              0 kB\n");
    io_set_sysroot(dir);
    T_REQUIRE(sampler_mem.open() == 0, "mem opens");
    memset(&s, 0, sizeof s);
    sampler_mem.read(&s);
    T_TRUE(s.swap_ok, "SwapTotal: 0 is a reading");
    T_EQ_U64(s.swap_total_mb, 0, "with a total of zero");
    T_EQ_U64(s.swap_used_mb, 0, "and nothing used");
    T_EQ_U64(s.ram_avail_mb, 14293884 / 1024, "and available is reported, not just used");
    sampler_mem.close();

    /* No swap fields at all. */
    put("proc/meminfo",
        "MemTotal:       15651824 kB\n"
        "MemAvailable:   14293884 kB\n");
    T_REQUIRE(sampler_mem.open() == 0, "mem opens without swap fields");
    memset(&s, 0, sizeof s);
    sampler_mem.read(&s);
    T_TRUE(s.ram_ok, "RAM still reads");
    T_TRUE(!s.swap_ok, "and swap is absent, which is not a swap of size zero");
    sampler_mem.close();

    /* One field without the other is not half an answer. */
    put("proc/meminfo",
        "MemTotal:       15651824 kB\n"
        "MemAvailable:   14293884 kB\n"
        "SwapTotal:       2097148 kB\n");
    T_REQUIRE(sampler_mem.open() == 0, "mem opens with SwapTotal alone");
    memset(&s, 0, sizeof s);
    sampler_mem.read(&s);
    T_TRUE(!s.swap_ok, "SwapTotal without SwapFree is absent, not a full swap");
    sampler_mem.close();
}

static void t_net_an_unreadable_counter_is_not_zero(void)
{
    struct sample s;

    /* rx_bytes exists, tx_bytes does not. */
    put("sys/class/net/eth1/type", "1\n");
    put("sys/class/net/eth1/operstate", "up\n");
    put("sys/class/net/eth1/statistics/rx_bytes", "4096\n");

    io_set_sysroot(dir);
    T_REQUIRE(sampler_net.open() == 0, "net opens");
    memset(&s, 0, sizeof s);
    sampler_net.read(&s);

    {
        int found = -1;
        for (int i = 0; i < s.nnet; i++)
            if (!strcmp(s.net[i].name, "eth1"))
                found = i;
        T_REQUIRE(found >= 0, "eth1 is in the list");
        T_TRUE(s.net[found].rx_bytes.ok, "a readable counter reads");
        T_EQ_U64(s.net[found].rx_bytes.v, 4096, "and carries its value");
        T_TRUE(!s.net[found].tx_bytes.ok, "an absent counter is absent");
        T_EQ_U64(s.net[found].tx_bytes.v, 0, "and its value is not trusted");
        /* speed is absent here; a down link reports -1, which is not a
         * measurement either. */
        T_TRUE(!s.net[found].speed_mbit.ok, "an absent speed is absent");
    }

    sampler_net.close();
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
    t_net_classifies_by_more_than_arphrd();
    t_net_an_unreadable_counter_is_not_zero();
    t_uptime_is_whole_seconds_and_absent_when_missing();
    t_swap_none_configured_is_a_reading_not_absence();

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: %s was left behind\n", dir);
    return t_summary("units");
}
