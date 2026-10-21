#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
# SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
#
# The flag surface, driven through argv against the built binary.
#
# The C tests link every object except main.o, so the argv layer sat outside them: of 12
# options, 3 were ever run, and two of the three output formats had no test at all. A --csv
# that emitted nothing would have passed the whole suite.
#
# QCSTATS_SYSROOT points every read at the captured tree, so this needs no board. qcstats
# writes nothing, so the fixture is used in place rather than copied.
set -u

. "$(dirname "$0")/t.sh"

BIN=./qcstats
[ -x "$BIN" ] || { echo "    t_cli: $BIN not built" >&2; exit 2; }

FX=$(ls -d tests/fixtures/monza-* 2>/dev/null | head -1)
[ -n "$FX" ] || { echo "    t_cli: no captured fixture" >&2; exit 2; }
export QCSTATS_SYSROOT=$FX

RT=$(mktemp -d /tmp/t_cli.XXXXXX) || exit 2
trap 'rm -rf "$RT"' EXIT INT TERM

# run <args...> - stdout in $out, status in $rc. stderr is kept separate here, because the
# sysroot notice goes there on every run and would pollute every output assertion.
run() { out=$("$BIN" "$@" 2>"$RT/err"); rc=$?; err=$(cat "$RT/err"); }

# --- help and version short-circuit the parse loop ------------------------------------

run --help
t_eq "$rc" 0 "--help exits 0"
t_has "$out" "usage: " "and prints the usage"

run -h
t_eq "$rc" 0 "-h exits 0 too"

run -V
t_eq "$rc" 0 "-V exits 0"
t_has "$out" "qcstats " "and names the binary"
# The schema version is the contract with qcctl, so -V states it and not just the release.
t_has "$out" "json-schema" "and reports the schema version"

# The loop acts on arguments in order, so which of two bad-and-good arguments comes first
# decides the exit code. Both directions, because only one of them is obvious.
run --help --count abc
t_eq "$rc" 0 "--help before a bad value wins"
run --count abc --help
t_eq "$rc" 2 "a bad value before --help still exits 2"

# --- refusals, which are all exit 2 ---------------------------------------------------

run --bogus
t_eq "$rc" 2 "an unknown argument exits 2"
t_has "$err" "unknown argument" "and says so on stderr"
t_has "$err" "usage: " "with the usage, also on stderr"

run --count
t_eq "$rc" 2 "--count with no value exits 2"
run --count 0
t_eq "$rc" 2 "--count 0 exits 2"
run --count abc
t_eq "$rc" 2 "--count of a non-number exits 2"
run --interval 0
t_eq "$rc" 2 "--interval 0 exits 2"
run --interval abc
t_eq "$rc" 2 "--interval of a non-number exits 2"
run --zones
t_eq "$rc" 2 "--zones with no list exits 2"
run 7x
t_eq "$rc" 2 "a positional interval that is not a number exits 2"

# Two ways of saying which zones, which cannot both be obeyed.
#
# --count 1 on this and the three below, even though each is expected to exit before it ever
# samples: without it, a regression that stops refusing leaves qcstats sampling forever and
# the test HANGS rather than failing. A refusal test must not depend on the refusal to
# terminate - learned here, by mutating the validation away and waiting.
run --count 1 --zones cpuss-0-0 --all-zones
t_eq "$rc" 2 "--zones with --all-zones exits 2"
t_has "$err" "mutually exclusive" "and says they are exclusive"

# The list's own validation, which has two dedicated refusals and neither was reached: the
# earlier check only passed --zones with no argument at all, a different code path.
run --count 1 --zones "cpuss-0-0,,ddrss-0"
t_eq "$rc" 2 "an empty entry in the list exits 2"
t_has "$err" "empty entry" "and says which fault it is"
run --count 1 --zones ","
t_eq "$rc" 2 "a list that is only a separator exits 2"

# More names than there are slots. 64 is the bound; 71 is comfortably past it.
many=$(awk 'BEGIN{for(i=1;i<=70;i++) printf "z%d,", i; print "z71"}')
run --count 1 --zones "$many"
t_eq "$rc" 2 "more names than the limit exits 2"
t_has "$err" "at most" "and names the limit"

run --logfile "$RT/no/such/dir/f"
t_eq "$rc" 2 "an unwritable --logfile exits 2 before sampling"

# --- the output formats ----------------------------------------------------------------

# JSON is a meta line and then one line per sample; the meta line is what qcctl keys on.
run --json --count 1
t_eq "$rc" 0 "--json exits 0"
t_eq "$(echo "$out" | wc -l)" 2 "and is a meta line plus one sample"
case "$(echo "$out" | head -1)" in '{"v":1,"meta":'*) t_ok 0 "the meta line comes first" ;;
    *) t_ok 1 "the first line was not the meta line" ;; esac
case "$(echo "$out" | tail -1)" in '{"v":1,"t":'*) t_ok 0 "and the sample carries the schema and a timestamp" ;;
    *) t_ok 1 "the sample line did not start with the schema and a timestamp" ;; esac

run --csv --count 1
t_eq "$rc" 0 "--csv exits 0"
t_eq "$(echo "$out" | wc -l)" 2 "and is a header plus one row"
t_has "$(echo "$out" | head -1)" "t,ram_used_mb" "the header names the columns"
t_hasnt "$out" '{"v"' "and there is no JSON in it"

run --plain --count 1
t_eq "$rc" 0 "--plain exits 0"
t_has "$out" "RAM " "and is the human form"
t_hasnt "$out" '{"v"' "with no JSON in it"

run --count 1
t_has "$out" "RAM " "no format flag means --plain"

# One `mode` variable, assigned in argv order, so the last format given wins. Both ways.
run --json --csv --count 1
t_has "$(echo "$out" | head -1)" "t,ram_used_mb" "--json then --csv is CSV"
run --csv --json --count 1
case "$(echo "$out" | head -1)" in '{"v":1,"meta":'*) t_ok 0 "--csv then --json is JSON" ;;
    *) t_ok 1 "--csv then --json did not produce JSON" ;; esac

run --json --count 3
t_eq "$(echo "$out" | wc -l)" 4 "--count 3 is three samples after the meta line"

# --- --logfile ------------------------------------------------------------------------

run --json --count 1 --logfile "$RT/log"
t_eq "$rc" 0 "--logfile exits 0"
t_eq "$out" "" "and stdout carries nothing"
t_ok "$([ -s "$RT/log" ] && echo 0 || echo 1)" "the samples went to the file"
case "$(head -1 "$RT/log")" in '{"v":1,"meta":'*) t_ok 0 "and the file holds the stream" ;;
    *) t_ok 1 "the logfile did not hold the stream" ;; esac

# --- zone selection -------------------------------------------------------------------

run --json --count 1
t_has "$out" '"cpuss-0-0":' "the default zone set includes the CPU zone"

run --json --count 1 --zones cpuss-0-0
t_has "$out" '"cpuss-0-0":' "--zones keeps the zone it names"
t_hasnt "$out" '"ddrss-0":' "and drops the ones it does not"

# A name no zone has is not an error: the selection is a filter, and an empty result is a
# truthful answer. Worth pinning because the opposite is just as defensible a design.
run --json --count 1 --zones nosuchzone
t_eq "$rc" 0 "a zone name nothing matches is not an error"
t_has "$out" '"thermal_c":{}' "and reports no temperatures at all"

run --json --count 1 --all-zones
t_has "$out" '"aoss-0":' "--all-zones reaches zones the default set omits"

# --- the two flags that ask for more than a fixture has -------------------------------

# The tracepoint is an extra, not the point of the tool, so whether tracefs is reachable or
# not it must exit 0 and keep sampling. That part is asserted unconditionally.
#
# The REFUSAL MESSAGE is not unconditional, and asserting it as though it were would fail a
# correct binary wherever tracefs can be read - as root in a container, or on a distro that
# grants a group access to it. So the branch is decided the same way the code decides it:
# access(R_OK|X_OK) on the two candidate mounts, which is what gputrace.c probes.
run --json --count 1 --gpu-busy
t_eq "$rc" 0 "--gpu-busy exits 0 whether or not tracefs can be read"
t_has "$out" '"v":1' "and keeps emitting the stream"
t_has "$out" '"gpu_busy_pct"' "with the field present either way - absent reads null"

if { [ -r /sys/kernel/tracing ] && [ -x /sys/kernel/tracing ]; } ||
   { [ -r /sys/kernel/debug/tracing ] && [ -x /sys/kernel/debug/tracing ]; }; then
    t_hasnt "$err" "root-only" "tracefs is reachable here, so it does not claim otherwise"
else
    t_has "$err" "root-only" "and where it is not, it says tracefs is root-only"
fi

# --npu enables a probe this fixture has nothing for. Accepted, and the field stays null -
# which is Schema rule 1 doing its job rather than a flag doing nothing.
run --json --count 1 --npu
t_eq "$rc" 0 "--npu is accepted"
t_has "$out" '"npu":null' "and an absent NPU reads null, not 0"

# --- the sysroot is reported, not silent ----------------------------------------------

run --json --count 1
t_has "$err" "QCSTATS_SYSROOT" "a redirected read says so on stderr"
t_has "$err" "$FX" "and names the tree"

t_summary
