// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Output formats. Each emitter describes one already-collected struct sample, so
// all three necessarily agree about a given tick.
//
// The rule every format follows: an ABSENT source is distinguishable from a source
// that read zero. plain prints n/a, json emits null, csv leaves the field empty.
// qcctl has to tell "sensor missing" from "sensor idle".

#define _GNU_SOURCE
#include "emit.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

/* Where samples go. Defaults to stdout; --logfile points it at a file so a dropped
 * terminal cannot lose data, and so stderr notices stay separate from the data. */
static FILE *out;

void emit_to(FILE *f)
{
    out = f;
}

static FILE *stream(void)
{
    return out ? out : stdout;
}

/* Borrowed, set once before the first emission. Never owned, never copied: it is two short
 * strings that cannot change during a run. */
static const struct envid *env;

void emit_set_env(const struct envid *e)
{
    env = e;
}

/* These come from files on disk, so they are not trusted to be JSON-safe. Escaping what the
 * schema calls a string is not optional just because the value "looks like" a version. */
static void jstr(const char *v)
{
    fputc('"', stream());
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", stream()); break;
        case '\\': fputs("\\\\", stream()); break;
        case '\n': fputs("\\n", stream());  break;
        case '\r': fputs("\\r", stream());  break;
        case '\t': fputs("\\t", stream());  break;
        default:
            if (*p < 0x20)
                fprintf(stream(), "\\u%04x", *p);
            else
                fputc(*p, stream());
        }
    }
    fputc('"', stream());
}

/* WHY A META LINE AND NOT A FIELD ON EVERY SAMPLE. The environment is not a reading - it is
 * a property of the run - and repeating two constant strings on every line of a 20 Hz stream
 * is waste the schema does not need to carry.
 *
 * It keeps the NDJSON promise: this is still one complete, independently parseable object on
 * its own line, and "v" is still the first field so the existing dispatch rule holds. It is
 * identified by carrying "meta" and NOT carrying "t" - a sample always has a timestamp, this
 * never does. A consumer iterating samples skips it by testing for "t".
 *
 * Verified against the real consumer before choosing this shape: qcctl's reader is
 * strstr-based (its src/stats.c), finds no "thermal_c", and reports the line as received
 * with no temperature - the same path a null reading already takes, which it already tests.
 * It does cost one of that reader's three allowed misses, which the first real sample then
 * resets; the retry budget itself is driven by timeouts and EOF. */
void emit_meta_json(void)
{
    fprintf(stream(), "{\"v\":%d,\"meta\":{\"kernel\":", QCSTATS_JSON_SCHEMA);
    if (env && env->have_kernel) jstr(env->kernel); else fputs("null", stream());
    fprintf(stream(), ",\"build_id\":");
    if (env && env->have_build_id) jstr(env->build_id); else fputs("null", stream());
    fprintf(stream(), "}}\n");
}

void emit_meta_human(void)
{
    fprintf(stream(), "env kernel %s image %s\n",
            (env && env->have_kernel)   ? env->kernel   : "n/a",
            (env && env->have_build_id) ? env->build_id : "n/a");
}

void emit_human(const struct sample *s)
{
    if (s->ram_ok)
        fprintf(stream(), "RAM %lu/%luMB ", s->ram_used_mb, s->ram_total_mb);
    else
        fprintf(stream(), "RAM n/a ");

    fprintf(stream(), "CPU [");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), "%s%s:", p ? " " : "", POLICY_LABEL[p]);
        for (int c = s->pol[p].first; c <= s->pol[p].last && c < NCPU; c++) {
            const char *sep = (c == s->pol[p].first) ? "" : ",";
            if (!s->pol[p].online[c])
                fprintf(stream(), "%soff", sep);
            else if (s->load_ok)
                fprintf(stream(), "%s%d%%", sep, s->pol[p].pct[c]);
            else
                fprintf(stream(), "%sn/a", sep);
        }
        if (s->pol[p].mhz_ok)
            fprintf(stream(), "@%luMHz", s->pol[p].mhz);
        else
            fprintf(stream(), "@n/a");
    }
    fprintf(stream(), "] ");

    if (s->gpu_ok && s->gpu_busy_ok)
        fprintf(stream(), "GPU %d%%@%luMHz ", s->gpu_busy_pct, s->gpu_mhz);
    else if (s->gpu_ok)
        fprintf(stream(), "GPU %luMHz ", s->gpu_mhz);   /* busy needs --gpu-busy */
    else
        fprintf(stream(), "GPU n/a ");

    for (int i = 0; i < s->nzone; i++)
        if (s->zone[i].ok)
            fprintf(stream(), "%s@%.1fC ", s->zone[i].label, s->zone[i].c);

    if (!s->pwr_ok) {
        fprintf(stream(), "VDD_IN n/a ");   /* CONFIG_SENSORS_INA2XX=m - may be unloaded */
    } else {
        fprintf(stream(), "VDD_IN ");
        if (s->pwr_mw_ok) fprintf(stream(), "%lumW (", s->pwr_mw); else fprintf(stream(), "n/a (");
        if (s->pwr_mv_ok) fprintf(stream(), "%.1fV ", s->pwr_mv / 1000.0); else fprintf(stream(), "n/a ");
        if (s->pwr_ma_ok) fprintf(stream(), "%lumA) ", s->pwr_ma); else fprintf(stream(), "n/a) ");
    }

    if (s->fan_ok) {
        fprintf(stream(), "FAN ");
        if (s->fan_duty_ok) fprintf(stream(), "%lu%%@", s->fan_duty_pct); else fprintf(stream(), "n/a@");
        if (s->fan_rpm_ok)  fprintf(stream(), "%lurpm", s->fan_rpm);      else fprintf(stream(), "n/arpm");
        fprintf(stream(), "%s ", s->fan_rpm_valid ? "" : "?");
    }

    if (s->npu_ok)
        fprintf(stream(), "NPU %.0f%%@%luMHz hvx:%.0f%% hmx:%.0f%% ",
                s->npu_q6_pct, (unsigned long)(s->npu_q6_khz / 1000),
                s->npu_hvx_pct, s->npu_hmx_pct);

    fprintf(stream(), "LMh [");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), "%s%s:", p ? " " : "", POLICY_LABEL[p]);
        if (s->thr_ok)
            fprintf(stream(), "%lu", s->thr[p]);
        else
            fprintf(stream(), "n/a");
    }
    fprintf(stream(), "]\n");
}

/* NDJSON: one object per line, so a consumer can read it as a stream.
 * Absent sources are `null`, never the string "n/a" - qcctl has to tell
 * "sensor missing" from "sensor read zero". All keys and labels are our own
 * ASCII literals, so no JSON escaping is needed. */
void emit_json(const struct sample *s)
{
    /* Schema version first, so a consumer can dispatch on it before reading anything
     * else. Bump it only for a BREAKING change - see docs/json-schema-v1.md. */
    fprintf(stream(), "{\"v\":%d,\"t\":%.3f", QCSTATS_JSON_SCHEMA, s->t);

    fprintf(stream(), ",\"ram\":");
    if (s->ram_ok)
        fprintf(stream(), "{\"used_mb\":%lu,\"total_mb\":%lu}", s->ram_used_mb, s->ram_total_mb);
    else
        fprintf(stream(), "null");

    fprintf(stream(), ",\"cpu\":[");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), "%s{\"label\":\"%s\",\"mhz\":", p ? "," : "", POLICY_LABEL[p]);
        if (s->pol[p].mhz_ok)
            fprintf(stream(), "%lu", s->pol[p].mhz);
        else
            fprintf(stream(), "null");
        fprintf(stream(), ",\"cores\":[");
        for (int c = s->pol[p].first; c <= s->pol[p].last && c < NCPU; c++) {
            fprintf(stream(), "%s{\"cpu\":%d,\"pct\":", (c == s->pol[p].first) ? "" : ",", c);
            /* null covers both offline and "load could not be measured" - in each
             * case there is no percentage, and 0 would be a lie. */
            if (s->pol[p].online[c] && s->load_ok)
                fprintf(stream(), "%d}", s->pol[p].pct[c]);
            else
                fprintf(stream(), "null}");
        }
        fprintf(stream(), "]}");
    }
    fprintf(stream(), "]");

    fprintf(stream(), ",\"gpu_mhz\":");
    if (s->gpu_ok)
        fprintf(stream(), "%lu", s->gpu_mhz);
    else
        fprintf(stream(), "null");
    fprintf(stream(), ",\"gpu_busy_pct\":");
    if (s->gpu_busy_ok)
        fprintf(stream(), "%d", s->gpu_busy_pct);
    else
        fprintf(stream(), "null");     /* needs --gpu-busy and root */

    fprintf(stream(), ",\"npu\":");
    if (s->npu_ok)
        fprintf(stream(), "{\"q6_pct\":%.2f,\"q6_khz\":%lu,\"hvx_pct\":%.2f,"
                          "\"hmx_pct\":%.2f}",
                s->npu_q6_pct, s->npu_q6_khz, s->npu_hvx_pct, s->npu_hmx_pct);
    else
        fprintf(stream(), "null");     /* needs --npu and fastrpc access */

    fprintf(stream(), ",\"thermal_c\":{");
    for (int i = 0; i < s->nzone; i++) {
        fprintf(stream(), "%s\"%s\":", i ? "," : "", s->zone[i].label);
        if (s->zone[i].ok)
            fprintf(stream(), "%.1f", s->zone[i].c);
        else
            fprintf(stream(), "null");        /* key stays, so the schema is stable */
    }
    fprintf(stream(), "}");

    fprintf(stream(), ",\"power\":");
    if (!s->pwr_ok) {
        fprintf(stream(), "null");
    } else {
        fprintf(stream(), "{\"mw\":");
        if (s->pwr_mw_ok) fprintf(stream(), "%lu", s->pwr_mw); else fprintf(stream(), "null");
        fprintf(stream(), ",\"mv\":");
        if (s->pwr_mv_ok) fprintf(stream(), "%lu", s->pwr_mv); else fprintf(stream(), "null");
        fprintf(stream(), ",\"ma\":");
        if (s->pwr_ma_ok) fprintf(stream(), "%lu", s->pwr_ma); else fprintf(stream(), "null");
        fprintf(stream(), "}");
    }

    fprintf(stream(), ",\"fan\":");
    if (!s->fan_ok) {
        fprintf(stream(), "null");
    } else {
        fprintf(stream(), "{\"duty_pct\":");
        if (s->fan_duty_ok) fprintf(stream(), "%lu", s->fan_duty_pct); else fprintf(stream(), "null");
        fprintf(stream(), ",\"rpm\":");
        if (s->fan_rpm_ok) fprintf(stream(), "%lu", s->fan_rpm); else fprintf(stream(), "null");
        fprintf(stream(), ",\"rpm_valid\":%s}", s->fan_rpm_valid ? "true" : "false");
    }

    fprintf(stream(), ",\"throttle\":{");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), "%s\"%s\":", p ? "," : "", POLICY_LABEL[p]);
        if (s->thr_ok)
            fprintf(stream(), "%lu", s->thr[p]);
        else
            fprintf(stream(), "null");     /* 0 would claim "nothing throttled" */
    }
    fprintf(stream(), "}}\n");
}

/* NPU: the NSP has no clock in clk_summary, no devfreq and no OPP table, and its
 * utilisation is not exposed in sysfs at all - so this comes from the DSP-side sysmon
 * service over FastRPC. See src/npu.c. */

/* CSV. A fixed column set per run, so the header written at startup describes every
 * row that follows. Absent value = empty field, matching JSON's null: a consumer has
 * to tell "sensor missing" from "sensor read zero". */

/* Zone labels come from sysfs "type" strings we do not control, so quote per
 * RFC 4180 rather than assuming no commas. The WHOLE field is quoted - quoting only
 * the label part yields temp_"we,ird"_c, which readers split into two fields. */
static void csv_field(const char *s)
{
    if (!strpbrk(s, ",\"\r\n")) {
        fputs(s, stream());
        return;
    }
    fputc('"', stream());
    for (; *s; s++) {
        if (*s == '"')
            fputc('"', stream());       /* doubled, per RFC 4180 */
        fputc(*s, stream());
    }
    fputc('"', stream());
}

/* Compose "<prefix><label><suffix>" then quote it as one field. */
static void csv_named(const char *prefix, const char *label, const char *suffix)
{
    char name[160];
    snprintf(name, sizeof name, "%s%s%s", prefix, label, suffix);
    csv_field(name);
}

void emit_csv_header(const struct sample *s)
{
    fprintf(stream(), "t,ram_used_mb,ram_total_mb");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), ",");
        csv_named("", POLICY_LABEL[p], "_mhz");
        for (int c = s->pol[p].first; c <= s->pol[p].last && c < NCPU; c++)
            fprintf(stream(), ",cpu%d_pct", c);
    }
    fprintf(stream(), ",gpu_mhz,gpu_busy_pct,npu_q6_pct,npu_q6_khz,npu_hvx_pct,npu_hmx_pct");
    for (int i = 0; i < s->nzone; i++) {
        fprintf(stream(), ",");
        csv_named("temp_", s->zone[i].label, "_c");
    }
    fprintf(stream(), ",power_mw,power_mv,power_ma");
    fprintf(stream(), ",fan_duty_pct,fan_rpm,fan_rpm_valid");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), ",");
        csv_named("throttle_", POLICY_LABEL[p], "");
    }
    /* WHY CSV REPEATS THESE ON EVERY ROW instead of taking a meta line like --json.
     * CSV's contract is rectangular data, and a leading comment or second header line is
     * rejected or mis-parsed by a real share of readers unless the caller knows to pass an
     * option for it. Two constant string columns are waste, but they are waste no parser is
     * ever surprised by - and a CSV capture with no provenance is the exact problem this
     * change exists to fix, so leaving CSV out was not an option either. */
    fprintf(stream(), ",kernel,build_id");
    fprintf(stream(), "\n");
}

void emit_csv(const struct sample *s)
{
    fprintf(stream(), "%.3f,", s->t);
    if (s->ram_ok)
        fprintf(stream(), "%lu,%lu", s->ram_used_mb, s->ram_total_mb);
    else
        fprintf(stream(), ",");
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), ",");
        if (s->pol[p].mhz_ok)
            fprintf(stream(), "%lu", s->pol[p].mhz);
        for (int c = s->pol[p].first; c <= s->pol[p].last && c < NCPU; c++) {
            fprintf(stream(), ",");
            /* empty = offline or unmeasurable; never 0, which would read as idle */
            if (s->pol[p].online[c] && s->load_ok)
                fprintf(stream(), "%d", s->pol[p].pct[c]);
        }
    }
    fprintf(stream(), ",");
    if (s->gpu_ok)
        fprintf(stream(), "%lu", s->gpu_mhz);
    fprintf(stream(), ",");
    if (s->gpu_busy_ok)
        fprintf(stream(), "%d", s->gpu_busy_pct);
    if (s->npu_ok)
        fprintf(stream(), ",%.2f,%lu,%.2f,%.2f", s->npu_q6_pct, s->npu_q6_khz,
                s->npu_hvx_pct, s->npu_hmx_pct);
    else
        fprintf(stream(), ",,,,");
    for (int i = 0; i < s->nzone; i++) {
        fprintf(stream(), ",");
        if (s->zone[i].ok)
            fprintf(stream(), "%.1f", s->zone[i].c);
    }
    if (!s->pwr_ok) {
        fprintf(stream(), ",,,");
    } else {
        fprintf(stream(), ",");
        if (s->pwr_mw_ok) fprintf(stream(), "%lu", s->pwr_mw);
        fprintf(stream(), ",");
        if (s->pwr_mv_ok) fprintf(stream(), "%lu", s->pwr_mv);
        fprintf(stream(), ",");
        if (s->pwr_ma_ok) fprintf(stream(), "%lu", s->pwr_ma);
    }
    if (!s->fan_ok) {
        fprintf(stream(), ",,,");
    } else {
        fprintf(stream(), ",");
        if (s->fan_duty_ok) fprintf(stream(), "%lu", s->fan_duty_pct);
        fprintf(stream(), ",");
        if (s->fan_rpm_ok) fprintf(stream(), "%lu", s->fan_rpm);
        fprintf(stream(), ",%d", s->fan_rpm_valid ? 1 : 0);
    }
    for (int p = 0; p < NPOLICY; p++) {
        fprintf(stream(), ",");
        if (s->thr_ok)
            fprintf(stream(), "%lu", s->thr[p]);
    }
    /* An empty field is this format's null, the same convention every absent reading above
     * uses. csv_field() because these come from files: a value containing a comma or a quote
     * must not be able to shift every column after it. */
    fprintf(stream(), ",");
    if (env && env->have_kernel)   csv_field(env->kernel);
    fprintf(stream(), ",");
    if (env && env->have_build_id) csv_field(env->build_id);
    fprintf(stream(), "\n");
}

