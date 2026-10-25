#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
# SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
#
# What only hardware can answer. Run this ON a Ventuno Q, as root.
#
# Everything else in tests/ runs against a captured tree, and a captured tree cannot
# contain tracefs: --gpu-busy's one write goes to an absolute path that no sysroot
# relocates, the event it enables is produced by a running devfreq governor, and the
# metric exists only while a GPU is powered. So the suite asserts the REFUSAL when
# tracefs cannot be read, and the path where it succeeds is checked here.
#
#   sh board-check.sh              # the default sections
#   sh board-check.sh --list       # what there is
#   sh board-check.sh teardown     # just one
#   QCSTATS=/tmp/qcstats sh board-check.sh    # a staged build, not the installed one
#
# It puts the board back on the way out, whatever happens: the GPU's runtime PM as it
# found it, no trace instance of ours left, and the global trace buffer untouched.
set -u

SECTIONS_DEFAULT="surface gpubusy suspended teardown"
SECTIONS_ALL="$SECTIONS_DEFAULT"

QCSTATS=${QCSTATS:-qcstats}
# Must match GPU_DEVFREQ_NAME in src/gputrace.c: the parser skips every trace line that
# does not carry this string, so a board that renames the devfreq device reports nothing
# and says nothing. That is what the first check of the surface section is for.
GPU_DEVFREQ_NAME=3d00000.gpu
GPUDIR=/sys/devices/platform/soc@0/3d00000.gpu

CHECKS=0
FAILS=0
SKIPS=0
TRACEFS=
WITNESS=
GLOBAL_SAVED=
PCTL_SAVED=
WORK=
KID=

ok()   { CHECKS=$((CHECKS + 1)); printf '    ok    %s\n' "$1"; }
bad()  { CHECKS=$((CHECKS + 1)); FAILS=$((FAILS + 1)); printf '    FAIL  %s\n' "$1"; }
note() { printf '          %s\n' "$1"; }
# A section whose precondition the machine cannot meet is skipped, not failed - and the
# skip is counted and repeated in the summary, because a skip nobody notices is worse
# than a failure. Here it is the GPU: whether it suspends depends on what else is using
# it, which is not something this script gets to decide.
skip_() { SKIPS=$((SKIPS + 1)); printf '    SKIP  %s\n' "$1"; }
head_() { printf '\n== %s\n' "$1"; }

# eq <got> <want> <what>
eq() { [ "$1" = "$2" ] && ok "$3" || bad "$3 (got '$1', want '$2')"; }

# has <text> <needle> <what>
has() {
    case "$1" in
        *"$2"*) ok "$3" ;;
        *) bad "$3 ('$2' is not in the output)" ;;
    esac
}

# wait_until <seconds> <shell-condition>
wait_until() {
    _d=$1; shift
    _i=0
    while [ "$_i" -lt "$_d" ]; do
        eval "$@" && return 0
        sleep 1; _i=$((_i + 1))
    done
    return 1
}

# Same two candidates and the same order as tracefs_root() in src/gputrace.c. Both are
# mounted on this board and they are one buffer seen twice, so the order only decides
# which name appears in the notice - and that is asserted below.
resolve() {
    for c in /sys/kernel/tracing /sys/kernel/debug/tracing; do
        [ -r "$c" ] && [ -x "$c" ] && { TRACEFS=$c; break; }
    done
    [ -n "$TRACEFS" ] || return 1
    [ -f "$TRACEFS/events/devfreq/devfreq_monitor/enable" ] || return 1
    [ -d "$GPUDIR/power" ] || return 1
    GLOBAL_SAVED=$(cat "$TRACEFS/events/devfreq/devfreq_monitor/enable")
    WORK=$(mktemp -d) || return 1
    return 0
}

rt_status() { cat "$GPUDIR/power/runtime_status" 2>/dev/null; }

# A second instance of our own, recording the same event for the whole run. It is the
# witness for three different claims: what the kernel actually reported in a window (so
# the number qcstats printed can be checked against it rather than merely being in
# range), that qcstats disabling the event on exit does not stop someone else's tracing,
# and - when it stays silent - that a reported 0 cannot have come from a trace line.
#
# NOT named qcstats-*: the teardown checks assert that no instance matching that glob
# survives, and a witness called qcstats-something would be the thing they found.
w_start() {
    WITNESS=$TRACEFS/instances/bc-witness-$$
    mkdir -p "$WITNESS" 2>/dev/null \
        && echo 1 > "$WITNESS/events/devfreq/devfreq_monitor/enable" 2>/dev/null
}
w_stop() {
    [ -n "$WITNESS" ] || return 0
    echo 0 > "$WITNESS/events/devfreq/devfreq_monitor/enable" 2>/dev/null
    rmdir "$WITNESS" 2>/dev/null
    WITNESS=
}
w_clear() { : > "$WITNESS/trace"; }
w_count() { _n=$(grep -c "$GPU_DEVFREQ_NAME" "$WITNESS/trace" 2>/dev/null) || _n=0; echo "$_n"; }
w_loads() {
    sed -n "s/.*$GPU_DEVFREQ_NAME.*load=\([0-9][0-9]*\).*/\1/p" "$WITNESS/trace" \
        | sort -un | tr '\n' ' '
}

# Any instance qcstats left behind, by the name it gives them.
leftovers() {
    _l=
    for d in "$TRACEFS"/instances/qcstats-*; do
        [ -d "$d" ] && _l="$_l ${d##*/}"
    done
    echo "$_l"
}

gpu_force_on() {
    PCTL_SAVED=$(cat "$GPUDIR/power/control")
    echo on > "$GPUDIR/power/control"
}
gpu_unforce() {
    [ -n "$PCTL_SAVED" ] && echo "$PCTL_SAVED" > "$GPUDIR/power/control" 2>/dev/null
    PCTL_SAVED=
}

# The sample lines' gpu_busy_pct, one per line, "null" included - absence has to stay
# visible here, because reporting it as 0 is the one thing the schema forbids.
busy_values() {
    sed -n 's/.*"gpu_busy_pct":\([^,}]*\).*/\1/p' "$1"
}

restore() {
    [ -n "$KID" ] && kill -KILL "$KID" 2>/dev/null
    KID=
    gpu_unforce
    w_stop
    # Whatever this run or an interrupted earlier one left: disable the event first, then
    # remove the directory. rmdir alone would leave the tracepoint enabled.
    for d in "$TRACEFS"/instances/qcstats-*; do
        [ -d "$d" ] || continue
        echo 0 > "$d/events/devfreq/devfreq_monitor/enable" 2>/dev/null
        rmdir "$d" 2>/dev/null
    done
    [ -n "$GLOBAL_SAVED" ] \
        && echo "$GLOBAL_SAVED" > "$TRACEFS/events/devfreq/devfreq_monitor/enable" 2>/dev/null
    [ -n "$WORK" ] && rm -rf "$WORK"
}

# ------------------------------------------------------------------ sections
s_surface() {
    head_ "surface: what --gpu-busy depends on, on this board"
    note "tracefs=$TRACEFS  qcstats=$(command -v "$QCSTATS" || echo "$QCSTATS")"
    out=$("$QCSTATS" -V 2>&1); eq "$?" 0 "$QCSTATS runs here"
    note "$(printf '%s' "$out" | tr '\n' ' ')"

    [ -d "/sys/class/devfreq/$GPU_DEVFREQ_NAME" ] \
        && ok "the devfreq device is still called $GPU_DEVFREQ_NAME" \
        || bad "no /sys/class/devfreq/$GPU_DEVFREQ_NAME - the parser would match nothing"
    note "governor=$(cat "/sys/class/devfreq/$GPU_DEVFREQ_NAME/governor" 2>/dev/null) \
polling_interval=$(cat "/sys/class/devfreq/$GPU_DEVFREQ_NAME/polling_interval" 2>/dev/null)ms"

    # The two fields the print format is built from, and the format itself. The parser
    # reads the FORMATTED load, not the raw counters, so it is "load=" that has to exist.
    fmt=$(cat "$TRACEFS/events/devfreq/devfreq_monitor/format" 2>/dev/null)
    has "$fmt" "busy_time"  "the tracepoint still carries busy_time"
    has "$fmt" "total_time" "and total_time"
    has "$fmt" "load=%"     "and prints load=, which is the field the parser reads"

    case "$(rt_status)" in
        active|suspended) ok "the GPU reports a runtime_status ($(rt_status))" ;;
        *) bad "runtime_status reads '$(rt_status)'" ;;
    esac
    eq "$GLOBAL_SAVED" 0 "the global trace buffer has devfreq_monitor off at rest"
}

s_gpubusy() {
    head_ "gpubusy: the metric, where it can actually be measured"
    w_start || { skip_ "cannot create a trace instance here"; return; }

    # The GPU is runtime-suspended ~97% of uptime on this board, and devfreq polling
    # suspends with it - so at rest the tracepoint is silent and the success path is
    # unreachable. Rather than find GPU work to do (which would measure the workload,
    # not qcstats), hold the GPU resumed through runtime PM: the governor then polls at
    # its 50 ms interval and the event fires whatever the GPU is or is not doing.
    # Put back by the restore trap however this exits.
    gpu_force_on
    if ! wait_until 10 '[ "$(rt_status)" = active ]'; then
        skip_ "the GPU will not stay resumed (runtime_status=$(rt_status))"
        gpu_unforce; w_stop; return
    fi
    ok "the GPU can be held resumed, so devfreq polls and the tracepoint fires"
    w_clear
    sleep 1
    n=$(w_count)
    [ "$n" -gt 0 ] && ok "the kernel emits devfreq_monitor for the GPU ($n in 1s)" \
                   || { bad "no devfreq_monitor events while resumed"; gpu_unforce; w_stop; return; }

    w_clear
    before=$(rt_status)
    "$QCSTATS" --gpu-busy --json --count 4 --interval 250 \
        > "$WORK/out" 2> "$WORK/err"
    rc=$?
    after=$(rt_status)
    err=$(cat "$WORK/err")

    eq "$rc" 0 "--gpu-busy exits 0"
    has "$err" "enabled devfreq_monitor in $TRACEFS/instances/qcstats-" \
        "it says what it enabled, and names its own private instance"
    has "$err" "(restored on exit)" "and that it will put it back"
    # The refusal belongs to the other case, and must not appear in this one.
    case "$err" in
        *root-only*) bad "it claimed tracefs is root-only while reading it" ;;
        *) ok "and does not claim tracefs is unreadable" ;;
    esac

    nsamp=$(grep -c '"gpu_busy_pct"' "$WORK/out") || nsamp=0
    eq "$nsamp" 4 "four samples came out"

    vals=$(busy_values "$WORK/out")
    nnull=$(printf '%s\n' "$vals" | grep -c null) || nnull=0
    eq "$nnull" 0 "no sample reports gpu_busy_pct as null"

    # Both endpoints active, so no sample in between could have taken the
    # runtime-suspended path: a value here was parsed from a trace line.
    eq "$before" active "the GPU was active when the run started"
    eq "$after"  active "and still active when it ended"

    seen=$(w_loads)
    note "qcstats reported: $(printf '%s' "$vals" | tr '\n' ' ')"
    note "the kernel reported: $seen"
    bogus=0
    unseen=0
    compared=0
    for v in $vals; do
        case "$v" in
            ''|*[!0-9]*) bogus=$((bogus + 1)); continue ;;
        esac
        if [ "$v" -lt 0 ] || [ "$v" -gt 100 ]; then
            bogus=$((bogus + 1)); continue
        fi
        compared=$((compared + 1))
        case " $seen " in
            *" $v "*) ;;
            *) unseen=$((unseen + 1)) ;;
        esac
    done
    eq "$bogus" 0 "every value is an integer percentage"
    # The strong form: not merely plausible, but one of the numbers the kernel printed in
    # the same window. A parser that read the wrong field would still be in range.
    #
    # Comparing nothing is not agreement. With the parse broken every value is null, every
    # one is counted above instead of here, and this reported a clean pass over an empty
    # loop - which is the one failure mode a test must not have.
    if [ "$compared" = 0 ]; then
        bad "no value could be compared with the kernel's, so nothing here was checked"
    else
        eq "$unseen" 0 "and all $compared of them are numbers the kernel emitted in that window"
    fi

    # Isolation, both directions. The event is reference-counted per instance, which is
    # the whole reason this works in a private instance instead of the global buffer: if
    # qcstats' exit turned the tracepoint off for everyone, an operator's own tracing
    # would stop when a sampler finished.
    eq "$(cat "$WITNESS/events/devfreq/devfreq_monitor/enable")" 1 \
        "another instance's event survives qcstats exiting"
    w_clear
    sleep 1
    n=$(w_count)
    [ "$n" -gt 0 ] && ok "and still records ($n lines after qcstats was gone)" \
                   || bad "the witness stopped recording once qcstats exited"
    eq "$(cat "$TRACEFS/events/devfreq/devfreq_monitor/enable")" "$GLOBAL_SAVED" \
        "the global buffer is as it was"
    eq "$(leftovers)" "" "and no instance of its own is left"

    gpu_unforce
    w_stop
}

s_suspended() {
    head_ "suspended: a silent tracepoint is a fact, not a gap"
    w_start || { skip_ "cannot create a trace instance here"; return; }
    gpu_unforce                     # in case a previous section is still holding it
    if ! wait_until 30 '[ "$(rt_status)" = suspended ]'; then
        skip_ "the GPU is not idle here (runtime_status=$(rt_status)); nothing to measure"
        w_stop; return
    fi
    ok "the GPU suspends when nothing uses it"

    w_clear
    "$QCSTATS" --gpu-busy --json --count 2 --interval 250 \
        > "$WORK/out" 2> "$WORK/err"
    rc=$?
    eq "$rc" 0 "--gpu-busy exits 0 with the GPU asleep"

    n=$(w_count)
    if [ "$n" != 0 ]; then
        skip_ "the GPU woke during the window ($n events), so a 0 could have come from a line"
        w_stop; return
    fi
    ok "the tracepoint stayed silent, so nothing was there to parse"

    vals=$(busy_values "$WORK/out")
    note "qcstats reported: $(printf '%s' "$vals" | tr '\n' ' ')"
    # Counted first, for the same reason as in the gpubusy section: a loop over no values
    # agrees with anything.
    nval=$(printf '%s\n' "$vals" | grep -c .) || nval=0
    eq "$nval" 2 "two samples came out"
    nz=0
    for v in $vals; do [ "$v" = 0 ] || nz=$((nz + 1)); done
    # 0 from runtime_status=suspended, which is a measurement: a powered-down GPU is
    # executing nothing. This is the branch that makes the common case reportable at all
    # - without it every idle sample would be null.
    eq "$nz" 0 "and reports 0 from runtime_status, not null"
    eq "$(leftovers)" "" "nothing is left behind here either"
    w_stop
}

s_teardown() {
    head_ "teardown: what is left behind, per signal"
    eq "$(leftovers)" "" "nothing of ours is in tracefs to begin with"

    "$QCSTATS" --gpu-busy --count 1 --interval 200 >/dev/null 2>"$WORK/err"
    eq "$?" 0 "a normal run exits 0"
    eq "$(leftovers)" "" "and removes its instance on the way out"

    # SIGINT, SIGTERM, SIGHUP are handled: the loop stops at the next tick and the
    # samplers' close() runs, so the restore is the same one a --count run does.
    for sig in INT TERM HUP; do
        "$QCSTATS" --gpu-busy --interval 200 >/dev/null 2>"$WORK/err" &
        KID=$!
        inst=$TRACEFS/instances/qcstats-$KID
        if ! wait_until 10 "[ -d '$inst' ]"; then
            bad "SIG$sig: the instance never appeared ($(cat "$WORK/err"))"
            kill -KILL "$KID" 2>/dev/null; KID=; continue
        fi
        eq "$(cat "$inst/events/devfreq/devfreq_monitor/enable")" 1 \
            "SIG$sig: the event is on while it runs"
        kill -"$sig" "$KID"
        # A handled signal must also END the run - a sampler that keeps going after
        # SIGTERM is a different defect from one that fails to tidy up.
        if wait_until 10 "! kill -0 $KID 2>/dev/null"; then
            ok "SIG$sig: the run stops"
        else
            bad "SIG$sig: still running 10s later"
            kill -KILL "$KID" 2>/dev/null
        fi
        wait "$KID" 2>/dev/null
        KID=
        [ -d "$inst" ] && bad "SIG$sig: $inst is still there" \
                       || ok "SIG$sig: the instance is gone"
    done

    # SIGKILL is the one signal no handler sees, so the instance and the enabled event
    # survive it. That is asserted rather than wished away: it is the state an operator
    # will find, the global buffer is still untouched when they find it, and removing the
    # directory is all it takes to undo - which is why the private instance was worth it.
    "$QCSTATS" --gpu-busy --interval 200 >/dev/null 2>"$WORK/err" &
    KID=$!
    inst=$TRACEFS/instances/qcstats-$KID
    if ! wait_until 10 "[ -d '$inst' ]"; then
        bad "SIGKILL: the instance never appeared ($(cat "$WORK/err"))"
        kill -KILL "$KID" 2>/dev/null; KID=; return
    fi
    kill -KILL "$KID"
    wait "$KID" 2>/dev/null
    KID=
    sleep 1
    [ -d "$inst" ] && ok "SIGKILL leaves the instance behind, as documented" \
                   || bad "SIGKILL: the instance is gone, so something else removed it"
    eq "$(cat "$inst/events/devfreq/devfreq_monitor/enable" 2>/dev/null)" 1 \
        "with the event still enabled - the one leak --gpu-busy can have"
    eq "$(cat "$TRACEFS/events/devfreq/devfreq_monitor/enable")" "$GLOBAL_SAVED" \
        "and the global buffer still untouched, which is what bounds the damage"
    note "cleaning it up: rmdir $inst"
    echo 0 > "$inst/events/devfreq/devfreq_monitor/enable" 2>/dev/null
    rmdir "$inst" 2>/dev/null
    eq "$(leftovers)" "" "a hand cleanup is one rmdir"
}

# ------------------------------------------------------------------ main
[ "${1:-}" = --list ] && { echo "sections: $SECTIONS_ALL"; echo "default:  $SECTIONS_DEFAULT"; exit 0; }
[ "$(id -u)" = 0 ] || { echo "board-check: must run as root - tracefs is 0700" >&2; exit 2; }
command -v "$QCSTATS" >/dev/null 2>&1 \
    || { echo "board-check: '$QCSTATS' not found; set QCSTATS=<path>" >&2; exit 2; }
resolve || { echo "board-check: no tracefs, no devfreq_monitor, or no $GPUDIR - wrong board?" >&2; exit 2; }

trap 'echo; echo "putting the board back..."; restore' EXIT INT TERM

want=${*:-$SECTIONS_DEFAULT}
started=$(date -u +%H:%M:%S)
echo "board-check on $(uname -n), kernel $(uname -r), started $started"
echo "sections: $want"

for sec in $want; do
    case " $SECTIONS_ALL " in
        *" $sec "*) "s_$sec" ;;
        *) echo "board-check: no such section '$sec'" >&2; exit 2 ;;
    esac
done

echo
if [ "$FAILS" = 0 ]; then
    if [ "$SKIPS" -gt 0 ]; then
        echo "board-check: $CHECKS checks ok, $SKIPS SECTION(S) SKIPPED - not a pass of"
        echo "             those sections; see the SKIP line(s) above  ($started -> $(date -u +%H:%M:%S))"
    else
        echo "board-check: $CHECKS checks ok  ($started -> $(date -u +%H:%M:%S))"
    fi
else
    echo "board-check: $CHECKS checks, $FAILS FAILED${SKIPS:+, $SKIPS skipped}  ($started -> $(date -u +%H:%M:%S))"
fi
[ "$FAILS" = 0 ]
