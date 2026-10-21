// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
// SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
//
// io.c is the primitive every sampler reads through, so a defect here reaches the output
// of every field. These are the contracts the hardening of 2026-10-16 established, each
// one written against a way a junk read used to become a measurement.

#define _GNU_SOURCE
#include "../src/io.h"
#include "t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char dir[64];

/* Write `len` bytes to dir/name and open it as a source. sysroot is "" unless a test
 * sets it, so the absolute path reaches the file. */
static void mksrc(struct src *s, const char *name, const char *content, size_t len)
{
    char path[256];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((f = fopen(path, "w")) == NULL) {
        perror(path);
        exit(2);
    }
    if (len)
        fwrite(content, 1, len, f);
    fclose(f);
    src_init(s);
    src_open(s, "%s", path);
}

static void t_u64_reads_a_value(void)
{
    struct src s;
    uint64_t v = 0;

    mksrc(&s, "freq", "2112000\n", 8);
    T_EQ_INT(src_u64(&s, &v), 0, "a plain number is read");
    T_EQ_U64(v, 2112000, "the value");
    src_close(&s);

    /* A real zero IS a measurement: pwm1 of 0 means the fan is off, which is a reading
     * and not an absence. The distinction this whole schema rests on. */
    mksrc(&s, "pwm", "0\n", 2);
    v = 99;
    T_EQ_INT(src_u64(&s, &v), 0, "zero is a value, not a failure");
    T_EQ_U64(v, 0, "zero is read as zero");
    src_close(&s);
}

static void t_u64_refuses_junk(void)
{
    struct src s;
    uint64_t v;

    /* strtoull reports unparseable input through its END POINTER, not errno, so this
     * used to return 0 with a success status - a governor name read as 0 MHz. */
    mksrc(&s, "gov", "schedutil\n", 10);
    v = 1;
    T_EQ_INT(src_u64(&s, &v), -1, "a non-numeric attribute is refused");
    T_EQ_U64(v, 1, "*out is untouched on failure");
    src_close(&s);

    /* cpu.c seeds `online = 1` for an absent attribute and ignores the return value,
     * so a junk read writing *out would have reported a live CPU as offline. */
    mksrc(&s, "online", "", 0);
    v = 1;
    T_EQ_INT(src_u64(&s, &v), -1, "an empty attribute is refused");
    T_EQ_U64(v, 1, "*out is untouched for an empty read");
    src_close(&s);

    /* A leading '-' wrapped to a huge unsigned value and reported success. */
    mksrc(&s, "neg", "-1\n", 3);
    T_EQ_INT(src_u64(&s, &v), -1, "a negative number is refused, not wrapped");
    src_close(&s);

    mksrc(&s, "huge", "99999999999999999999999\n", 24);
    T_EQ_INT(src_u64(&s, &v), -1, "an out-of-range number is refused (ERANGE)");
    src_close(&s);
}

static void t_u64_takes_the_leading_number(void)
{
    struct src s;
    uint64_t v = 0;

    /* Deliberate, not an oversight: scaling_available_frequencies is a list, and the
     * rule is "at least one digit was parsed", so the first number wins. Pinned here
     * because tightening it would silently change what several samplers read. */
    mksrc(&s, "list", "2112000 1958400 1497600\n", 24);
    T_EQ_INT(src_u64(&s, &v), 0, "a list parses");
    T_EQ_U64(v, 2112000, "the first number is the value");
    src_close(&s);
}

static void t_absent_source(void)
{
    struct src s;
    char buf[32];
    uint64_t v = 7;

    /* fd < 0 means absent and stays absent. Every reader must say so rather than
     * invent a value. */
    src_init(&s);
    T_EQ_INT(src_have(&s), 0, "an unopened source is absent");
    T_EQ_INT(src_u64(&s, &v), -1, "u64 of an absent source fails");
    T_EQ_U64(v, 7, "*out is untouched for an absent source");
    T_EQ_INT(src_str(&s, buf, sizeof buf), -1, "str of an absent source fails");
    T_EQ_INT(src_str_all(&s, buf, sizeof buf), -1, "str_all of an absent source fails");
    T_EQ_INT(src_str_ok(&s), 0, "str_ok of an absent source is false");

    /* A path that does not exist is the same answer. */
    src_open(&s, "%s/no-such-attribute", dir);
    T_EQ_INT(src_have(&s), 0, "a missing file leaves the source absent");
    src_close(&s);
}

static void t_str_truncates_and_str_all_refuses(void)
{
    struct src s;
    char small[8], big[64];
    const char *forty = "0123456789012345678901234567890123456789";

    /* src_str does ONE pread of len-1 and reports success for any positive count: the
     * caller asked for what fits. src_str_ok() depends on that - it probes 7 bytes of
     * an arbitrarily long attribute to test readability. */
    mksrc(&s, "long", forty, 40);
    T_EQ_INT(src_str(&s, small, sizeof small), 0, "a short buffer still succeeds");
    T_EQ_INT((int)strlen(small), 7, "it returns len-1 bytes, NUL-terminated");
    T_EQ_STR(small, "0123456", "and they are the leading bytes");
    T_EQ_INT(src_str_ok(&s), 1, "str_ok is true for a readable attribute");

    /* The same read through src_str_all is a failure, because its caller parses the
     * whole file: throttle.c reported 0 events for policies past the cut, and 0 is a
     * meaningful reading here. -2 distinguishes it from unreadable. */
    T_EQ_INT(src_str_all(&s, small, sizeof small), -2, "str_all refuses a filled buffer");
    T_EQ_INT(src_str_all(&s, big, sizeof big), 0, "and accepts the whole file");
    T_EQ_INT((int)strlen(big), 40, "all of it");
    src_close(&s);

    /* Exactly len-1 bytes is indistinguishable from a truncation, so it is refused.
     * The safe direction: a file that just fits is rare, a silent cut is not. */
    mksrc(&s, "exact", "0123456", 7);
    T_EQ_INT(src_str_all(&s, small, sizeof small), -2,
             "a file of exactly len-1 bytes is refused too");
    src_close(&s);
}

static void t_sysroot_relocates(void)
{
    struct src s;
    char sub[128], path[192], buf[32];
    FILE *f;

    /* The hook the whole fixture design rests on: with a sysroot set, a literal source
     * path is read from the captured tree instead of this machine. */
    snprintf(sub, sizeof sub, "%s/sys/class/thermal/thermal_zone0", dir);
    snprintf(path, sizeof path, "mkdir -p '%s'", sub);
    if (system(path) != 0) {
        T_FAIL("%s", "cannot create the fixture subtree");
        return;
    }
    snprintf(path, sizeof path, "%s/temp", sub);
    f = fopen(path, "w");
    if (!f) {
        T_FAIL("%s", "cannot write the fixture attribute");
        return;
    }
    fputs("36800\n", f);
    fclose(f);

    io_set_sysroot(dir);
    src_init(&s);
    src_open(&s, THERMAL_DIR "/thermal_zone0/temp");
    T_EQ_INT(src_have(&s), 1, "a literal path opens inside the sysroot");
    T_EQ_INT(src_str(&s, buf, sizeof buf), 0, "and reads");
    T_EQ_STR(buf, "36800\n", "the fixture's value, not this machine's");
    src_close(&s);

    /* Absolute paths on the host are unreachable while a sysroot is set, which is what
     * makes a fixture run honest: a path the capture missed fails rather than falling
     * through to the host. */
    src_init(&s);
    src_open(&s, "/proc/self/cmdline");
    T_EQ_INT(src_have(&s), 0, "a host path is not reachable through a sysroot");
    src_close(&s);
    io_set_sysroot("");
}

static void t_an_overlong_path_is_refused_not_truncated(void)
{
    struct src s;
    char attr[PATH_MAXLEN + 64], decoy[PATH_MAXLEN], buf[32];
    size_t fits;
    FILE *f;

    /* io.c's own reason: "A truncated path would silently name a different file, so refuse
     * to open one." The failure it prevents is reading the WRONG attribute and reporting
     * the answer as the right one - so the test has to make the truncated path name a file
     * that EXISTS. Without that, a missing guard merely opens a path that is not there, the
     * source reads absent, and the check passes for the wrong reason. It did, once.
     *
     * So: a decoy file whose name is exactly what "<dir>/<attr>" truncates to, holding a
     * value nothing should ever read, and then an attr long enough to truncate onto it. */
    /* Length has to come from nested DIRECTORIES: one component cannot exceed NAME_MAX,
     * which is 255, and the buffer is 768. Components of 120 keep it well clear. */
    {
        /* Generously sized: the compiler must be able to see that the composed command
         * cannot be cut, or -Werror=format-truncation rejects the file. */
        char nest[PATH_MAXLEN], comp[160], cmd[PATH_MAXLEN * 2 + 128];
        size_t want_prefix;

        nest[0] = '\0';
        memset(comp, 'd', 120);
        comp[120] = '\0';
        /* Grow the nest while another component plus a one-character basename still fits. */
        while (strlen(dir) + 1 + strlen(nest) + strlen(comp) + 3 < sizeof decoy - 1) {
            size_t n = strlen(nest);
            snprintf(nest + n, sizeof nest - n, "%s%s", n ? "/" : "", comp);
        }
        snprintf(cmd, sizeof cmd, "mkdir -p '%s/%s'", dir, nest);
        if (system(cmd) != 0) {
            T_FAIL("%s", "cannot create the nested directories");
            return;
        }

        /* The decoy's name is exactly as long as the room left in the buffer, so
         * "<dir>/<nest>/<basename>" is the string a truncation would produce. */
        want_prefix = strlen(dir) + 1 + strlen(nest) + 1;
        fits = (sizeof decoy - 1) - want_prefix;
        T_TRUE(fits >= 1 && fits <= 200, "the decoy's name fits in one component");

        memset(attr, 'x', sizeof attr - 1);
        attr[sizeof attr - 1] = '\0';
        snprintf(decoy, sizeof decoy, "%s/%s/%.*s", dir, nest, (int)fits, attr);
        if ((f = fopen(decoy, "w")) == NULL) {
            T_FAIL("%s", "cannot create the decoy file");
            return;
        }
        fputs("9999\n", f);
        fclose(f);

        /* The attr asked for: the decoy's name and then more, so the whole path overflows
         * and its truncation is the decoy exactly. */
        snprintf(attr, sizeof attr, "%s/%.*sTAIL_THAT_OVERFLOWS", nest, (int)fits,
                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
        T_TRUE(strlen(dir) + 1 + strlen(attr) >= sizeof decoy,
               "the full path is longer than the buffer, so it would be cut");
    }

    src_init(&s);
    src_open_abs(&s, dir, attr);
    T_EQ_INT(src_have(&s), 0, "a path that cannot fit is refused, not cut short");
    /* The decoy is readable, so a guard that was missing would have opened THIS. */
    T_EQ_INT(src_str(&s, buf, sizeof buf), -1, "so nothing is read from the truncated name");
    src_close(&s);

    /* And a path that DOES fit still opens, so the guard is a bound and not a refusal. */
    src_init(&s);
    src_open_abs(&s, dir, "freq");
    T_EQ_INT(src_have(&s), 1, "while a path that fits opens normally");
    src_close(&s);
}

int main(void)
{
    char cmd[128];

    snprintf(dir, sizeof dir, "/tmp/qcstats-t_io.%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "cannot create %s\n", dir);
        return 2;
    }

    t_u64_reads_a_value();
    t_u64_refuses_junk();
    t_u64_takes_the_leading_number();
    t_absent_source();
    t_str_truncates_and_str_all_refuses();
    t_sysroot_relocates();
    t_an_overlong_path_is_refused_not_truncated();

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: %s was left behind\n", dir);
    return t_summary("io");
}
