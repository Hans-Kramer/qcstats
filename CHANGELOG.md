<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Changelog

All notable changes to `qcstats` will be summarized and documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

- **The arithmetic, and the two output formats nothing had ever executed.** The suite was
  thorough about whether a field was PRESENT and almost silent about whether it was RIGHT:
  dividing the GPU's hertz by a thousand instead of a million, reporting available memory as
  used, computing fan duty against 256 instead of 255, or summing only the first of eight
  per-CPU interrupt columns all passed it. Each of those is now compared against the
  fixture's own numbers, and each mutation reddens.

  `tests/t_units.c` is new, for the three things nothing linked against. `cpu_pct()` is the
  starkest: `cpu.h` says it is declared non-static *"so it should be reachable by linking
  against it, not by #including the translation unit"* - a header documenting testability for
  a function no test called - and halving the formula passed. It now pins the percentage, the
  half-up rounding, a zero elapsed interval, and a counter that went backwards. `envid.c` had
  no test at all, including the by-key `BUILD_ID` lookup its own comment calls load-bearing
  (*"a line added ahead of BUILD_ID in a future image must not change the answer"*); an
  off-by-one in that key made the whole field vanish silently. And the throttle summation
  needed numbers: the captured board was idle, so every count in the fixture is zero and a
  broken sum is still zero. It gets the first synthetic tree in this repo, with eight
  deliberately different columns, so taking one column or stopping early cannot come out
  right by luck.

  `tests/t_emit.c` now covers CSV and the default human format, which between them had three
  shallow shell assertions. A dropped header column - which misaligns every field after it
  for whatever reads the file - and `csv_field()`'s RFC 4180 quote doubling, whose own comment
  explains that zone labels come from the kernel or the command line and may contain a comma,
  both passed. The JSON *meta* line, which is what `qcctl` reads to know which kernel and
  image produced a stream, had never been looked at past its first fourteen bytes.

  Also closed: `src_open_abs()`'s path-truncation guard, which exists because *"a truncated
  path would silently name a different file"*. Testing that needed the truncated path to BE a
  file - a decoy at exactly the cut, built out of nested directories because one filename
  cannot exceed 255 bytes - or a missing guard merely opens something absent and the check
  passes for the wrong reason. It did, on the first attempt. And `--zones`' two validation
  branches, each with its own message, neither previously reached.

- **A behavioural suite, and `make test` to run it.** `make check` compiles the tree; nothing
  executed it, and every behavioural claim in these docs was measured by hand on one board.
  The harness is `tests/t.h` — counting macros and no framework, so the zero-dependency
  promise holds — and each `tests/t_*.c` is a `main()` linked against every object but
  `main.o`, built into `tests/bin/`.

  - `t_io.c` covers the primitive every sampler reads through: a junk attribute is refused
    rather than read as 0 and `*out` is left untouched (the `cpu.c` `online = 1` case), a
    leading `-` is refused rather than wrapped, an empty read is a failure, `src_str()` still
    truncates deliberately because `src_str_ok()` probes 7 bytes of an arbitrarily long
    attribute on purpose, and `src_str_all()` refuses that same read with `-2`.
  - `t_emit.c` covers the output contract: absence is `null` and a measured zero stays `0`,
    the exact `fan`, `cpu`, `thermal_c` and `v` spellings `qcctl` parses with `strstr`, the
    version first in the line, and one newline per sample.
  - `t_sample.c` runs **every sampler against a captured board**, with no board present, and
    asserts the JSON a consumer would see.

  The capture is `tests/capture-fixture.sh`, run on a board; the committed tree is 1131 files
  and 21 symlinks holding 35 KB of sysfs and procfs text — `du` reports far more, because 167
  directory inodes dwarf files averaging 32 bytes — named for the image and kernel it came
  from and carrying
  a `.capture` file of its own provenance. That script is also the authoritative list of what
  this project reads. `QCSTATS_SYSROOT` made all of it possible and had never been used.

  Three properties are deliberate. A test file that checks **nothing** returns non-zero,
  because a suite that cannot fail is not a gate. `make check` compiles the test sources under
  both compilers as well, so test code holds the same warning bar as the rest of the tree. And
  every test file has been shown to fail against a real regression rather than an edited
  expectation — reverting the `src_u64()` hardening reddens three checks, breaking Schema
  rule 1 reddens two, renaming one field reddens the contract check.

  A second CI job runs the whole suite on `ubuntu-24.04-arm`, which is the board's
  architecture. It was compiled for aarch64 on every push and executed on none; `char` is
  unsigned there and signed on x86. The suite also passes when cross-built and run on the
  board itself.

  **The command line is in it too, as `tests/sh/t_cli.sh` — 54 checks.** The C tests link
  every object except `main.o`, so the argv layer sat structurally outside them: of 12
  options, 3 were ever run, and `--csv` and `--plain` had no test at all. A `--csv` that
  emitted nothing would have passed the whole suite. It now asserts all three output formats
  and that the last one given wins, every exit-2 refusal (a value that is not a number, a
  count of 0, `--zones` with `--all-zones`, an unwritable `--logfile`, an unknown argument
  reaching stderr with the usage), `--count`'s line arithmetic against the meta line,
  `--logfile` taking the stream off stdout entirely, zone selection including a name nothing
  matches — which is a filter returning nothing and not an error — and that `--gpu-busy`
  keeps sampling and exits 0 whether or not tracefs can be read. That last one decides which
  branch to assert with the same `access(R_OK|X_OK)` probe the code uses: the refusal message
  appears only when tracefs is unreadable, so asserting it unconditionally would have failed
  a correct binary anywhere that mount is reachable - as root in a container, or on a distro
  that grants a group access to it. The shell harness is `tests/sh/t.sh`, a copy of
  `qcctl`'s, noted as one in the file: the repos share no build, so a fix here does not
  propagate. `make test` now builds the binary and runs both halves.

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

- **A CPU percentage was reported for a delta nobody measured.** `cpu_pct()` returns 0 when
  two `/proc/stat` readings are identical, and 0% is indistinguishable from a genuinely idle
  machine — the one output this sampler must never produce, and what `qcstats.h` says about
  load in particular: *"Without them, 0% is indistinguishable from a genuinely idle machine,
  so it must not be reported as a value."*

  Unreachable on a live board, where something always advances between two readings. The
  normal case against a **captured tree**, where nothing does — so it would have quietly
  mis-taught every fixture-based test from here on. `load_ok` is false now when no counter
  moved, which makes those percentages `null`; frequencies come from cpufreq and are still
  reported. Found by the first fixture run, minutes after there was a fixture to run.

  Verified on the board that live sampling is unchanged: real percentages, and a genuinely
  idle core still reports `0`, which is a measurement.

- **A truncated read was reported as a whole file.** `src_str()` does a single `pread` of
  `len - 1` and returns success for any positive count, so content larger than the buffer
  was silently cut and the caller told it had read all of it. `throttle.c` would then report
  **0** events for every policy whose line fell past the cut — and 0 is a meaningful reading
  here, "nothing throttled" — so those policies got a zero nobody measured.

  The two callers that parse a whole file now use `src_str_all()`, which refuses a read that
  filled the buffer, and report the object absent instead. `src_str()` is deliberately
  unchanged: `src_str_ok()` reads 7 bytes of an arbitrarily long attribute on purpose to
  test readability, so truncation is a property of the caller's intent rather than of the
  read, and a fail-on-full-buffer contract there would break every probe in the project.

  It distinguishes the two failures — `-1` unreadable, `-2` filled the buffer, both testing
  as `< 0` — because a missing attribute is routine and stays silent as before, while a
  buffer too small for the machine is a standing fault worth saying once:

  ```
  qcstats: /proc/meminfo exceeds the 8192-byte buffer; memory reported absent
  ```

  Not reachable on a Ventuno Q — `/proc/interrupts` is 21,602 bytes against 65,535 and
  `/proc/meminfo` 1,504 against 8,191 — but both scale with CPU count, and running on
  hardware this was not built for is an open item in [`TODO.md`](TODO.md).

- **`atoi` turned unparseable fields into measurements.** `throttle.c` took the IRQ number
  out of a `dcvsh-irq-<N>` name with `atoi`, which returns 0 for anything it cannot parse —
  and 0 is cpu0's own IRQ. Since the loop assigns rather than accumulates, a name it failed
  on was charged to policy0 and *overwrote* that policy's real count: against a crafted
  `/proc/interrupts`, a malformed `dcvsh-irq-x` line replaced policy0's 1000 events with its
  own 10. `gputrace.c` read `load=` the same way, so an unparseable field became a confident
  0 % GPU busy instead of an absent one. Both now require at least one digit and skip what
  they cannot parse; `load` is range-checked too, since a percentage outside 0–100 is a
  misparse rather than a reading.

- **`gputrace.c` could parse half a trace line as a whole one.** It tokenised a 64 KB read
  without allowing for the last line being cut in half by the read boundary. A full buffer
  now drops its trailing partial line; the next read carries the remainder, which no longer
  matches the device name and is skipped.

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
