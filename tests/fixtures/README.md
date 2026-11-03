<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
<!-- SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite -->
# Captured fixtures

Each directory here is a photograph of one board's read surface: the sysfs and procfs files
this sampler opens, with the values they held at the moment of capture. `QCSTATS_SYSROOT`
points at one and every read is relocated onto it, so the whole sampler runs on a machine
that is not a Ventuno Q and has no board attached.

Captured by [`../capture-fixture.sh`](../capture-fixture.sh), which is also the authoritative
list of what this project reads — a path missing from that script is a path no fixture can
satisfy.

| fixture | board image | kernel | files | content |
|---|---|---|---|---|
| `monza-20260710-235-6.8.0-1084` | `BUILD_ID=20260710-235` | `6.8.0-1084-qcom` | 1242 | 37 KB |

Each tree carries a `.capture` file recording when it was taken, from which host, kernel,
architecture, build id and model — so a fixture that has gone stale can be recognised as one
rather than trusted.

## What a fixture proves, and what it does not

It proves this code turns **that** shape into **this** output, which is most of what the
samplers do. It cannot prove the shape is still what a kernel presents: a renamed attribute
or a changed unit would be invisible here and obvious on hardware. Anything about what the
board physically does — fan speeds, how temperature responds — stays a hardware check.

One consequence is worth knowing when reading test expectations: a captured `/proc/stat`
never advances, so no CPU load can be measured from it. The sampler reports `null` for those
percentages rather than `0`, because a zero nobody measured is indistinguishable from a
genuinely idle machine. That rule was added when this fixture's first run exposed the
opposite behaviour.

## Licensing

The captured attribute values are machine output — numbers a kernel printed — and are not
authored content. This file is CC-BY-4.0 like the project's other documentation. The gate's
SPDX assertion skips the captured trees for that reason and points here instead.

## Why this is tracked and not ignored

The test for `.gitignore` is whether a clone can regenerate it. A build artifact can; this
cannot — it takes **this** board. Ignored, `make test` on a fresh clone would have nothing to
read, the fixture-driven checks (most of the suite) could not run at all, and CI's "no test
may report SKIPPED" step would be the only thing left standing. The capture is a test vector
with its provenance in its name, not output.

The cost is paid in **file count**, not in size: 1152 of this repo's 1194 tracked files are
fixture, while `.git` is under a megabyte and `git status` takes ten milliseconds. A single
`tar.gz` would be 28 KB against 35 KB of loose text — no saving worth having — and it would
cost the thing that makes a loose tree worth keeping: when the board is re-captured, the diff
*shows what changed underneath this project*. A kernel that renumbers a zone or moves an
attribute's mode is exactly what a reader needs to see, and an archive hides it.

Nothing here identifies the machine it came from: `etc/` holds only the published `BUILD_ID`,
and there is no hostname, machine-id, cmdline or MAC in the tree. `capture-fixture.sh` takes
only the paths this project reads, which is why that script is the authoritative list.

## Copying one of these trees

Use `tar`, not `scp -r` or `cp` without `-a`. The tree contains symlinks that carry meaning —
a zone's `cdevN`, an interface's `device` — and `scp -r` dereferences them, turning each into
a copied file or a recursively copied directory. That silently produces a tree that still
passes a file count and no longer tests what it was taken for.

    ssh board 'tar -C /tmp/fx -cf - .' | tar -C tests/fixtures/<name> -xf -

## Indices in these trees are per-boot, and names are not

Between the first capture of this fixture and its re-capture, **18 of 48 thermal zone
indices changed identity** — `thermal_zone13` was `cpu-1-0-1-thermal` and became
`pmm8650au_1_sdram` — and `hwmon4`/`hwmon5`/`hwmon6` shuffled, moving `ina232` from hwmon6 to
hwmon4. The shuffle is confined to the range where PMIC zones interleave with SoC ones, which
is a driver probe-order race and not something a caller can predict.

Nothing here reads an index to decide what a sensor *is*: zones are matched on `type` and
hwmons on `name`, which is why a re-capture changed no expectation. Tests that name an index
do so only to read a value back out of this frozen tree, never to assert what lives there on a
live board.
