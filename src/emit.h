// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer

#ifndef QCSTATS_EMIT_H
#define QCSTATS_EMIT_H

#include "qcstats.h"
#include "envid.h"

#include <stdio.h>

/* Redirect sample output. NULL or unset means stdout. */
void emit_to(FILE *f);

/* The environment is run-scoped, not sample-scoped: read once, borrowed for the run.
 * Pass NULL (or never call this) and every emitter behaves as if both fields were absent. */
void emit_set_env(const struct envid *e);

/* Once-per-run provenance, for the formats where a non-sample line is safe. There is no
 * emit_meta_csv(): see emit.c for why CSV carries it as columns instead. */
void emit_meta_json(void);
void emit_meta_human(void);

void emit_human(const struct sample *s);
void emit_json(const struct sample *s);
void emit_csv_header(const struct sample *s);
void emit_csv(const struct sample *s);

#endif /* QCSTATS_EMIT_H */
