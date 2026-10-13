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
VERSION ?= 0.1.0-pre
CFLAGS  += -DQCSTATS_VERSION=\"$(VERSION)\"

DESTDIR ?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin

TARGET := qcstats
SRCDIR := src
SRCS   := $(SRCDIR)/main.c $(SRCDIR)/io.c $(SRCDIR)/emit.c \
          $(SRCDIR)/cpu.c $(SRCDIR)/mem.c $(SRCDIR)/gpu.c $(SRCDIR)/thermal.c \
          $(SRCDIR)/power.c $(SRCDIR)/fan.c $(SRCDIR)/throttle.c $(SRCDIR)/npu.c \
          $(SRCDIR)/gputrace.c $(SRCDIR)/envid.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(OBJS:.o=.d)

.PHONY: all check cross clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

# -MMD -MP generates the header dependencies below. Without them, editing a header
# left stale objects linked against a DIFFERENT struct sample layout - undefined
# behaviour that surfaced as a wrong CPU percentage rather than a crash.
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
	    ran="$$ran $$cc"; \
	done; \
	[ -n "$$ran" ] || { echo "== no C compiler found - NOTHING was checked"; exit 1; }; \
	echo "== clean under -Wall -Wextra -Werror at -O2:$$ran"

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

# qcstats is a sampler and never writes to the system, so it needs no
# privileges and no setuid bit.
install: $(TARGET)
	install -d $(DESTDIR)$(bindir)
	install -m 0755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
