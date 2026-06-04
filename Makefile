VERSION = 0.10.0
CC      = gcc
LDFLAGS =

DEFS = -D_DEFAULT_SOURCE -DVERSION='"$(VERSION)"' -DUSE_LOCALE \
       -DCONFIGNAME='".mlvwmrc"' -DOXWMLIBDIR='"/usr/local/share/oxwm"'

CFLAGS  = -g -Wall $(DEFS)

PKGS_WM   = x11 xpm xext
PKGS_DESK = x11 xpm xext xft imlib2

CFLAGS_WM   = $(CFLAGS) $(shell pkg-config --cflags $(PKGS_WM))
LIBS_WM     = $(shell pkg-config --libs   $(PKGS_WM))

CFLAGS_DESK = $(CFLAGS) $(shell pkg-config --cflags $(PKGS_DESK))
LIBS_DESK   = $(shell pkg-config --libs   $(PKGS_DESK)) -lm

WM_SRCS   = $(wildcard oxwm/*.c)
WM_OBJS   = $(WM_SRCS:.c=.o)
WM_BIN    = oxwm/oxwm

DESK_SRC  = xowm-desktop.c
DESK_BIN  = oxwm-desktop

BINS = $(WM_BIN) $(DESK_BIN)

.PHONY: all clean install

all: $(BINS)

$(WM_BIN): $(WM_OBJS)
	$(CC) -o $@ $(WM_OBJS) $(LIBS_WM) $(LDFLAGS)

oxwm/%.o: oxwm/%.c
	$(CC) $(CFLAGS_WM) -c $< -o $@

$(DESK_BIN): $(DESK_SRC)
	$(CC) $(CFLAGS_DESK) -o $@ $< $(LIBS_DESK) $(LDFLAGS)

clean:
	rm -f $(WM_OBJS) $(WM_BIN) $(DESK_BIN)

install: all
	install -d $(DESTDIR)$(HOME)/.local/bin
	install -m 0755 $(WM_BIN)   $(DESTDIR)$(HOME)/.local/bin/oxwm
	install -m 0755 $(DESK_BIN) $(DESTDIR)$(HOME)/.local/bin/oxwm-desktop
