<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open work on `qcstats`, and one standing fact at the end that a reader of this file would
otherwise ask about. **No defects are open.** What was fixed, and why, is in
`CHANGELOG.md`; the JSON contract is in `docs/json-schema-v1.md`.

Items marked **[measured]** were checked against the board; anything else is read from the
source and not yet confirmed on hardware.

---

## Gaps

- **A second captured fixture.** `make test` runs 241 checks against one real tree, and
  `tests/t_units.c` builds its own synthetic trees for the shapes nobody can capture. What
  is missing is another real board: one capture proves the code handles that capture.
  Blocked on hardware.

- **Behaviour on anything that is not a Ventuno Q.** Unverified by design — there is no
  board detection, and guarding is capability-based: a missing path reads as absent and is
  emitted as `null`. What a different board's *present but differently-shaped* paths would
  produce is untested. Potential future work if Arduino releases a sibling or successor.

## Opportunities

- **Investigate `libqcperf`, for what it could add beyond `--npu`.** It reads the DSP the
  same way this does — FastRPC to the Hexagon — which is how either of them sees
  utilisation that no sysfs path exposes.

  **The four NPU figures are already here**, so they are not the opportunity: `src/npu.c`
  queries the DSP's `sysmonquery` service and emits `q6_pct`, `q6_khz`, `hvx_pct` and
  `hmx_pct` — the Q6 core, its clock, and the vector and matrix units. The reply it reads
  is 14 floats and one uint of which **only four carry meaning**; what the remaining nine
  are is unknown, and finding that out is the concrete first step.

  What would genuinely be additive: a DCVS frequency *limit* and an effective-utilisation
  figure for the CPU. Neither is derivable from what is read today — `scaling_cur_freq`
  and `/proc/stat` give the frequency in use and the load, and the LMh counters give
  throttle *events*, never the limit in force. Claimed but unverified here: that its power
  and thermal backends are Windows-on-ARM only, which would make it additive to the INA232
  and the thermal zones rather than a replacement for them.

  Two conditions decide whether any of it is reachable. **Licensing** comes first — the
  source carries `Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries`, and
  whether that permits a port at all is the question to settle before writing code. And
  the **no-dependency promise**: `src/npu.c` is shaped the way it is because the FastRPC
  ABI was declared from the published spec and `libcdsprpc.so.1` is resolved with
  `dlopen`, so this program still links nothing but libc and degrades to `null` where the
  library is absent. Anything taken from `libqcperf` has to arrive the same way — as a
  declared wire format, not as vendored source.

## Known, and not a gap

- **The gate does not run the hardware checks, and cannot.** `tests/board-check.sh` is 45
  checks that need tracefs, a devfreq governor that is polling, and a GPU that can be
  powered — none of which a runner has. It is a release step run by hand, and nothing
  reminds anyone to run it. Last green **2026-10-25** (45 ok, 12s, staged `aarch64`
  build). **[measured]**
