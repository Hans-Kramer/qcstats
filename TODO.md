<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open items for `qcstats`. Items marked **[measured]** were checked against the board;
anything else is read from the source and not yet confirmed on hardware.

---

## Defects

None open.

---

## Gaps

- **The suite proves one board shape.** `make test` runs 226 checks on every push and on
  `ubuntu-24.04-arm`, covering `io.c`, all three emitters, every sampler against a captured
  tree, the arithmetic on its own, and the command line through `argv`.

  The command line is covered as of 2026-10-19: `tests/sh/t_cli.sh` drives the built binary
  through `argv` — all three output formats and which one wins when two are given, every
  exit-2 refusal, `--count`'s line arithmetic, `--logfile`, and zone selection including a
  name nothing matches.

  What is left: `--gpu-busy`'s tracepoint path when it *succeeds*, which needs root — the
  unprivileged refusal is asserted, and the branch is chosen by the same `access()` probe the
  code uses so it cannot false-fail where tracefs is readable. And a **second fixture**, because one
  captured tree proves the code handles that shape and says nothing about another. Synthetic
  trees for shapes this board cannot show are the cheaper half of that, because they need no
  board to write.

- **No release published.** The binary version and the JSON schema version move
  independently; `-V` reports both.

- **Paths nothing verifies:** the `--gpu-busy` tracepoint teardown after `SIGKILL` (the three
  handled signals are covered, and the normal restore leaves the tracepoint disabled and the
  trace instance removed **[measured]**), and behaviour on any board that is not a Ventuno Q.

---

## Settled

- **No `CONTRIBUTING.md`.** The README's License section states the policy: inbound=outbound,
  no CLA, you keep your copyright.
