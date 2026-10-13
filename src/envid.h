// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// The environment a sample was taken in: which image, which kernel. See envid.c for why
// this exists and why each source was chosen.
//
// ABSENT IS NOT A VALUE. Each field carries its own presence flag, and they are independent
// on purpose: a board can plausibly have a readable kernel and no /etc/buildinfo at all.
// An unreadable source emits null - never 0, never "unknown", never "". Schema rule 1.

#ifndef QCSTATS_ENVID_H
#define QCSTATS_ENVID_H

struct envid {
    char build_id[64];      /* BUILD_ID from /etc/buildinfo, e.g. "20260710-235" */
    char kernel[80];        /* /proc/sys/kernel/osrelease, e.g. "6.8.0-1084-qcom" */
    int  have_build_id;
    int  have_kernel;
};

/* Never fails: an unreadable source leaves that field absent and empty. Reads two small
 * files, so it is called ONCE per run, not per sample. */
void envid_read(struct envid *e);

#endif /* QCSTATS_ENVID_H */
