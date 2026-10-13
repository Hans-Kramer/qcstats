<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# `qcstats` JSON schema, version 1

The contract between `qcstats --json` and its potential consumers.

## Format

**NDJSON** — one JSON object per line, no enclosing array and no terminator,
so the output is a stream a consumer can read line by line as it arrives:

```sh
qcstats --json 1000 | while read -r line; do ...; done
```

Every line is a complete, independently parseable object. Not every line is a *sample*:
see "The first line may be a meta line" below.

## The two rules that matter most

**1. Absent is `null`, never a zero and never the string `"n/a"`.**

This is the rule the whole schema is built around. `0 rpm` means the fan is off, `0`
throttle events means nothing throttled, `0 %` means an idle core — each a real, useful reading.
A source that could not be read must never be able to claim any of them.
If you see a number, it was measured.

Absence is reported **per attribute**, not per subsystem: `power` can be a live object
with `"mw": null` inside it if that one sysfs file failed while voltage and current read fine.

**2. A consumer must tolerate fields it does not know.**

New fields may appear within version 1. Adding one is *not* a breaking change,
precisely because rule 1 already forces consumers to handle fields being null or missing.
Do not fail on an unrecognised key.

## The first line may be a meta line, not a sample

A `--json` run emits **one leading line that is not a sample**, carrying the environment the
run was taken in:

```json
{"v":1,"meta":{"kernel":"6.8.0-1084-qcom","build_id":"20260710-235"}}
```

**How to tell it apart:** a sample always has `t`; this never does. It carries `meta`; a
sample never does. So a consumer iterating samples skips it with one test:

```python
for line in stream:
    o = json.loads(line)
    if "t" not in o:
        continue          # meta, or any future non-sample line
```

It is still a complete, independently parseable object on its own line, and `v` is still the
first field, so the dispatch rule above is unchanged. At most one is emitted, and only as the
first line of a run.

**Why a meta line and not a field on every sample.** The environment is a property of the
*run*, not a reading. Repeating two constant strings on every line of a 20 Hz stream is waste
the schema does not need to carry, and a reading that is identical forever is not a reading.

### `meta` fields

| field | type | notes |
|---|---|---|
| `meta.kernel` | string \| null | `/proc/sys/kernel/osrelease`, e.g. `6.8.0-1084-qcom` |
| `meta.build_id` | string \| null | `BUILD_ID` from `/etc/buildinfo`, e.g. `20260710-235`; matches the distributed archive name |

Either may be `null` independently — rule 1 applies here too. A board can have a readable
kernel and no `/etc/buildinfo` at all. **`null` means the source could not be read**, which is
not the same as the environment being unknown to the operator, and is deliberately not a
placeholder string: a consumer comparing two captures must never read a change that was never
observed.

**Why it matters.** Thermal-zone and hwmon indices are assigned at *probe time* — `ina232`
has been observed at six distinct hwmon indices. Discovery is by name, so nothing breaks at
runtime; but a saved stream, log, or quoted figure is uninterpretable later if nothing records
what produced it.

**`--csv` carries the same two values as trailing `kernel` and `build_id` columns**, repeated
on every row, because CSV's contract is rectangular data and a comment or second header line
is rejected or mis-parsed by a real share of readers. `--plain` prints one `env ...` line
first. Absent is an empty field in CSV and `n/a` in `--plain`.

## Versioning

`v` is the first field on every line, so a consumer can dispatch before parsing the rest.

`v` is bumped **only for a breaking change**:
a field removed, renamed, retyped, or given a different unit.
It is **not** bumped for a new field.

## Fields

| field | type | unit | notes |
|---|---|---|---|
| `v` | int | — | schema version; `1` |
| `t` | float | seconds | `CLOCK_REALTIME`, ms precision. For correlating against other logs |
| `ram` | object \| null | | |
| `ram.used_mb` | int | MiB | `MemTotal - MemAvailable` |
| `ram.total_mb` | int | MiB | |
| `cpu` | array | | one entry per **cpufreq policy**, not per core — always 3 on this board |
| `cpu[].label` | string | — | `A78C.0`, `A78C.1`, `A55` |
| `cpu[].mhz` | int \| null | MHz | the **policy's** frequency; frequency is per policy, not per core |
| `cpu[].cores` | array | | |
| `cpu[].cores[].cpu` | int | — | absolute CPU id |
| `cpu[].cores[].pct` | int \| null | % | `null` means offline **or** load unmeasurable |
| `gpu_mhz` | int \| null | MHz | Adreno 623; source reports Hz, converted here |
| `gpu_busy_pct` | int \| null | % | needs `--gpu-busy` and root; `null` otherwise |
| `npu` | object \| null | | needs `--npu`; `null` otherwise |
| `npu.q6_pct` | float | % | Hexagon Q6 utilisation |
| `npu.q6_khz` | int | kHz | 768000 idle, 1497600 under load |
| `npu.hvx_pct` | float | % | HVX vector units (4 on this SoC) |
| `npu.hmx_pct` | float | % | HMX matrix engine (1); often 0 — see below |
| `thermal_c` | object | °C | keyed by the **short** zone name, e.g. `cpuss-0-0` |
| `thermal_c.<zone>` | float \| null | °C | one key per resolved zone; the key stays even if a read fails |
| `power` | object \| null | | INA232; `null` means the sensor was not found |
| `power.mw` | int \| null | mW | **measured**, not modelled |
| `power.mv` | int \| null | mV | ~20 V rail (USB-C PD), not 5 V |
| `power.ma` | int \| null | mA | |
| `fan` | object \| null | | EMC2305 |
| `fan.duty_pct` | int \| null | % | derived from `pwm1` |
| `fan.rpm` | int \| null | rpm | |
| `fan.rpm_valid` | bool | — | `false` when duty is below state 2, where the tachometer misreads |
| `throttle` | object | | keyed by policy label |
| `throttle.<label>` | int \| null | events | LMh/DCVSh **event counts, not depth** |

## Semantics a consumer needs to get right

- **`cpu` is per policy.** Three policies on the board:
  `A78C.0` = CPU 0–1, `A78C.1` = CPU 2–3, `A55` = CPU 4–7.
  Anything keyed on "cpu0's frequency" silently misses two thirds of the machine.
- **`throttle` counts events, not severity.** It will read 0 in virtually all normal use:
  the fan's active trip is 38 °C while the CPU's own first passive trip is 126 °C,
  so on this board throttling is effectively unreachable.
  A 0 is meaningful; a `null` means `/proc/interrupts` could not be read.
- **`fan.rpm_valid: false`** means `rpm` is present but untrustworthy, not that the fan is broken.
  And `rpm: 0` usually means the fan is simply off.
- **`npu.hmx_pct` is often 0 even under heavy AI load.**
  Quantised llama.cpp paths use the HVX vector units, not the HMX matrix engine.
  A zero means "this workload did not use HMX", not a broken counter.
- **`npu` is polled at 1 Hz** regardless of `--interval`,
  because a query costs ~1.12 ms and loads the DSP itself.
  Between polls the previous reading stands;
  it is a windowed average at most one period old.
  It is dropped, not held, after three periods.
- **`gpu_busy_pct` can be `null` while `gpu_mhz` is high.**
  The GPU's devfreq polling stops when it runtime-suspends,
  so a just-woken GPU may have no measured window yet.
  `null` there means "not measured", and is deliberately preferred over a stale value.
- **The sampler perturbs what it measures.** At 20 Hz with all 48 zones it draws about +384 mW.
  If you are comparing two power profiles, sample both at the same rate.

## The CSV equivalent

`--csv` carries the same data with a header row, which serves the same self-describing
role as `v`. Its column set is **fixed for a run** and does not depend on which flags are passed,
so header and rows always align. Absent is an **empty field** there — same rule, different spelling.

The last two columns are `kernel` and `build_id`, repeated on every row. That repetition is
deliberate: a CSV reader expects rectangular data, and the alternative — a comment line or a
second header — is rejected or mis-parsed by a real share of readers unless the caller knows
to pass an option for it. Two constant columns are waste, but they are waste no parser is
surprised by, and a CSV capture with no provenance is the problem the field exists to solve.
