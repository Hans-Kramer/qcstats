// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// WHICH IMAGE AND KERNEL PRODUCED A SAMPLE.

#include "envid.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

/* /etc/buildinfo is a KEY=value file, parsed BY KEY and not by position: a line added ahead
 * of BUILD_ID in a future image must not change the answer.
 *
 * Not read_file_str(): that is deliberately a single-line reader (one fgets) for sysfs
 * attributes, so a BUILD_ID on line 2 would be invisible. That failure is easy to ship with
 * a comment claiming key-parsing, which is why this loop is explicit about it. */
static int read_build_id(char *out, size_t len)
{
    char jp[PATH_MAXLEN], line[512];
    FILE *f;

    /* spath() first. read_file_str() and fopen() both take an ALREADY-JOINED path, so a bare
     * literal reads the HOST file and silently ignores QCSTATS_SYSROOT. Its only symptom is
     * that a fixture tree reports the host's values, which look entirely plausible. */
    spath(jp, sizeof jp, "/etc/buildinfo");
    f = fopen(jp, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof line, f)) {
        char *p = line;

        line[strcspn(line, "\r\n")] = '\0';
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "BUILD_ID=", 9))
            continue;
        snprintf(out, len, "%s", p + 9);
        fclose(f);
        return out[0] ? 0 : -1;
    }
    fclose(f);
    return -1;
}

void envid_read(struct envid *e)
{
    char jp[PATH_MAXLEN];

    memset(e, 0, sizeof *e);

    e->have_build_id = (read_build_id(e->build_id, sizeof e->build_id) == 0);

    spath(jp, sizeof jp, "/proc/sys/kernel/osrelease");
    e->have_kernel = (read_file_str(jp, e->kernel, sizeof e->kernel) == 0 && e->kernel[0]);

    /* Absent must stay EMPTY as well as flagged absent. A placeholder string here would let
     * a consumer comparing two captures read a change that was never observed - schema
     * rule 1, in the direction people forget. */
    if (!e->have_build_id)
        e->build_id[0] = '\0';
    if (!e->have_kernel)
        e->kernel[0] = '\0';
}
