<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open work on `qcstats`, and nothing else. **No defects are open.** What was fixed, and why,
is in `CHANGELOG.md`; the JSON contract is in `docs/json-schema-v1.md`.

Items marked **[measured]** were checked against the board; anything else is read from the
source and not yet confirmed on hardware.

---

## Gaps

- **The gate does not run the hardware checks, and cannot.** `tests/board-check.sh` is 45
  checks that need tracefs, a devfreq governor that is polling, and a GPU that can be
  powered. Nothing reminds anyone to run it — it is a release step written down, not an
  enforced one. Last green **2026-10-25** (45 ok, 12s, staged `aarch64` build).
  **[measured]**

- **A second captured fixture.** `make test` runs 241 checks against one real tree, and
  `tests/t_units.c` builds its own synthetic trees for the shapes nobody can capture. What
  is missing is another real board: one capture proves the code handles that capture.
  Blocked on hardware.

- **Behaviour on anything that is not a Ventuno Q.** Unverified by design — there is no
  board detection, and guarding is capability-based: a missing path reads as absent and is
  emitted as `null`. What a different board's *present but differently-shaped* paths would
  produce is untested.
