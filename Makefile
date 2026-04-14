# ─────────────────────────────────────────────────────────────────────────────
# nfm — Flexible ncurses video converter
# ─────────────────────────────────────────────────────────────────────────────
# Targets
#   make           build ./nfm
#   make install   install binary + presets (PREFIX defaults to /usr/local)
#   make deb       build a Debian .deb package  (Linux only)
#   make clean     remove build artefacts
# ─────────────────────────────────────────────────────────────────────────────

TARGET  := nfm
VERSION := 1.0.0
PREFIX  ?= /usr/local

SRCS := src/main.c src/detect.c src/probe.c src/presets.c src/progress.c
OBJS := $(SRCS:.c=.o)

UNAME := $(shell uname)

CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O2 -g -Isrc
LDFLAGS=
LDLIBS = -lm

# ── Platform tweaks ───────────────────────────────────────────────────────────
ifeq ($(UNAME), Darwin)
    # Prefer Homebrew ncurses (newer than the macOS system one) if available
    BREW_NCURSES := $(shell brew --prefix ncurses 2>/dev/null)
    BREW_NCURSES_INC := $(wildcard $(BREW_NCURSES)/include)
    BREW_NCURSES_LIB := $(wildcard $(BREW_NCURSES)/lib)
    ifneq ($(BREW_NCURSES_INC),)
        CFLAGS  += -I$(BREW_NCURSES_INC)
    endif
    ifneq ($(BREW_NCURSES_LIB),)
        LDFLAGS += -L$(BREW_NCURSES_LIB) -Wl,-rpath,$(BREW_NCURSES_LIB)
    endif
    LDLIBS += -lncurses
else
    LDLIBS += -lncurses
endif

# ── Build rules ───────────────────────────────────────────────────────────────
.PHONY: all clean install uninstall deb

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Install ───────────────────────────────────────────────────────────────────
BINDIR     := $(PREFIX)/bin
DATADIR    := $(PREFIX)/share/$(TARGET)
PRESETDIR  := $(DATADIR)/presets
MANDIR     := $(PREFIX)/share/man/man1

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(PRESETDIR)
	install -m 644 presets/*.preset $(DESTDIR)$(PRESETDIR)/

uninstall:
	rm -f  $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DATADIR)

# ── Debian package ────────────────────────────────────────────────────────────
deb: $(TARGET)
	@bash packaging/build_deb.sh

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)
