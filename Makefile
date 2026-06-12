VERSION = 0.10.0
CC      = gcc
LDFLAGS =

DEFS = -D_DEFAULT_SOURCE -DVERSION='"$(VERSION)"' -DUSE_LOCALE \
       -DCONFIGNAME='".mlvwmrc"' -DOXWMLIBDIR='"/usr/local/share/oxwm"'

CFLAGS  = -g -Wall $(DEFS)

PKGS_WM   = x11 xpm xext xft imlib2

CFLAGS_WM   = $(CFLAGS) $(shell pkg-config --cflags $(PKGS_WM))
LIBS_WM     = $(shell pkg-config --libs   $(PKGS_WM)) -lm

WM_SRCS   = $(wildcard oxwm/*.c)
WM_OBJS   = $(WM_SRCS:.c=.o)
WM_BIN    = oxwm/oxwm

# Menu-bar widgets (small standalone X11 programs that get Swallowed
# into the menubar). Each is built separately; they don't need
# imlib2/xft, just x11.
PKGS_WIDGET = x11
CFLAGS_WIDGET = $(CFLAGS) $(shell pkg-config --cflags $(PKGS_WIDGET))
LIBS_WIDGET   = $(shell pkg-config --libs   $(PKGS_WIDGET))

WIDGET_SRCS = $(wildcard oxwm/widgets/*.c)
WIDGET_BINS = $(patsubst oxwm/widgets/%.c,oxwm/widgets/%,$(WIDGET_SRCS))

BINS = $(WM_BIN) $(WIDGET_BINS)

.PHONY: all clean install

all: $(BINS)

$(WM_BIN): $(WM_OBJS)
	$(CC) -o $@ $(WM_OBJS) $(LIBS_WM) $(LDFLAGS)

oxwm/%.o: oxwm/%.c
	$(CC) $(CFLAGS_WM) -c $< -o $@

oxwm/widgets/%: oxwm/widgets/%.c
	$(CC) $(CFLAGS_WIDGET) -o $@ $< $(LIBS_WIDGET) $(LDFLAGS)

clean:
	rm -f $(WM_OBJS) $(WM_BIN) $(WIDGET_BINS)

install: all
	install -d $(DESTDIR)$(HOME)/.local/bin
	install -m 0755 $(WM_BIN) $(DESTDIR)$(HOME)/.local/bin/oxwm
	install -m 0755 $(WIDGET_BINS) $(DESTDIR)$(HOME)/.local/bin/
	install -d $(DESTDIR)$(HOME)/.config/picom
	install -m 0644 contrib/picom/picom.conf $(DESTDIR)$(HOME)/.config/picom/picom.conf
	install -d $(DESTDIR)$(HOME)/.mlvwm/MenuExtras
	install -m 0644 contrib/menu-extras/* $(DESTDIR)$(HOME)/.mlvwm/MenuExtras/
