// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Platform access layer. The only file that opens anything.

#define _GNU_SOURCE
#include "io.h"
#include "qcstats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

const int   POLICY_CPU[NPOLICY]   = { 0, 2, 4 };
const char *POLICY_LABEL[NPOLICY] = { "A78C.0", "A78C.1", "A55" };

/* Zones worth printing by default; 48 exist. --zones / --all-zones replace this. */
static const char *ZONES_DEFAULT[] = {
    "cpuss-0-0-thermal",   /* also the only zone that drives the fan */
    "cpuss-1-0-thermal",
    "gpuss-0-thermal",
    "nsp-0-0-0-thermal",   /* NPU */
    "ddrss-0-thermal",
};

struct config cfg = {
    .zones  = ZONES_DEFAULT,
    .nzones = sizeof ZONES_DEFAULT / sizeof ZONES_DEFAULT[0],
};

const char *sysroot = "";

void io_set_sysroot(const char *dir)
{
    sysroot = dir;
}

void spath(char *out, size_t len, const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(sysroot);

    if (n >= len)
        n = 0;                  /* pathological sysroot; fall back to bare path */
    else
        memcpy(out, sysroot, n);
    va_start(ap, fmt);
    vsnprintf(out + n, len - n, fmt, ap);
    va_end(ap);
}

void src_init(struct src *s)
{
    s->fd = -1;
}

void src_close(struct src *s)
{
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
}

void src_open(struct src *s, const char *fmt, ...)
{
    char path[PATH_MAXLEN];
    va_list ap;
    size_t n = strlen(sysroot);

    if (n >= sizeof path)
        n = 0;
    else
        memcpy(path, sysroot, n);
    va_start(ap, fmt);
    vsnprintf(path + n, sizeof path - n, fmt, ap);
    va_end(ap);
    s->fd = open(path, O_RDONLY | O_CLOEXEC);
}

/* dir is already sysroot-prefixed (hwmon and zone dirs come back that way).
 * A truncated path would silently name a different file, so refuse to open one. */
void src_open_abs(struct src *s, const char *dir, const char *attr)
{
    char path[PATH_MAXLEN];
    int n = snprintf(path, sizeof path, "%s/%s", dir, attr);

    s->fd = (n > 0 && (size_t)n < sizeof path)
          ? open(path, O_RDONLY | O_CLOEXEC) : -1;
}

int src_str(const struct src *s, char *buf, size_t len)
{
    ssize_t n;

    if (s->fd < 0)
        return -1;
    n = pread(s->fd, buf, len - 1, 0);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    return 0;
}

int src_str_ok(const struct src *s)
{
    char probe[8];
    return src_str(s, probe, sizeof probe) == 0;
}

int src_u64(const struct src *s, uint64_t *out)
{
    char buf[64], *end;
    uint64_t v;

    if (src_str(s, buf, sizeof buf) < 0)
        return -1;
    errno = 0;
    v = strtoull(buf, &end, 10);
    /* strtoull reports junk through endptr, not errno, and wraps a leading '-'
     * (only whitespace, a sign and digits precede end). Assign on success only. */
    if (errno || end == buf || memchr(buf, '-', (size_t)(end - buf)))
        return -1;
    *out = v;
    return 0;
}

int read_file_str(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");

    if (!f)
        return -1;
    if (!fgets(buf, (int)len, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* hwmonN indices are not stable across boots - match on the "name" attribute. */
int hwmon_find(const char *want, char *out, size_t len)
{
    char path[PATH_MAXLEN], name[64];
    struct dirent *e;
    DIR *d;
    int rc = -1;

    spath(path, sizeof path, HWMON_DIR);
    d = opendir(path);
    if (!d)
        return -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0)
            continue;
        spath(path, sizeof path, HWMON_DIR "/%.32s/name", e->d_name);
        if (read_file_str(path, name, sizeof name) < 0)
            continue;
        if (strcmp(name, want) == 0) {
            spath(out, len, HWMON_DIR "/%.32s", e->d_name);
            rc = 0;
            break;
        }
    }
    closedir(d);
    return rc;
}

/* Read every zone's type once: resolution then costs 64 reads no matter how many
 * zones are selected. Matching per name against the tree instead cost ~1500 opens
 * for 48 zones - and before the fd cache, that was per *sample*. */
size_t zone_table(struct zone_tab *t, size_t max)
{
    char path[PATH_MAXLEN];
    size_t n = 0;

    for (int i = 0; i < ZONES_MAX && n < max; i++) {
        spath(path, sizeof path, THERMAL_DIR "/thermal_zone%d/type", i);
        if (read_file_str(path, t[n].type, sizeof t[0].type) < 0)
            continue;
        t[n].idx = i;
        n++;
    }
    return n;
}

/* Accepts the bare name ("cpuss-0-0") as well as the full type, because the bare
 * form is what we *print* - a user copying a label out of the output must not be
 * told that a plainly existing zone does not exist. */
int zone_lookup(const struct zone_tab *t, size_t n, const char *want)
{
    size_t wl = strlen(want);

    for (size_t i = 0; i < n; i++) {
        if (strcmp(t[i].type, want) == 0)
            return t[i].idx;
        if (strncmp(t[i].type, want, wl) == 0
            && strcmp(t[i].type + wl, "-thermal") == 0)
            return t[i].idx;
    }
    return -1;
}
