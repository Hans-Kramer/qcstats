#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
# SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
#
# Capture the sysfs and procfs shape this sampler reads, as a tree for QCSTATS_SYSROOT.
# Run it ON a board; the result is small enough to commit - the whole read surface of this
# project is a few kilobytes of text.
#
#   sh tests/capture-fixture.sh tests/fixtures/monza-20260710-235-6.8.0-1084
#
# The file list below IS the sampler's read surface, which is the second reason this script
# exists: a path that is not here is a path no fixture can satisfy, and a test against it
# would fail for the wrong reason.
#
# Unreadable attributes are left OUT rather than written empty. An absent source and an
# empty one mean the same thing to src_read(), and an absent one is the honest record.
set -u

OUT=${1:-}
[ -n "$OUT" ] || { echo "usage: $0 <output-dir>" >&2; exit 2; }
[ -d /sys/class/thermal ] || { echo "$0: no /sys/class/thermal - not a Linux board?" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT" || exit 1
n=0
skipped=0

# grab <absolute-path> - copy one regular file, or record nothing if it cannot be read.
# Symlinks are NOT followed here: sysfs is full of plumbing links - a devfreq node's
# `device` and `subsystem`, for instance - whose targets are nowhere near the captured set,
# and copying them leaves a tree full of dangling links that breaks `scp -r`, `cp -L` and
# anything else that resolves them. The links that carry meaning are captured explicitly by
# grab_link below.
grab() {
    [ -e "$1" ] || return 0
    [ -L "$1" ] && return 0
    [ -f "$1" ] || return 0
    mkdir -p "$OUT$(dirname "$1")"
    if cat "$1" > "$OUT$1" 2>/dev/null; then
        n=$((n + 1))
    else
        rm -f "$OUT$1"
        skipped=$((skipped + 1))
    fi
}

# grab_glob <glob...> - unquoted on purpose, so the shell expands it.
grab_glob() {
    for p in "$@"; do grab "$p"; done
}

# grab_link <absolute-path> - a symlink whose target is RELATIVE, so it still resolves
# inside the captured tree. A zone's cdevN links are the only ones this project reads, and
# they point at ../cooling_deviceN.
grab_link() {
    [ -L "$1" ] || return 0
    case "$(readlink "$1")" in
        /*) skipped=$((skipped + 1)); return 0 ;;     # would dangle in the fixture
    esac
    mkdir -p "$OUT$(dirname "$1")"
    ln -sfn "$(readlink "$1")" "$OUT$1"
    n=$((n + 1))
}

# grab_dev_link <absolute-path> - an interface's `device` link. Its target is relative but
# points OUTSIDE the captured set, so copying the link alone leaves the tree with a dangling
# entry, and a tree with one of those cannot be copied by `scp -r` or `cp -L`. An empty stub
# directory at the resolved target keeps the link text - which is the only thing readlink()
# hands the classifier - while leaving the tree copyable. The `..` components resolve against
# the directories already built above, so this needs no realpath.
grab_dev_link() {
    [ -L "$1" ] || return 0
    tgt=$(readlink "$1")
    case "$tgt" in
        /*) skipped=$((skipped + 1)); return 0 ;;
    esac
    mkdir -p "$OUT$(dirname "$1")"
    ln -sfn "$tgt" "$OUT$1"
    mkdir -p "$OUT$(dirname "$1")/$tgt"
    n=$((n + 1))
}

echo "capturing into $OUT"

# ---- thermal: zones, their trips, and the cooling devices they drive ----
for z in /sys/class/thermal/thermal_zone*; do
    [ -d "$z" ] || continue
    grab_glob "$z"/type "$z"/mode "$z"/policy "$z"/temp "$z"/available_policies \
              "$z"/sustainable_power "$z"/k_po "$z"/integral_cutoff
    grab_glob "$z"/trip_point_*_type "$z"/trip_point_*_temp "$z"/trip_point_*_hyst
    # The cdevN symlinks are how a zone's cooling devices are discovered, so they are
    # captured as symlinks rather than resolved away.
    for l in "$z"/cdev*; do grab_link "$l"; done
    grab_glob "$z"/cdev*_trip_point "$z"/cdev*_weight
done
for c in /sys/class/thermal/cooling_device*; do
    [ -d "$c" ] || continue
    grab_glob "$c"/type "$c"/cur_state "$c"/max_state
done

# ---- cpufreq, per POLICY rather than per core: frequency is a policy property ----
for p in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$p" ] || continue
    for f in "$p"/*; do grab "$f"; done
done
# ---- per-cpu: the online node, capacity, and cpuidle states ----
for c in /sys/devices/system/cpu/cpu[0-9]*; do
    [ -d "$c" ] || continue
    grab_glob "$c"/online "$c"/cpu_capacity
    for s in "$c"/cpuidle/state*; do
        [ -d "$s" ] || continue
        grab_glob "$s"/name "$s"/desc "$s"/disable "$s"/latency
    done
done

# ---- the GPU's devfreq node, and any other devfreq this board exposes ----
for d in /sys/class/devfreq/*; do
    [ -d "$d" ] || continue
    for f in "$d"/*; do grab "$f"; done
done

# ---- hwmon: resolved by NAME at runtime, so every hwmon's name must be present ----
for h in /sys/class/hwmon/hwmon*; do
    [ -d "$h" ] || continue
    grab "$h"/name
    grab_glob "$h"/pwm* "$h"/fan* "$h"/in[0-9]* "$h"/curr[0-9]* "$h"/power[0-9]* \
              "$h"/temp[0-9]*
done

# ---- network: every interface, and the attributes that classify it ----
# `speed` and `carrier` are refused with EINVAL on a down CAN link and on loopback, so
# they land in the skipped count rather than as empty files - which is the honest record:
# an absent source and an empty one mean the same thing to src_read().
for i in /sys/class/net/*; do
    [ -d "$i" ] || continue
    grab_glob "$i"/type "$i"/operstate "$i"/carrier "$i"/mtu "$i"/speed
    grab_glob "$i"/statistics/rx_bytes "$i"/statistics/tx_bytes \
              "$i"/statistics/rx_packets "$i"/statistics/tx_packets \
              "$i"/statistics/rx_errors "$i"/statistics/tx_errors \
              "$i"/statistics/rx_dropped "$i"/statistics/tx_dropped
    # `device` is read with readlink() and never followed, so a relative target that
    # dangles inside the fixture still answers the only question asked of it: whether the
    # parent chain runs through USB.
    grab_dev_link "$i"/device
    # wireless/phy80211/bridge are tested for EXISTENCE only, and open() on a directory
    # succeeds - so an empty directory is a faithful capture of each marker. phy80211 is
    # a symlink on this board, and is recorded as a directory for that reason: its target
    # is nowhere near the captured set, and only its presence is ever read.
    for m in wireless phy80211 bridge; do
        if [ -e "$i/$m" ]; then
            mkdir -p "$OUT$i/$m"
            n=$((n + 1))
        fi
    done
done

# ---- procfs ----
grab_glob /proc/stat /proc/meminfo /proc/interrupts /proc/loadavg /proc/uptime \
          /proc/sys/kernel/osrelease /proc/sys/kernel/sched_util_clamp_min \
          /proc/sys/kernel/sched_util_clamp_max

# ---- identity: the build id and model a capture is only meaningful with ----
grab /etc/buildinfo
grab /proc/device-tree/model

# ---- provenance, so a stale fixture can be recognised as one ----
{
    echo "# qcstats fixture capture"
    echo "captured   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host       $(uname -n)"
    echo "kernel     $(uname -r)"
    echo "arch       $(uname -m)"
    echo "build_id   $(sed -n 's/^BUILD_ID=//p' /etc/buildinfo 2>/dev/null || echo unknown)"
    echo "model      $(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo unknown)"
    echo "files      $n"
} > "$OUT/.capture"

echo "  $n files captured, $skipped unreadable and left out"
echo "  provenance in $OUT/.capture"
echo "  size: $(du -sh --apparent-size "$OUT" | cut -f1) of content"
