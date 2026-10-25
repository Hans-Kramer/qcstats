<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open work on `qcstats`, and nothing else. **No defects are open.** What was fixed, and why,
is in `CHANGELOG.md`; the JSON contract is in `docs/json-schema-v1.md`.

Items marked **[measured]** were checked against the board; anything else is read from the
source and not yet confirmed on hardware.

---

## Gaps

- **`--gpu-busy`'s tracepoint path when it succeeds.** It needs root and a real adreno
  tracepoint, so neither CI nor a container can reach it. The unprivileged refusal is
  asserted, and the branch is chosen by the same `access()` probe the code uses, so it
  cannot false-fail where tracefs happens to be readable. Closing it means a small
  hardware-only script, the way `qcctl` has one — which would also cover the teardown after
  `SIGKILL`, the one signal that leaves the tracepoint enabled and the trace instance
  behind. The three handled signals and the normal restore are covered. **[measured]**

- **A second captured fixture.** `make test` runs 226 checks against one real tree, and
  `tests/t_units.c` builds its own synthetic trees for the shapes nobody can capture. What
  is missing is another real board: one capture proves the code handles that capture.
  Blocked on hardware.

- **Behaviour on anything that is not a Ventuno Q.** Unverified by design — there is no
  board detection, and guarding is capability-based: a missing path reads as absent and is
  emitted as `null`. What a different board's *present but differently-shaped* paths would
  produce is untested.
