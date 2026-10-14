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

- **`-h` / `--help` works but is absent from the `usage()` text.** Documented in
  `docs/USAGE.md` only.

- **`gputrace.c` and `throttle.c` parse with `atoi`**, which cannot report failure at all —
  junk reads as `0`. Lower risk than `src_u64()` was: both read a trace buffer whose format
  `qcstats` itself enabled, rather than an arbitrary sysfs attribute. `throttle.c`'s
  unguarded `strtoull` sum is in the same class.

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
