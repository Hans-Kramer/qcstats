# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
# qcstats - system telemetry sampler for the Arduino Ventuno Q (QCS8275).
# Must build warning-free under gcc *and* clang; `make check` enforces both.

# `?=` will not override make's builtin default for CC, so test its origin.
ifeq ($(origin CC),default)
CC := gcc
endif

# The board is aarch64 and ships no toolchain, so board binaries are cross-built here.
CROSS   ?= aarch64-linux-gnu-gcc

CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra

# The version `--version` reports. One line to change for a release, and
# `make VERSION=0.2.0` overrides it without editing anything. Once this tree is under
# git the literal can become:
#   VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo 0.1.0-pre)
#
# NOTE: this is the SAMPLER's version and is independent of the JSON schema version,
# which is the consumer contract and lives in docs/json-schema-v1.md. The schema is
# bumped only for a breaking change; this moves whenever the code does.
VERSION ?= 0.1.0
CFLAGS  += -DQCSTATS_VERSION=\"$(VERSION)\"

DESTDIR ?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin
mandir  ?= $(prefix)/share/man

TARGET := qcstats
SRCDIR := src
SRCS   := $(SRCDIR)/main.c $(SRCDIR)/io.c $(SRCDIR)/emit.c \
          $(SRCDIR)/cpu.c $(SRCDIR)/mem.c $(SRCDIR)/gpu.c $(SRCDIR)/thermal.c \
          $(SRCDIR)/power.c $(SRCDIR)/fan.c $(SRCDIR)/throttle.c $(SRCDIR)/npu.c \
          $(SRCDIR)/gputrace.c $(SRCDIR)/envid.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(OBJS:.o=.d)

# Behavioural tests. Each tests/t_*.c is a main() of its own, so they are linked one at a
# time against every object except main.o - which brings the program's own main().
# Wildcarded: a new test file is picked up without editing this list, because a test that
# exists and is never run is worse than no test.
TESTSRC := $(wildcard tests/t_*.c)
TESTS   := $(patsubst tests/%.c,tests/bin/%,$(TESTSRC))
TESTOBJS := $(filter-out $(SRCDIR)/main.o,$(OBJS))

.PHONY: all check test tests cross clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

# -MMD -MP generates the header dependencies below. Without them, editing a header
# left stale objects linked against a DIFFERENT struct sample layout - undefined
# behaviour that surfaced as a wrong CPU percentage rather than a crash.
# The version is baked into every object through CFLAGS, and make does not track CFLAGS: a
# bumped VERSION with existing objects produced a binary that REPORTED THE OLD ONE. `make
# cross` cleans first, so the staged board build was never exposed to it; a native build was,
# and a release is defined by exactly this string. Depending on the Makefile is cruder than a
# version stamp file and needs no new machinery: editing it rebuilds thirteen small objects.
$(OBJS): Makefile

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

# Both compilers AND warning-free. Two things this gate got wrong before:
#   - without -Werror a warning printed and `check` still reported success;
#   - with -fsyntax-only the optimiser never runs, so -Wformat-truncation and
#     friends are invisible. A real -O2 compile is required to see them.
check:
	@# One shell, so the summary can name the compilers that actually ran.
	@ran=""; for cc in gcc clang; do \
	    command -v $$cc >/dev/null 2>&1 || { echo "== $$cc not installed, skipped"; continue; }; \
	    echo "== $$cc"; \
	    $$cc $(CFLAGS) -Werror -o /dev/null $(SRCS) || exit 1; \
	    for t in $(TESTSRC); do \
	        $$cc $(CFLAGS) -Werror -c -o /dev/null $$t || exit 1; \
	    done; \
	    ran="$$ran $$cc"; \
	done; \
	[ -n "$$ran" ] || { echo "== no C compiler found - NOTHING was checked"; exit 1; }; \
	echo "== clean under -Wall -Wextra -Werror at -O2:$$ran (sources and tests)"

# Build the tests without running them, which is what a cross build needs: those binaries
# are for the board and this machine cannot execute them.
tests: $(TESTS)

# The behavioural suite. `check` is the compilation gate and stays that; this runs code.
# Each test prints its own line and returns non-zero if anything failed OR if it checked
# nothing at all - see tests/t.h.
# The shell half needs the BINARY, because the flag surface is only reachable through
# argv - the C tests link every object but main.o.
test: $(TESTS) $(TARGET)
	@[ -n "$(TESTS)" ] || { echo "== no tests found in tests/"; exit 1; }
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; \
	 sh tests/sh/run.sh || fail=1; \
	 [ "$$fail" = 0 ] || { echo "== TESTS FAILED"; exit 1; }; \
	 echo "== tests passed"

# Into tests/bin/ so one .gitignore line covers every test binary. A build artifact that
# becomes trackable by being renamed is a trap this tree has already fallen into once.
tests/bin/%: tests/%.c tests/t.h $(TESTOBJS)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) -MMD -MP -o $@ $< $(TESTOBJS) $(LDFLAGS)

# The tests' own header dependencies, so editing qcstats.h rebuilds the test that pins it.
-include $(TESTS:%=%.d)

# Cross-build for the board. 'char' is UNSIGNED on aarch64 and signed on x86, among other
# differences, so a host-only build cannot see a whole class of bug.
#
# The aarch64 OBJECTS are deleted afterwards and only the binary is kept. Objects built for
# the board are not linkable by a host build, and because this Makefile compiles them beside
# the sources, leaving them behind makes the next plain `make` fail with "file in wrong
# format" until someone thinks to run `clean`. The binary that remains IS for the board, so
# run `make` before using qcstats on this machine again.
cross:
	@command -v $(CROSS) >/dev/null 2>&1 || { echo "$(CROSS) not installed"; exit 1; }
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory CC=$(CROSS) all
	@rm -f $(OBJS) $(DEPS)
	@echo "  ./$(TARGET) is now a BOARD binary (aarch64); run 'make' before using it here"

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)
	rm -rf tests/bin

# qcstats is a sampler and never writes to the system, so it needs no
# privileges and no setuid bit.
# The man page is installed uncompressed: man(1) reads either, and compressing it here
# would differ from what the tests check and from what a packager does for themselves.
install: $(TARGET)
	install -d $(DESTDIR)$(bindir)
	install -m 0755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)
	install -d $(DESTDIR)$(mandir)/man1
	install -m 0644 docs/$(TARGET).1 $(DESTDIR)$(mandir)/man1/$(TARGET).1

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(mandir)/man1/$(TARGET).1
