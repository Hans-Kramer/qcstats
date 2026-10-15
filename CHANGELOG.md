<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Changelog

All notable changes to `qcstats` will be summarized and documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

- **A GitHub Actions gate** (`.github/workflows/gate.yml`) on every push to `main` and every
  pull request. It runs `make check` and the cross build, and asserts three things `make
  check` does not: that every toolchain is actually present, because `make check` skips a
  compiler it cannot find rather than failing; that the cross build really produced an
  aarch64 binary, by reading `e_machine` at ELF offset 18; and that every tracked file
  carries an SPDX identifier.

### Removed

- **`NPU_STALE_LIMIT` and its test from `npu.c`.** The test could not fire: `st.last_ms` is
  stamped on every query *attempt*, and one happens as soon as the rate-limit period elapses,
  so the age checked at publish time never reached the three periods the constant allowed.
  Implementing it would have meant holding a reading for up to three periods after a query
  had already failed — a number nobody measured, when the branch beside it clears the value
  for exactly that reason. The hold is bounded by the rate limit alone.

### Fixed

- **`fan.rpm_valid` vouched for a fan that was commanded off but still spinning.** The
  distrust window was `0 < pwm < 51`, which covers state 1's tachometer misread but excludes
  `pwm == 0` — so for about a second after the fan is switched off, a coasting reading was
  flagged trustworthy. Measured on a Ventuno Q: `duty_pct: 0` beside `rpm: 5160` and
  `rpm_valid: true`, a pairing no duty can describe. It is now `false` while a fan commanded
  off is still turning, and also `false` when either reading is absent, since a flag
  vouching for a `null` rpm says nothing.

  Settled readings are unchanged: state 0 off is still `rpm: 0` with `rpm_valid: true`,
  state 1 is still `false`, and states 2 and up still `true`. Settling after any other
  change is left to the consumer, because judging it needs an expected-rpm reference this
  tool deliberately does not hold.

- **`--help` printed to stderr and did not list itself.** `-h` and `--help` were accepted
  but absent from the usage text, and `usage()` wrote to stderr whether it was answering a
  request for help or reporting a bad argument — so `qcstats --help | less` showed nothing.
  Help now goes to stdout and exits 0, a usage error still goes to stderr and exits 2, and
  both flags are listed. `docs/USAGE.md` had documented the old behaviour rather than
  calling it a defect.

- **`src_u64()` turned unparseable input into a measurement.** It tested only `errno`, but
  `strtoull` reports unparseable input through its end pointer — so a non-numeric attribute
  returned **0 with a success status**, and a leading `-` returned a silently wrapped huge
  value. That is a zero nobody measured, reaching the output through the one primitive every
  sampler reads through. It now rejects input where nothing was parsed, and any sign in the
  span the parse consumed.

  `*out` is also written on success only. `cpu.c` seeds `uint64_t online = 1` for an absent
  `online` attribute and ignores the return; a junk read used to overwrite that with 0 and
  report a live CPU as offline.

  A real zero is still a measurement: `pwm1` of `0` reports `duty_pct: 0`, because a fan
  being off is a reading and not an absence.

- **`thermal.c` left `mc` uninitialised.** It was safe only because the assignment after it
  is guarded by the same flag, and with `src_u64()` no longer writing its output on failure
  that guard became the only thing between a failed read and an indeterminate temperature.
