<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# qcstats

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

One sample per line, for reading live or logging to a file (one line, wrapped here):

```
RAM 1344/15284MB CPU [A78C.0:1%,0%@1017MHz A78C.1:0%,0%@940MHz A55:4%,0%,0%,1%@844MHz]
GPU 479MHz cpuss-0-0@35.7C cpuss-1-0@35.4C gpuss-0@35.7C nsp-0-0-0@35.1C ddrss-0@35.1C
VDD_IN 10800mW (20.1V 539mA) FAN 40%@2658rpm LMh [A78C.0:0 A78C.1:0 A55:0]
```

## What it reports

- **CPU** — per-core utilisation and per-cluster frequency, across all three frequency
  policies (four Cortex-A78C cores in two policies, four A55)
- **Memory** — used and total
- **GPU** — Adreno frequency, and with `--gpu-busy` the true busy percentage
- **NPU** — Hexagon NSP utilisation and clock, with `--npu`
- **Temperatures** — five zones by default, or any of the 48 the board exposes
- **Power** — board input power, voltage and current from the INA232
- **Fan** — duty cycle and measured rpm
- **Throttling** — LMh throttle events per cluster (counts since boot, not severity)

Three output formats: `--plain` (default), `--json` (NDJSON, one object per sample) and
`--csv`. A source that cannot be read is reported as **absent** — `n/a`, `null`, or an empty
field — and **never as a zero**, because `0 rpm` means the fan is off and `0%` means an idle
core.

It links nothing but libc — `--npu` resolves the vendor FastRPC library at runtime, and
reports absent when it is not there. It runs unprivileged and writes nothing to the system,
with one opt-in exception: `--gpu-busy` needs root and enables a kernel tracepoint, in a
private trace instance it removes again on exit.

## Scope

**This targets one board.** Every path it reads was verified on a Ventuno Q, and nothing else
is tested. On other hardware the paths are simply absent, and absent is reported as absent.

## Install

Build it — there is nothing to install beyond a C compiler, and no dependencies:

```sh
make
sudo make install          # /usr/local/bin, on the board's default PATH
qcstats --count 1
```

### Install from GitHub

A release builds on the board itself, once there is a toolchain — the board image has
none:

```sh
sudo apt update && sudo apt install -y build-essential
curl -fsSLO https://github.com/Hans-Kramer/qcstats/archive/refs/tags/v0.1.0.tar.gz
tar xzf v0.1.0.tar.gz
cd qcstats-0.1.0
make
sudo make install
```

Substitute the tag of whichever release you want; the same tarball is the *Source code
(tar.gz)* link on the release page.

`make install` places exactly two files — `/usr/local/bin/qcstats` and
`/usr/local/share/man/man1/qcstats.1` — and `sudo make uninstall` removes both. It needs
the extracted directory to do that, so keep it, or delete those two paths by hand.

## Documentation

- [`docs/USAGE.md`](docs/USAGE.md) — every flag, all three formats, and how to read each
  field correctly
- [`docs/BUILD.md`](docs/BUILD.md) — building, testing and installing, step by step
- [`docs/json-schema-v1.md`](docs/json-schema-v1.md) — the versioned JSON contract
- [`CHANGELOG.md`](CHANGELOG.md) and [`TODO.md`](TODO.md) — what changed, and what is open

## License

`MPL-2.0` for the source, `CC-BY-4.0` for the documentation, `MIT` for the build and CI
glue. See [`LICENSE`](LICENSE) and [`LICENSES/`](LICENSES/).
