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

- **No automated behavioural gate.** `make check` is a compilation gate (gcc and clang,
  `-Wall -Wextra -Werror` at `-O2`) and passes; CI runs it, plus the cross build, on every
  push. `QCSTATS_SYSROOT` already relocates every read onto a captured tree, so a suite would
  need no board and no root — the hook is built and unused.

- **No release published.** The binary version and the JSON schema version move
  independently; `-V` reports both.

- **Paths nothing verifies:** the `--gpu-busy` tracepoint teardown after `SIGKILL` (the three
  handled signals are covered, and the normal restore leaves the tracepoint disabled and the
  trace instance removed **[measured]**), and behaviour on any board that is not a Ventuno Q.

---

## Settled

- **No `CONTRIBUTING.md`.** The README's License section states the policy: inbound=outbound,
  no CLA, you keep your copyright.
