<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Using `qcstats`

What it reports, how to ask for it, and how to read the result correctly.

For the field-by-field contract — types, units, nullability — see
**[`json-schema-v1.md`](json-schema-v1.md)**. For building and installing, see
**[`BUILD.md`](BUILD.md)**.

---

## Quick start

```sh
qcstats                       # one plain line per second, until killed
qcstats --json --count 5      # five NDJSON samples, then exit
qcstats --csv --interval 200  # CSV at 5 Hz
qcstats --all-zones           # every thermal zone (48 on this board)
```

**Nothing is printed for the first interval.** The first sample sleeps before it emits,
because CPU percentages are a delta and need two `/proc/stat` readings — there is nothing
truthful to print before then. A consequence worth knowing: a run interrupted *during* that
first wait produces **no output at all**, not even the provenance line.

Runs unprivileged. No `sudo`, no setuid — it reads `scaling_cur_freq` (mode 444) rather than
`cpuinfo_cur_freq` (mode 400) precisely so that stays true. Exactly one flag needs root; see
[GPU busy %](#gpu-busy--opt-in-and-it-writes).

## Options

```
qcstats [--plain|--json|--csv] [--count N] [--interval MS]
        [--zones LIST | --all-zones] [--gpu-busy] [--npu] [--logfile P] [interval_ms]
```

| Flag | |
|---|---|
| `--plain` | compact single-line form, for reading (**default**) |
| `--json` | NDJSON, one object per line |
| `--csv` | CSV with a header row; empty field = absent |
| `--count N` | emit N samples then exit. Omit to run until killed |
| `--interval MS` | sampling interval; same as the positional form |
| `--zones LIST` | comma-separated, e.g. `gpuss-0,ddrss-0` |
| `--all-zones` | every zone, ordered by index — 48 on the VENTUNO |
| `--gpu-busy` | true GPU busy %. **Needs root**, and writes — see below |
| `--npu` | Hexagon NSP utilisation and clock. Polls at 1 Hz — see below |
| `--logfile P` | write samples to P instead of stdout. **Truncates P** |
| `-V`, `--version` | print the version and the JSON schema version, then exit |
| `-h`, `--help` | print usage, then exit |

All three output modes report which kernel and image the run was taken on — see
[Provenance](#provenance-which-kernel-and-image).

Four details the usage line does not convey:

- **The output modes are last-wins, not exclusive.** `--plain --json` emits JSON;
  `--json --plain` emits plain. The same is true of every repeated flag.
- **`--interval` below 50 ms is silently raised to 50.** Only `0` is an error.
- **`--count 0` is rejected.** "Run until killed" is reached by *omitting* `--count`, not by
  passing zero.
- **No `--flag=value` form and no `--` terminator.** `--count=5` is an unknown argument, and
  a missing value is only detected at the end of the command line — so `--count --json`
  consumes `--json` as the count and fails on it.

`-h` prints to **stderr**; `-V` prints to **stdout**, because `-V` is meant to be read by a
script. `qcstats --help | less` shows nothing.

## Choosing zones

`--zones` accepts **either** the short name the tool prints (`gpuss-0`) **or** the full sysfs
type (`gpuss-0-thermal`). The output shows the short form, so a label copied straight out of
a line will match.

The default set is five zones:

| zone | |
|---|---|
| `cpuss-0-0` | also the only zone that drives the fan |
| `cpuss-1-0` | |
| `gpuss-0` | |
| `nsp-0-0-0` | NPU |
| `ddrss-0` | |

**Zones are resolved by their `type` string, never by index.** Those five sit at indices 37,
9, 33, 5 and 8 on this board, and the ordering is not guaranteed across boots either. The
same rule governs the hwmon sensors: `ina232` and `emc2305` are found by name.

A malformed list (`a,,b`, `a,`, `,a`) is a usage error and exits 2, as is a list longer than
64 names. A name that simply does not resolve **warns on stderr and sampling continues** —
one bad zone never suppresses the rest, and never aborts the run. A default zone that is
absent is not reported, because that is not a user error.

`--zones` and `--all-zones` are mutually exclusive and the combination is refused, whichever
order they appear in. An explicit list is reported in **your** order; `--all-zones` follows
index order.

## Output modes

### `--plain`

```
RAM 1706/15284MB CPU [A78C.0:3%,8%@1017MHz A78C.1:5%,5%@940MHz A55:24%,5%,5%,5%@844MHz]
GPU 479MHz cpuss-0-0@36.0C VDD_IN 10920mW (20.0V 543mA) FAN 0%@0rpm LMh [A78C.0:0 A78C.1:0 A55:0]
```

(one physical line; wrapped here for reading)

Absent reads `n/a`. Two tokens are specific to this mode: a core that is **offline** shows
`off` rather than `n/a`, and a `?` suffix on the fan — `FAN 20%@3172rpm?` — means the
tachometer reading is present but untrustworthy.

**`--plain` is for reading, not for parsing.** It has no fixed field count: a thermal zone
whose read failed is *omitted* rather than shown as `n/a`, and the whole `FAN` and `NPU`
sections vanish when those sources are absent. Use `--json` or `--csv` for anything
programmatic.

### `--json`

NDJSON — one object per line, so a consumer reads it as a stream:

```sh
qcstats --json 1000 | while read -r l; do ...; done
```

**Every key is always present.** The key set does not depend on which flags were passed;
only the *values* become `null`. `gpu_busy_pct` is `null` without `--gpu-busy`, `npu` is
`null` without `--npu`, and a thermal zone that fails a read keeps its key with a `null`
value so the shape stays stable.

The contract is versioned: `v` is the first field on every line and is bumped **only** for a
breaking change — a field removed, renamed, retyped, or given a different unit. Adding a
field is not breaking, because the absent-is-`null` rule already requires consumers to
tolerate keys they do not know. Full field list, units and semantics:
**[`json-schema-v1.md`](json-schema-v1.md)**.

### `--csv`

One header row, then one row per sample. An absent value is an **empty field**, matching
JSON's `null`; an offline core is empty rather than `0`.

The column set is **fixed for a run**, so header and rows always align — but it is not
independent of your flags: the `temp_<zone>_c` columns follow the zones that resolved at
startup, so `--zones` and `--all-zones` change them. The `gpu_busy_pct` and the four
`npu_*` columns are always present, whether or not `--gpu-busy` and `--npu` were given.

Two spellings differ from JSON by design: `fan_rpm_valid` is `1`/`0` rather than
`true`/`false`, since CSV is consumed by numeric tools, and provenance arrives as two
trailing columns on every row rather than as a separate line. Zone names are quoted per
RFC 4180 when they contain a comma or quote.

The header can only be written once a sample exists to describe it, which is why
`--logfile` truncates rather than appends — a CSV header must never land mid-file.

### Piping and `--logfile`

Every sample is flushed as it is emitted, so a pipe sees each line at the end of its tick
and a `SIGTERM` loses at most the sample in progress. `SIGINT`, `SIGTERM` and `SIGHUP` are
handled and only break between samples, so a graceful stop never truncates a line.

`--logfile P` sends samples to `P` instead of stdout and **truncates `P` on open**. Stderr
notices still go to stderr, so the data stays clean either way; the file exists so a dropped
terminal cannot lose it. A path that cannot be opened fails immediately, before any sampler
starts.

## Absent is never zero

| mode | absent |
|---|---|
| `--plain` | `n/a` |
| `--json` | `null` |
| `--csv` | empty field |

That distinction is the point. `0 rpm` means the fan is off, `0` throttle events means
nothing throttled, and `0%` means an idle core — each a real, useful reading. A sensor that
could not be read must not be able to claim any of them.

Absence is reported **per attribute**, not per subsystem: `power` can be a live object with
`"mw": null` inside it if that one file failed while voltage and current read fine.

Every source is optional at runtime. A missing one reports absent and the sample continues —
`CONFIG_SENSORS_INA2XX=m` means the power sensor can legitimately be absent on a working
board, so degradation is a feature, not a fallback.

## What it reports

| Field | Source | Notes |
|---|---|---|
| RAM | `/proc/meminfo` | `MemTotal - MemAvailable`, in MB. `MemAvailable`, not `MemFree`: free excludes reclaimable page cache and understates what a workload can get |
| CPU freq | `cpufreq/policy{0,2,4}` | **three policies**, not eight cores: A78C 0–1, A78C 2–3, A55 4–7 |
| CPU load | `/proc/stat` | a delta between two samples, rounded half-up; `idle + iowait` counts as idle |
| GPU | `devfreq/3d00000.gpu` | reports Hz; 479/599/780/877 MHz. No busy counter here — see below |
| Temps | `thermal_zone*/type` | millidegrees, emitted to one decimal |
| VDD_IN | `hwmon` `ina232` | measured power, not a model. `mw` is the sensor's own register, **not** `mv × ma` |
| FAN | `hwmon` `emc2305` | duty is `pwm1 × 100 / 255`; `?` flags a tach misread below state 2 |
| LMh | `/proc/interrupts` `dcvsh-irq-{0,2,4}` | throttle **events**, not depth — and **cumulative since boot**, so difference them yourself |
| NPU | sysmon over FastRPC (`--npu`) | Q6 util/clock + HVX/HMX; no sysfs source exists |

Frequency is **per policy, not per core**. Anything keyed on `cpu0` alone silently misses two
thirds of the machine.

The fan's tachometer misreads below state 2 (`pwm1` under 51) — state 1 reports about
3172 rpm against a 5140 rpm maximum — so `rpm` is still reported but `rpm_valid` is false.
It is also false whenever `pwm1` itself could not be read, because the test cannot be
evaluated. `rpm: 0` with `rpm_valid: true` means the fan is simply off.

Throttle counters read `0` in essentially all use, and that is a true reading: the fan's
active trip is 38 °C while the CPU's own first passive trip is 126 °C, so with a working fan
there is nothing to count. (115 °C is the first passive trip on the GPU and NSP zones, not
the CPU.) A `0` is real; `null` means `/proc/interrupts` could not be read.

### GPU busy % — opt-in, and it writes

There is **no sysfs export** of GPU utilisation on this SoC: no `/sys/class/kgsl` (this is
upstream `drm/msm`), no `*busy*` attribute, and `debugfs/dri/0/gpu` is a register dump. The
devfreq *core* computes it — `simple_ondemand` cannot work without it — and exports it
through the **`devfreq_monitor` tracepoint** as `100 * busy_time / total_time`.

So `--gpu-busy` is the one thing `qcstats` does that writes to the system: it enables that
tracepoint in a **private trace instance** of its own, and removes it on exit, including on
`SIGINT`/`SIGTERM`/`SIGHUP`. A private instance rather than the global buffer, so it neither
disturbs tracing the operator set up nor consumes events another reader wants. It needs root,
because `tracefs` is `0700`. Without the flag none of this runs.

> **A `SIGKILL` leaves it behind.** Restoration happens on the normal exit path and on the
> three handled signals. After a `SIGKILL`, a crash or power loss, an
> `instances/qcstats-<pid>` directory remains with the tracepoint still enabled.

Three honest limits:

- The tracepoint fires on devfreq's own **50 ms** polling, independent of `--interval`, and
  the sampler reports the most recent window — not an average over your interval.
- devfreq **stops polling when the GPU runtime-suspends**, which is about **97 % of uptime**
  on this board. A suspended GPU reports **0 %**: it is powered down and cannot be executing
  anything, so that is a fact rather than a guess — but note it is *inferred from the
  suspend state*, and the output does not distinguish it from a measured zero.
- If the GPU is *active* and no window has been measured yet, the field is **absent**, not a
  stale value. Events can be seconds apart.

### NPU — opt-in, 1 Hz, and it needs `fastrpc` group access (not root)

The Hexagon NSP has **no sysfs presence whatsoever**: no clock, no devfreq, no OPP table.
`--npu` reaches it through the DSP-side **sysmon** service over FastRPC and reports Q6
utilisation and clock plus HVX and HMX utilisation.

It costs nothing when unused: `libcdsprpc.so.1` is resolved with `dlopen` at runtime, so the
binary still **links only libc**, and a board without the library simply reports absent. No
headers or libraries are needed to build it, and no third-party source is in this tree.

Four behaviours worth knowing:

- **It polls at 1 Hz regardless of `--interval`.** A query costs ~1.12 ms *and loads the
  DSP* — polling back-to-back put `q6_util` at ~50 % on an idle board, entirely
  self-inflicted. At a shorter interval the held reading is re-emitted unchanged, so
  consecutive samples can carry identical NPU numbers.
- **A held reading is at most one second old**, and is reported absent rather than presented
  stale beyond that.
- **The first reading is discarded** during startup: the window straight after `init` is
  partial and has been seen to report `q6_clock = 1`.
- **`hmx_pct` is often 0 even under heavy AI load.** llama.cpp's Q4_0 path uses the **HVX**
  vector units, not the **HMX** matrix engine. A zero means "this workload did not use HMX",
  not a broken counter.

Measured on the board: idle `q6_pct` ≈ 0.05 at 768 MHz; under a Hexagon inference load
`q6_pct` 64–73 with `hvx_pct` 56–63 and the clock stepping to **1497.6 MHz**.

`--npu` needs membership of the `fastrpc` group, because `/dev/fastrpc-cdsp` is
`fastrpc:fastrpc` — and the `arduino` user is already in it, so it simply works. Running the
whole tool as root would buy you nothing but `--gpu-busy`: root unlocks no further telemetry
here, since `qcom_aoss/ddr_frequency_mhz` is mode `220` — write-only even for root — and
`interconnect_summary` holds bandwidth *votes*, not traffic counters.

### Not reported, and why

- **DDR frequency** — nothing an unprivileged process can read. No DDR clock appears in
  `clk_summary`, and `interconnect_summary` holds bandwidth *requests and votes*, not traffic
  counters, so it cannot be used to measure DDR activity at all.

  A firmware residency table does exist. Validated on this board at physical `0x0c3f00b8`
  (magic `0xa1157a75`), it holds twelve frequency records carrying firmware MHz labels — 200,
  451, 547, 681, 768, 1017, 1353, 1555, 1708, 2092, 2736, 3196 — beside four low-power-mode
  records. Reading it needs root *and* an out-of-tree kernel module, which taints the kernel
  until reboot, so it stays out of scope here. Even with access the records are **cumulative
  and settled late**: across one measured four-second RAM workload only 1.98 % of the elapsed
  time had been accounted for, the remainder turning up in a later snapshot. Over a full
  162-second capture the accounting reconciled to 99.9998 %. An instantaneous `ddr_mhz`
  derived from those counters would be a fabrication, so none is offered.
- **Throttle depth.** `LMh` reports event *counts*, not severity. See above.
- **GPU busy, without `--gpu-busy`.** Deliberately absent rather than faked from residency.

## Provenance: which kernel and image

Every run records the environment it was taken in, so a saved stream stays interpretable
later. Zone and hwmon indices are assigned at probe time — a reading means one thing on the
kernel it was taken on and possibly another after a kernel bump, and recording which one is
the point.

**`--json` emits one leading line that is not a sample:**

```json
{"v":1,"meta":{"kernel":"6.8.0-1084-qcom","build_id":"20260710-235"}}
```

A sample always has `t`; this never does. So a consumer iterating samples skips it with one
test — `if "t" not in obj: continue`. Either field is `null` if its source could not be read;
absent is never a placeholder string, so a consumer comparing two captures cannot read a
change that was never observed.

`--plain` prints an `env kernel ... image ...` line first — note the label there is `image`,
where JSON and CSV call it `build_id`. `--csv` carries the same two values as trailing
columns on every row.

## Which build is running

```console
$ qcstats -V
qcstats 0.1.0-pre
json-schema 1
```

Two lines, because the two move independently: the first is this binary's own version, which
changes whenever the code does; `json-schema` is the **consumer contract**, bumped only for a
breaking change. A version bump is therefore not a schema bump, and the second line is the
one a reader of a saved stream needs.

A binary reporting `unknown` was compiled by hand rather than through `make`, which is where
the version comes from. That is deliberately not a plausible-looking number.

## Exit codes

| | |
|---|---|
| `0` | normal completion — `--count` reached, or `SIGINT`/`SIGTERM`/`SIGHUP` received, or `-h`/`-V` |
| `2` | a usage error, or `--logfile` could not be opened |

There is no other code. **A sampler that cannot open its source is absent, not an error** —
nothing in the sampling loop can change the exit status, so a run on a board with no fan and
no power sensor still exits `0`.

## Environment

| | |
|---|---|
| `QCSTATS_SYSROOT=<dir>` | read sysfs/procfs under `<dir>` instead of `/` |

For running against a captured fixture tree on a machine that is not a Ventuno Q. It is
announced on stderr when set, applied before zone names are resolved — they resolve against
that tree — and an empty value counts as unset.

## Cost on the board

| | |
|---|---|
| Peak RSS | **1,408 kB** (worst case: 48 zones at 20 Hz) |
| CPU, 5 zones at 1 Hz | 0.03 s over 10 s — **~0.3 % of one core** |
| CPU, 48 zones at 20 Hz | 0.31 s over 3 s — **~10 % of one core** |
| Cadence error | 1.5 % ± 0.5 %, and **does not accumulate** |
| Syscalls per extra sample | **0** (every source is held open) |

Every source is an fd held open for the whole run and re-read with `pread`, which is why an
extra sample costs no syscalls. One consequence: a sysfs attribute missing at startup stays
absent for the run — it does not appear mid-run.

Cadence is kept on absolute deadlines rather than by sleeping for the interval, because
sleeping *adds* the sampling cost to it: 60 samples at 50 ms took 3.67 s instead of 3.00 s,
a 22 % longer cadence than asked for. An overrun resets the schedule rather than catching up
in a burst, so timestamps can drift from an exact grid by design.

**The sampler's own power draw is an observer effect worth knowing about.** Measured on the
board: 7,624 mW idle against 8,008 mW while sampling 48 zones at 20 Hz — **+384 mW, about
5 %**. If you are comparing two power profiles, sample both at the same rate.
