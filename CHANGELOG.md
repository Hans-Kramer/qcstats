<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Changelog

All notable changes to `qcstats` will be summarized and documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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
