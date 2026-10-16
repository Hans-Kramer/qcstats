<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open items for `qcstats`. Items marked **[measured]** were checked against the board;
anything else is read from the source and not yet confirmed on hardware.

---

## Defects

- **`src_str()` reports success on a truncated read.** It does a single `pread` of
  `len - 1` and returns 0 for any positive count, so a file larger than the buffer is
  silently cut and the caller is told it read the whole thing. `throttle.c` would then
  report **0** events for every policy whose line fell past the cut, and 0 is a meaningful
  reading here — "nothing throttled" — so those policies get a zero nobody measured.
  `cpu.c` degrades correctly instead: a missing CPU line leaves `present` unset, which is
  absence.

  **Not reachable on a Ventuno Q.** Measured: `/proc/interrupts` is 21,602 bytes against a
  65,535-byte read (32%), `/proc/stat` 1,454 against 8,191, `/proc/meminfo` 1,504 against
  8,191. Both of the first two scale with CPU count, so the condition is a machine with
  materially more cores — and running on hardware this is not built for is already an open
  item below. **[measured]**

  `src_str()` itself must **not** gain a fail-on-full-buffer contract: `src_str_ok()` reads
  7 bytes of an arbitrarily long attribute on purpose, to test readability, so that would
  break every probe in the project. Truncation is a property of the caller's intent, not of
  the read. The fix is a sibling for the callers that need the whole file — `throttle.c` and
  `mem.c` — reporting absence rather than a partial count. This belongs with the read-back
  item in `qcctl`: both are a primitive returning success for a result the caller cannot
  trust.

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
