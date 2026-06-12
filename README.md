# OXWM

![Screenshot](ss.png?raw=true)
![Desktop](ss_desktop.png?raw=true)

## OVERVIEW

OXWM (OS-X like Window Manager) is a fork of [MLVWM](https://github.com/morgant/mlvwm)
mixing OS 8 and OS 9 aesthetics. The fork extends the core features with
a minimal desktop manager, hotfixes, and modern system compatibility.

Features:

* Emulation of MacOS 8–9 menu bar & window decorations
* Optional multiple virtual desktops
* **Desktop icon manager** (`oxwm-desktop`) with wallpaper support
* A main menu bar across the top of the screen, with:
  * Configurable global and per-application menus
  * Menu items trigger application functionality via keyboard shortcuts or commands
  * An icon menu which shows all windows and supports:
    * Switching desktops
    * Selecting, hiding, and showing windows
  * A balloon help menu
  * The ability to "swallow" small windows into the menu bar
* Windows which support:
  * Title bars with optional close, zoom, and shade buttons
  * Resize handle
  * Optional double-click to toggle window shade
  * Drag as solid window or just outline
* Balloon help which shows X window information
* Global keyboard shortcuts
* Numerous configuration options to tune functionality

Roadmap:
* Spotlight-like widget
* OS 9 Drawer widget
* Customizable context menu
* Rich Finder options menu

## INSTALLATION

### Dependencies

```bash
sudo pacman -S --needed imake libxpm xorg-server xorg-xinit xorg-fonts-misc \
    xorg-mkfontscale xterm xdg-desktop-portal-gtk xdg-desktop-portal \
    imlib2 libxft
```

Optional for desktop icons:
```bash
sudo pacman -S --needed xdg-utils
```

### Building

Build (uses imake):
```
cd oxwm && make
```

This produces the `oxwm` binary in the project root.

Install (local, no sudo):
```
cp oxwm ~/.local/bin/

gcc -o oxwm-desktop xowm-desktop.c \
    $(pkg-config --cflags --libs xft imlib2) \
    -lX11 -lXpm -lXext -lm
cp oxwm-desktop ~/.local/bin/
```

Or system-wide:
```
sudo make install
```

### Configuration

OXWM reads its config from `~/.oxwmrc` by default. For backward
compatibility with existing MLVWM configs, `~/.mlvwmrc` is also accepted
as a fallback when the OXWM config is not found.

### Fonts

Generate the bitmap font index so the X server can find fonts:

    sudo mkfontdir /usr/share/fonts/misc

Install TTF fonts for desktop icon labels:

    mkdir -p ~/.fonts
    cp customize/Charcoal.ttf customize/MONACO.TTF ~/.fonts/
    fc-cache -f ~/.fonts

Install icon theme for desktop icons:

    mkdir -p ~/.icons
    tar xzf customize/NineIcons48x.tar.gz -C ~/.icons/

### Menubar widgets

Three small X11 widgets ship with oxwm and get Swallowed into the
menubar (add to your `~/.mlvwmrc` or include from `~/.mlvwm/MenuBar`):

* `oxwm-volume` — PulseAudio volume bar. Click to set, drag to scrub,
  scroll wheel to nudge, right-click to mute. Requires `pactl`.
* `oxwm-battery` — reads `/sys/class/power_supply/BAT0/`, shows
  percentage + charge state. Red below 15%, yellow below 30%.
* `oxwm-clock` — `strftime`-style clock. Use `-f` for time format,
  `-F` for an optional second line (e.g. date).

Example entries (these are in `contrib/menu-extras/` and auto-installed
to `~/.mlvwm/MenuExtras/` by `make install`):

    Read .mlvwm/MenuExtras/oxwm-volume
    Read .mlvwm/MenuExtras/oxwm-battery
    Read .mlvwm/MenuExtras/oxwm-clock

### Compositor (picom)

OXWM does not bundle a compositor. For window transparency, rounded
corners, and proper vsync, install [picom](https://github.com/yshui/picom):

    sudo pacman -S --needed picom

A recommended config is shipped in `contrib/picom/picom.conf` and
installed to `~/.config/picom/picom.conf` by `make install`. It
disables fade animations (big CPU saver) and unnecessary shadows on
notifications, conky, etc.

To start picom before oxwm, put this in your `~/.mlvwmrc` or `~/.xinitrc`:

    Exec "picom" picom
    # or in .xinitrc:
    picom --config ~/.config/picom/picom.conf &
    exec oxwm

## CREDITS & Thanks

OXWM is a fork of [MLVWM](https://github.com/morgant/mlvwm), originally
written by TakaC HASEGAWA. The Mac-style UI is inspired by classic
MacOS 8/9. The desktop manager is inspired by
[xfdesktop](https://gitlab.xfce.org/xfdesktop).

Full credit & lineage is documented in [`NOTICE`](./NOTICE).

## LICENSE

This software is distributed as freeware as long as the original copyright
remains in the source code and all documentation. Some files retain their
original MIT license and one file is in the public domain.

Macintosh and MacOS are registered trademarks of Apple, Inc. (née Apple
Computer, Inc.)
