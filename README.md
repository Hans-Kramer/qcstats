<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# qcstats

> [!CAUTION]
> **Pre-release: `v0.1.0-pre`. No release has been published.**
>
> `qcstats` and its companion [`qcctl`](https://github.com/Hans-Kramer/qcctl)
> target **one board** — the Arduino Ventuno Q (QCS8275). Nothing else is
> tested, and the sampler reads paths that only exist there.
>
> The gate in this tree compiles under `gcc`, `clang` and the `aarch64` cross
> build at `-Wall -Wextra -Werror`, and runs **226 behavioural checks** — `io.c`,
> the emitters in all three output formats, every sampler against a captured
> board, the arithmetic on its own, and the command line itself — on x86-64 and
> on `aarch64`. A fixture is a photograph of one board's sysfs: it proves what the
> code does with that shape, never that the shape is still what a kernel
> presents. **Every figure in these docs was measured on hardware by hand.**
>
> `qcstats` reads; it writes nothing to the system except the opt-in
> `--gpu-busy` tracepoint, which it removes on exit. Interfaces may change
> without notice before `0.1.0` — the JSON schema version is the only contract
> intended to be stable, and it is `1`. Known gaps are in [`TODO.md`](TODO.md).

A telemetry sampler for the **Arduino Ventuno Q**
a.k.a. `monza` (Qualcomm QCS8275 / Dragonwing IQ-8275).

> **Legal Disclaimer**
> 
> This project is an independent personal project. It is in no way affiliated with, 
> sponsored by, or endorsed by Arduino&reg; or Qualcomm&reg;.
>
> "Arduino" and "VENTUNO" are registered trademarks of Arduino. "Qualcomm" and 
> "Dragonwing" are trademarks of Qualcomm Incorporated.
> All trademarks are the property of their respective owners. 

## Install

No release has been published yet, so build it. There is nothing to install beyond a C
compiler — `qcstats` has no dependencies.

- **New to Linux?** → **[`docs/BUILD.md`](docs/BUILD.md)**.
- **Comfortable with a terminal?** → the [Build](#build) section below.

`sudo make install` puts the binary in `/usr/local/bin`, which is on the default `PATH` on
the board, so `qcstats` works from any directory afterwards. Check it with:

```sh
qcstats --count 1
```

---

## Build

No dependencies — libc only.

```sh
make            # builds ./qcstats
```

## Usage

```sh
qcstats                       # one plain line per second, until killed
qcstats --json --count 5      # five NDJSON samples, then exit
qcstats --all-zones           # every thermal zone (48 on this board)
```

Nothing is printed for the first interval: CPU percentages are a delta and need two
`/proc/stat` readings, so there is nothing truthful to print before then.

Three output modes — `--plain` (default, for reading), `--json` (NDJSON, one object per
line), `--csv` (header row, empty field = absent). Absent is `n/a`, `null` and an empty
field respectively, and **never a zero**: `0 rpm` means the fan is off, `0%` means an idle
core, and a sensor that could not be read must not be able to claim either.

It runs unprivileged and writes nothing — with one opt-in exception, `--gpu-busy`, which
needs root and enables a tracepoint.

**Full reference — every flag, all three output formats, what each field means and how to
read it correctly: [`docs/USAGE.md`](docs/USAGE.md).**
The versioned field-by-field contract: [`docs/json-schema-v1.md`](docs/json-schema-v1.md).

## License

Three licences, chosen by what the file *is*:

| | |
|---|---|
| `src/` — the sampler itself | **MPL-2.0** |
| `Makefile` — build glue | **MIT** |
| `README.md`, `docs/*.md` — prose | **CC-BY-4.0** |

Every file carries an `SPDX-License-Identifier`. Full texts are in [`LICENSE`](LICENSE)
(MPL-2.0) and [`LICENSES/`](LICENSES/). **Code blocks inside the documentation are MIT**,
not CC-BY-4.0 — paste a snippet into your own project and you owe no attribution.

MPL-2.0 is *file-level* copyleft: patch one of these source files and publish that file.
It stops there. Linking `qcstats` into proprietary software creates no obligation to open
that software — static linking included — and new files you write alongside it stay yours.

**No CLA.** Contributions are inbound=outbound: your patch is licensed under whatever the
file you touched already carries, and you keep your copyright.

No third-party source is in this tree, so there is nothing to attribute. `libcdsprpc.so.1`
is resolved with `dlopen` at runtime under `--npu` and is neither bundled nor linked.
