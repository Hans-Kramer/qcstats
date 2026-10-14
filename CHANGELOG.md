<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Changelog

All notable changes to `qcstats` will be summarized and documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Fixed

- **`src_u64()` fabricated a measurement out of unparseable input.** It tested only `errno`,
  and `strtoull` reports unparseable input through its end pointer rather than through
  `errno` — so a non-numeric attribute returned **0 with a success status**, and a leading
  `-` returned a silently wrapped huge positive. That is "a zero you did not measure", the
  exact failure the absence contract in [`docs/json-schema-v1.md`](docs/json-schema-v1.md)
  exists to forbid, arriving through the one primitive every sampler reads through — ten call
  sites across `cpu.c`, `fan.c`, `gpu.c`, `power.c` and `thermal.c`.

  It now rejects input where nothing was parsed, and any `-` in the span the parse consumed.
  Only whitespace, a sign and digits can precede the end pointer, so a `-` found there is
  always the sign — which also catches the leading-whitespace form that testing a single
  first character would miss. `*out` is assigned on success only, so a caller's seeded
  default survives a failed read; `cpu.c`'s `uint64_t online = 1` for an absent `online`
  attribute was previously clobbered to 0 by a junk read, silently reporting a live CPU as
  offline.

  Demonstrated under `QCSTATS_SYSROOT` with a tree holding `unknown`, `-5`, `N/A` and
  `  -12`. Before:

  ```json
  "fan": {"duty_pct": 0, "rpm": 18446744073709551611, "rpm_valid": true}
  "power": {"mw": 0, "mv": 20000, "ma": 18446744073709551604}
  ```

  After, with the one real value still read:

  ```json
  "fan": {"duty_pct": null, "rpm": null, "rpm_valid": false}
  "power": {"mw": null, "mv": 20000, "ma": null}
  ```

  Note `rpm_valid: true` in the first: the schema's own trust flag was vouching for a
  wrapped negative. The consumer is affected too — `qcctl` parses this stream with `strstr`
  and treats the numbers as authoritative, so an invented `0` for a zone temperature feeds
  its fan curve, which would select the lowest state for a board it believed was at 0 °C.

  No change to legitimate reads, verified against the same mechanism: a trailing newline
  still parses, `2**64 - 1` still parses, an empty file is still absent, and a **real** zero
  is still a measurement — `pwm1` of `0` reports `duty_pct: 0`, because the fan being off is
  a reading and not an absence.

- **`thermal.c` left `mc` uninitialised**, which was safe only because the assignment that
  follows is guarded by the same flag. Now that `src_u64()` leaves the output untouched on
  failure, that guard was the only thing standing between a failed read and an indeterminate
  temperature. Initialised.

### Added

- **A GitHub Actions gate** (`.github/workflows/gate.yml`), on every push to `main`, every
  pull request, and on demand. It installs gcc, clang and `gcc-aarch64-linux-gnu`, then
  runs `make check` and the cross build.

  What it asserts that `make check` alone does not:

  - **Every toolchain is present.** `make check` *skips* a compiler it cannot find, with a
    notice rather than a failure. That is right on a developer's machine and wrong on a
    runner, where a missing clang would report a green gate having checked half of it.
  - **The cross build really produced an aarch64 binary**, by reading `e_machine` at ELF
    offset 18 and requiring `0xb7`. `char` is unsigned on aarch64 and signed on x86, so the
    cross build belongs in the gate rather than in a release step. `od`, not `file(1)`,
    matching the Makefile: `od` is always present.
  - **Every tracked file carries an SPDX identifier**, `LICENSE` and `LICENSES/*` excepted.
    What a REUSE lint would check, done inline to keep the zero-dependency promise.
