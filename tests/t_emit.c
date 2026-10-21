// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
// SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
//
// The JSON emitter, which is this project's contract with a consumer. Two rules are under
// test, and both are load-bearing for qcctl:
//
//   Schema rule 1 - an absent reading emits null and never 0. A 0 that was never measured
//   is the worst output this sampler can produce, because it is indistinguishable from a
//   real measurement.
//
//   Field spelling and order - qcctl parses this stream with strstr, so a renamed or
//   reordered key makes it read "not measured" forever, which is a valid answer here and
//   therefore silent.
//
// emit_to() redirects the stream, so no output is captured from a pipe or a terminal.

#define _GNU_SOURCE
#include "../src/qcstats.h"
#include "../src/emit.h"
#include "t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[8192];

/* Capture whatever `fn` writes, so the non-JSON emitters can be asserted too: CSV and the
 * default human format had no test at all, and the meta line - the one qcctl keys on - was
 * only ever checked for its first 14 bytes by a shell test. */
static const char *captured(void (*fn)(const struct sample *), const struct sample *s)
{
    FILE *f = tmpfile();
    long n;

    if (!f) {
        T_FAIL("%s", "cannot open a temp file for the emitter");
        buf[0] = '\0';
        return buf;
    }
    emit_to(f);
    fn(s);
    fflush(f);
    rewind(f);
    n = (long)fread(buf, 1, sizeof buf - 1, f);
    buf[n > 0 ? n : 0] = '\0';
    fclose(f);
    emit_to(stdout);
    return buf;
}

/* The same, for the two emitters that take no sample. */
static const char *captured_meta(void (*fn)(void))
{
    FILE *f = tmpfile();
    long n;

    if (!f) {
        T_FAIL("%s", "cannot open a temp file for the emitter");
        buf[0] = '\0';
        return buf;
    }
    emit_to(f);
    fn();
    fflush(f);
    rewind(f);
    n = (long)fread(buf, 1, sizeof buf - 1, f);
    buf[n > 0 ? n : 0] = '\0';
    fclose(f);
    emit_to(stdout);
    return buf;
}

/* Emit one sample into a temp file and read it back as a string. */
static const char *emitted(const struct sample *s)
{
    FILE *f = tmpfile();
    long n;

    if (!f) {
        T_FAIL("%s", "cannot open a temp file for the emitter");
        buf[0] = '\0';
        return buf;
    }
    emit_to(f);
    emit_json(s);
    fflush(f);
    rewind(f);
    n = (long)fread(buf, 1, sizeof buf - 1, f);
    buf[n > 0 ? n : 0] = '\0';
    fclose(f);
    emit_to(stdout);
    return buf;
}

static void t_absence_is_null_never_zero(void)
{
    struct sample s;
    const char *j;

    /* A sample where nothing could be measured. Every one of these would be a plausible
     * zero: 0 MHz, 0 rpm, 0 degrees, 0 mW, 0 throttle events. */
    memset(&s, 0, sizeof s);
    s.nzone = 1;
    snprintf(s.zone[0].label, sizeof s.zone[0].label, "cpuss-0-0");
    j = emitted(&s);

    T_HAS(j, "\"ram\":null", "absent RAM is null");
    T_HAS(j, "\"mhz\":null", "absent frequency is null");
    T_HAS(j, "\"gpu_mhz\":null", "absent GPU frequency is null");
    T_HAS(j, "\"gpu_busy_pct\":null", "absent GPU busy is null, not 0 percent");
    T_HAS(j, "\"npu\":null", "absent NPU is null");
    T_HAS(j, "\"cpuss-0-0\":null", "an unreadable zone keeps its key and reads null");
    T_HAS(j, "\"power\":null", "absent power is null");
    T_HAS(j, "\"fan\":null", "absent fan is null");
    T_HAS(j, "\"A78C.0\":null", "absent throttle counts are null, not zero events");

    /* The one that cost a real bug: a throttle count of 0 is a MEASUREMENT - "nothing
     * throttled" - so it must never stand in for "could not read". */
    T_HASNT(j, "\"gpu_mhz\":0", "no field is emitted as a bare 0 while absent");
    T_HASNT(j, "\"pct\":0", "an offline or unmeasured core is null, not 0%");
}

static void t_present_values_are_emitted(void)
{
    struct sample s;
    const char *j;

    memset(&s, 0, sizeof s);
    s.t = 1.5;
    s.ram_ok = 1; s.ram_used_mb = 1024; s.ram_total_mb = 7777;
    s.pol[0].mhz_ok = 1; s.pol[0].mhz = 2112;
    s.gpu_ok = 1; s.gpu_mhz = 877;
    s.thr_ok = 1; s.thr[0] = 12;
    s.nzone = 1;
    snprintf(s.zone[0].label, sizeof s.zone[0].label, "cpuss-0-0");
    s.zone[0].ok = 1; s.zone[0].c = 36.8;
    j = emitted(&s);

    T_HAS(j, "\"ram\":{\"used_mb\":1024,\"total_mb\":7777}", "RAM is an object");
    T_HAS(j, "\"mhz\":2112", "a frequency is a number");
    T_HAS(j, "\"gpu_mhz\":877", "so is the GPU's");
    T_HAS(j, "\"cpuss-0-0\":36.8", "a zone reads one decimal");
    T_HAS(j, "\"A78C.0\":12", "and a throttle count is the count");

    /* A real zero IS a value, and the distinction only works if it survives. */
    s.thr[0] = 0;
    j = emitted(&s);
    T_HAS(j, "\"A78C.0\":0", "a measured zero is emitted as 0, not null");
}

static void t_the_fields_qcctl_parses(void)
{
    struct sample s;
    const char *j;

    /* qcctl reads exactly these by name, with strstr. The spellings are the contract: it
     * looks for "fan":, then duty_pct, rpm and rpm_valid inside it; "v":; "cpu": with
     * label and mhz; and "thermal_c". A rename here is silent over there. */
    memset(&s, 0, sizeof s);
    s.fan_ok = 1;
    s.fan_duty_ok = 1; s.fan_duty_pct = 40;
    s.fan_rpm_ok = 1; s.fan_rpm = 2666;
    s.fan_rpm_valid = 1;
    s.pol[0].mhz_ok = 1; s.pol[0].mhz = 2112;
    s.nzone = 1;
    snprintf(s.zone[0].label, sizeof s.zone[0].label, "cpuss-0-0");
    s.zone[0].ok = 1; s.zone[0].c = 41.2;
    j = emitted(&s);

    T_HAS(j, "\"v\":1", "the schema version, which qcctl checks before anything else");
    T_HAS(j, "\"cpu\":[", "the cpu array");
    T_HAS(j, "\"label\":\"A78C.0\"", "a policy label");
    T_HAS(j, "\"mhz\":2112", "and its frequency");
    T_HAS(j, "\"thermal_c\":{", "the thermal object");
    T_HAS(j, "\"fan\":{\"duty_pct\":40,\"rpm\":2666,\"rpm_valid\":true}",
          "and the fan object, whole, in the order qcctl's parser walks");

    /* Version first: a consumer must be able to dispatch on the schema before reading
     * anything whose meaning the schema decides. */
    T_EQ_INT(strncmp(j, "{\"v\":1,", 7), 0, "the version is the first field in the line");

    /* rpm_valid is a bare boolean, not a string or a number. It gates qcctl's airflow
     * judgement, so its type is part of the contract. */
    s.fan_rpm_valid = 0;
    j = emitted(&s);
    T_HAS(j, "\"rpm_valid\":false", "and false when the reading is not trustworthy");
}

static void t_one_line_per_sample(void)
{
    struct sample s;
    const char *j;
    size_t n;

    /* NDJSON: qcctl reassembles lines from a pipe, so exactly one newline, at the end. */
    memset(&s, 0, sizeof s);
    j = emitted(&s);
    n = strlen(j);
    T_TRUE(n > 2, "something was emitted");
    T_EQ_INT(j[n - 1], '\n', "the line ends with a newline");
    T_EQ_INT((int)(strchr(j, '\n') - j), (int)(n - 1), "and contains no other newline");
}

/* A sample with every field present, shared by the format tests below.
 *
 * Note the PER-ATTRIBUTE ok flags: pwr_ok says the sensor was found, and pwr_mw_ok says that
 * one file read. The three power files fail independently, and so do duty and rpm, which is
 * why a single "present" flag is not enough - setting only pwr_ok leaves all three columns
 * empty, correctly. That cost this test a wrong expectation before it cost anyone a wrong
 * reading. */
static void full_sample(struct sample *s)
{
    memset(s, 0, sizeof *s);
    s->t = 1792417016.024;
    s->ram_ok = 1; s->ram_used_mb = 1326; s->ram_total_mb = 15284;

    s->pol[0].mhz_ok = 1; s->pol[0].mhz = 1017;
    s->pol[0].first = 0; s->pol[0].last = 1;
    s->pol[1].mhz_ok = 1; s->pol[1].mhz = 940;
    s->pol[1].first = 2; s->pol[1].last = 3;
    s->pol[2].mhz_ok = 1; s->pol[2].mhz = 844;
    s->pol[2].first = 4; s->pol[2].last = 7;
    s->load_ok = 1;
    for (int p = 0; p < NPOLICY; p++)
        for (int c = s->pol[p].first; c <= s->pol[p].last; c++) {
            s->pol[p].online[c] = 1;
            s->pol[p].pct[c] = 7 + c;
        }

    s->gpu_ok = 1; s->gpu_mhz = 479;
    s->pwr_ok = 1;
    s->pwr_mw = 11440; s->pwr_mw_ok = 1;
    s->pwr_mv = 20062; s->pwr_mv_ok = 1;
    s->pwr_ma = 574;   s->pwr_ma_ok = 1;
    s->fan_ok = 1;
    s->fan_duty_pct = 40; s->fan_duty_ok = 1;
    s->fan_rpm = 2588;    s->fan_rpm_ok = 1;
    s->fan_rpm_valid = 1;
    s->thr_ok = 1; s->thr[0] = 0; s->thr[1] = 0; s->thr[2] = 0;
    s->nzone = 2;
    snprintf(s->zone[0].label, sizeof s->zone[0].label, "cpuss-0-0");
    s->zone[0].ok = 1; s->zone[0].c = 36.8;
    snprintf(s->zone[1].label, sizeof s->zone[1].label, "ddrss-0");
    s->zone[1].ok = 1; s->zone[1].c = 36.5;
}

static void t_the_csv_header_and_row_line_up(void)
{
    struct sample s;
    char header[4096], row[4096];
    int hn = 0, rn = 0;

    /* CSV had no test anywhere. A dropped header column silently misaligns every field
     * after it for whatever is reading the file, which is the whole point of the format. */
    full_sample(&s);
    snprintf(header, sizeof header, "%s", captured(( void (*)(const struct sample *) )
                                                  emit_csv_header, &s));
    snprintf(row, sizeof row, "%s", captured(emit_csv, &s));

    for (const char *p = header; *p; p++) if (*p == ',') hn++;
    for (const char *p = row; *p; p++) if (*p == ',') rn++;
    T_EQ_INT(rn, hn, "the row has exactly as many fields as the header names");
    T_TRUE(hn > 20, "and there are a couple of dozen of them, not a handful");

    T_HAS(header, "t,ram_used_mb,ram_total_mb", "the header starts with time and RAM");
    T_HAS(header, "power_mw,power_mv,power_ma", "and carries all three power columns");
    T_HAS(header, "temp_cpuss-0-0_c", "with one column per zone, named for the zone");
    T_HAS(row, "1326,15284", "the row carries the RAM values in that order");
    T_HAS(row, "11440,20062,574", "and the power values in theirs");
    T_HASNT(row, "\"", "a row of ordinary values needs no quoting");

    /* An absent field is an EMPTY column, not a zero: the same rule as null in JSON. */
    {
        struct sample a;
        const char *r;

        memset(&a, 0, sizeof a);
        a.nzone = 1;
        snprintf(a.zone[0].label, sizeof a.zone[0].label, "cpuss-0-0");
        r = captured(emit_csv, &a);
        T_HAS(r, ",,", "an unmeasured column is empty");
        T_HASNT(r, ",0,", "and never a zero standing in for it");
    }
}

static void t_a_zone_label_with_a_comma_is_quoted(void)
{
    struct sample s;
    const char *h;

    /* csv_field() exists because zone labels are not ours: --zones takes them from the
     * command line and the default set from the kernel's own `type` strings. A label with
     * a comma in it would split one column into two for every reader of the file, and
     * RFC 4180 says a quote inside a quoted field is doubled. Nothing tested either. */
    full_sample(&s);
    snprintf(s.zone[0].label, sizeof s.zone[0].label, "we,ird");
    h = captured(( void (*)(const struct sample *) )emit_csv_header, &s);
    T_HAS(h, "\"temp_we,ird_c\"", "a label with a comma is quoted whole");

    full_sample(&s);
    snprintf(s.zone[0].label, sizeof s.zone[0].label, "qu\"ote");
    h = captured(( void (*)(const struct sample *) )emit_csv_header, &s);
    T_HAS(h, "\"\"", "a quote inside a field is doubled, as RFC 4180 requires");
}

static void t_the_human_format_says_what_it_shows(void)
{
    struct sample s;
    const char *h;

    /* The DEFAULT output mode, and it had one assertion anywhere: that the string "RAM "
     * appears. Swapping used and total in the display passed. */
    full_sample(&s);
    h = captured(emit_human, &s);

    T_HAS(h, "RAM 1326/15284MB", "RAM reads used of total, in that order");
    T_HAS(h, "1017MHz", "a policy's frequency carries its unit");
    T_HAS(h, "A78C.0", "and its label");
    T_HAS(h, "36.8", "a zone temperature is shown");
    T_HAS(h, "2588", "so is the fan's speed");
    T_HASNT(h, "{", "and none of it is JSON");

    {
        struct sample a;
        const char *p;

        /* n/a, not 0: the human format has to make absence visible too. */
        memset(&a, 0, sizeof a);
        a.nzone = 1;
        snprintf(a.zone[0].label, sizeof a.zone[0].label, "cpuss-0-0");
        p = captured(emit_human, &a);
        T_HAS(p, "n/a", "an unmeasured field reads n/a");
    }
}

static void t_the_meta_line_carries_the_environment(void)
{
    struct envid e;
    const char *j, *h;

    /* The meta line is what qcctl reads to know which kernel and image produced a stream,
     * and no test had ever looked past its first 14 bytes. */
    memset(&e, 0, sizeof e);
    snprintf(e.kernel, sizeof e.kernel, "6.8.0-1084-qcom");
    snprintf(e.build_id, sizeof e.build_id, "20260710-235");
    e.have_kernel = e.have_build_id = 1;
    emit_set_env(&e);

    j = captured_meta(emit_meta_json);
    T_HAS(j, "\"kernel\":\"6.8.0-1084-qcom\"", "the kernel release is in the JSON meta line");
    T_HAS(j, "\"build_id\":\"20260710-235\"", "and the image build id");
    T_HAS(j, "\"meta\":", "under a meta key, which is how a consumer tells it from a sample");

    h = captured_meta(emit_meta_human);
    T_HAS(h, "6.8.0-1084-qcom", "the human meta line names the kernel too");
    T_HAS(h, "20260710-235", "and the image");
    T_HASNT(h, "{", "without being JSON");

    /* Absent is absent here as well: an unreadable /etc/buildinfo must not become "". */
    memset(&e, 0, sizeof e);
    emit_set_env(&e);
    j = captured_meta(emit_meta_json);
    T_HASNT(j, "\"build_id\":\"\"", "an unreadable build id is not an empty string");
}

int main(void)
{
    t_absence_is_null_never_zero();
    t_the_csv_header_and_row_line_up();
    t_a_zone_label_with_a_comma_is_quoted();
    t_the_human_format_says_what_it_shows();
    t_the_meta_line_carries_the_environment();
    t_present_values_are_emitted();
    t_the_fields_qcctl_parses();
    t_one_line_per_sample();
    return t_summary("emit");
}
