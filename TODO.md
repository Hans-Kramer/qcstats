<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# TODO

Open items for `qcstats`. Items marked **[measured]** were checked against the board;
anything else is read from the source and not yet confirmed on hardware.

---

## Defects

- **`npu.c` — `NPU_STALE_LIMIT` is unreachable dead code.** `st.last_ms` is reassigned on
  every query *attempt*, so the staleness test can never exceed one period. A failed query
  already drops the value via `st.have = 0`, which is the behaviour that actually ships.
  `docs/json-schema-v1.md` has been corrected to describe that; the constant and its test
  should now either be removed or made to work. **[measured]**

- **`fan.c` — `rpm_valid` stays `true` during fan spin-down.** The distrust window is
  `0 < pwm < 51`, which excludes `pwm == 0`, so for a second or two after the fan is switched
  off a coasting reading is reported as trustworthy. The window is otherwise exactly right:
  state 2 *is* pwm 51, and state 1 (pwm 26) is the only untrustworthy state. **[measured]**

- **`io.c` — `src_u64()` does not check the `strtoull` endptr.** It tests only `errno`, so a
  non-numeric attribute yields `0` with a success return — precisely the "a zero you did not
  measure" failure the whole data model exists to prevent. Every sampler reads through this
  function. The sibling project hardened its equivalent primitive; this one hardened the
  `--count` caller instead, leaving the primitive exposed.

- **`-h` / `--help` works but is absent from the `usage()` text.** Documented in
  `docs/USAGE.md` only.

---

## Gaps

- **No automated behavioural gate.** `make check` is a compilation gate (gcc and clang,
  `-Wall -Wextra -Werror` at `-O2`) and passes. `QCSTATS_SYSROOT` already relocates every read
  onto a captured tree, so a suite would need no board and no root — the hook is built and
  unused.

- **No CI.** `.github/` is empty, and git does not track empty directories, so a fresh clone
  has none at all. Nothing runs `make check` automatically.

- **No release published.** `CHANGELOG.md` stays empty until there is one. The binary version
  and the JSON schema version move independently; `-V` reports both.

- **Paths nothing verifies:** the `--gpu-busy` tracepoint teardown after `SIGKILL` (the three
  handled signals are covered, and the normal restore leaves the tracepoint disabled and the
  trace instance removed **[measured]**), and behaviour on any board that is not a Ventuno Q.

---

## Settled

- **The name stays `qcstats`.** It pairs with `qcctl` — same prefix, observe/act split — which
  carries the project's central architectural claim. `qc` is also the trademark-safest prefix
  available, where a board or vendor name would weaken the disclaimer.
- **No `CONTRIBUTING.md`.** The README's License section already states the policy:
  inbound=outbound, no CLA, you keep your copyright.
