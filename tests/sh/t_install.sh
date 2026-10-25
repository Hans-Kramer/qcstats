# SPDX-License-Identifier: MPL-2.0
# SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
# SPDX-FileContributor: Claude Opus 5 (Anthropic), paired on the behavioural suite
#
# `make install` and `make uninstall`, into a staging directory.
#
# There is no installer script here - the Makefile IS the installer - and until the man
# page it put one file in one place, which is hard to get wrong. Two files in two places
# with a mode each is not, and an uninstall that forgets one of them leaves the system
# holding a file nobody will think to look for.
#
# DESTDIR and prefix are honoured for exactly this: nothing below touches the real system.
. tests/sh/t.sh

STAGE=$(mktemp -d) || exit 2
trap 'rm -rf "$STAGE"' EXIT

command -v make >/dev/null 2>&1 || t_skip "no make"
[ -f qcstats ] || t_skip "qcstats is not built"

mi() { make install DESTDIR="$STAGE" prefix=/usr >/dev/null 2>&1; }
mu() { make uninstall DESTDIR="$STAGE" prefix=/usr >/dev/null 2>&1; }

BIN=$STAGE/usr/bin/qcstats
MAN=$STAGE/usr/share/man/man1/qcstats.1

mi
t_ok "$?" "make install succeeds"

[ -f "$BIN" ] && t_ok 0 "the binary lands in bindir" || t_ok 1 "no $BIN"
[ -f "$MAN" ] && t_ok 0 "the man page lands in mandir" || t_ok 1 "no $MAN"
t_eq "$(stat -c %a "$BIN" 2>/dev/null)" 755 "the binary is 0755"
t_eq "$(stat -c %a "$MAN" 2>/dev/null)" 644 "the man page is 0644"
# Installed, not just created: a zero-byte file of the right name would satisfy -f.
cmp -s qcstats "$BIN" && t_ok 0 "the installed binary is the one that was built" \
                      || t_ok 1 "the installed binary differs from ./qcstats"
cmp -s docs/qcstats.1 "$MAN" && t_ok 0 "and the man page is the one in docs/" \
                             || t_ok 1 "the installed man page differs from docs/qcstats.1"
# It has to be a man page, not merely a file: NAME is what apropos and whatis read.
t_has "$(cat "$MAN" 2>/dev/null)" ".SH NAME" "the installed page has a NAME section"

# Installing over an existing install is what an upgrade does.
mi
t_ok "$?" "a second install over the first succeeds"

mu
t_ok "$?" "make uninstall succeeds"
[ -f "$BIN" ] && t_ok 1 "$BIN survived uninstall" || t_ok 0 "the binary is gone"
[ -f "$MAN" ] && t_ok 1 "$MAN survived uninstall" || t_ok 0 "the man page is gone"
# The whole point: nothing of ours is left anywhere under the staged root. Counting files
# rather than naming them is what catches a file this test does not know about - the next
# thing install grows will be caught here without anyone remembering to add a check.
left=$(find "$STAGE" -type f | wc -l)
t_eq "$left" 0 "and no file at all is left in the staged tree"

# Uninstalling what is not there must not fail: a failed uninstall in a package's
# postrm leaves the package half-removed.
mu
t_ok "$?" "uninstalling twice is not an error"

t_summary
