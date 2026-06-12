/*
 * oxwm-volume — volume bar MenuExtra for oxwm
 *
 * Draws a volume bar in a small X window suitable for swallowing
 * into the oxwm menubar. Left-click toggles mute, scroll wheel
 * adjusts volume.
 *
 * Reads/writes PulseAudio via `pactl`. Designed to be tiny — no
 * libpulse, no Xft, just Xlib and CoreX11 fonts.
 *
 * Usage: oxwm-volume [-W width] [-H height] [-i interval-ms]
 *
 * Environment:
 *   XDG_CACHE_HOME/oxwm-volume     caches last-seen volume
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

static Display *dpy;
static Window win;
static GC gc;
static int W = 100, H = 18;
static unsigned long bg_pixel;
static int interval = 200;

static int vol = 0;     /* 0..100 */
static int muted = 0;
static int dragging = 0;

static Atom a_wm_delete_window;
static Atom a_wm_take_focus;

static void get_volume(void) {
	FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null",
	                 "r");
	if (!fp) return;
	char buf[256];
	if (fgets(buf, sizeof(buf), fp)) {
		/* "Volume: front-left: 35958 /  55% / -15.64 dB, ..." */
		char *p = strchr(buf, '/');
		if (p) {
			p++;
			while (*p == ' ') p++;
			vol = atoi(p);
			if (vol < 0) vol = 0;
			if (vol > 100) vol = 100;
		}
	}
	pclose(fp);

	muted = 0;
	fp = popen("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", "r");
	if (!fp) return;
	if (fgets(buf, sizeof(buf), fp)) {
		if (strstr(buf, "yes")) muted = 1;
	}
	pclose(fp);
}

static void set_volume(int new_vol) {
	if (new_vol < 0) new_vol = 0;
	if (new_vol > 100) new_vol = 100;
	char cmd[128];
	snprintf(cmd, sizeof(cmd),
	         "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null",
	         new_vol);
	system(cmd);
	vol = new_vol;
	if (muted) {
		system("pactl set-sink-mute @DEFAULT_SINK@ 0 2>/dev/null");
		muted = 0;
	}
}

static void toggle_mute(void) {
	char *cmd = muted ?
		"pactl set-sink-mute @DEFAULT_SINK@ 0" :
		"pactl set-sink-mute @DEFAULT_SINK@ 1";
	system(cmd);
	muted = !muted;
}

static void draw(void) {
	XClearWindow(dpy, win);
	Window root;
	int x, y;
	unsigned int w, h, bw, depth;
	XGetGeometry(dpy, win, &root, &x, &y, &w, &h, &bw, &depth);

	/* For a tall widget (>= 30px): bar on top, text below.
	 * For a short widget (default 18px menubar height): text-only,
	 * a colored bar drawn OVER the text background as the indicator. */
	int pad = 3;
	int text_y = h - 4;
	int text_w_estimate = 24;  /* approx width of "100%" or "MUTE" */

	if (h >= 30) {
		int bar_x = pad, bar_y = 4;
		int bar_w = w - 2 * pad;
		int bar_h = h / 2 - 4;
		if (bar_h < 2) bar_h = 2;
		XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
		XDrawRectangle(dpy, win, gc, bar_x, bar_y, bar_w, bar_h);
		int fill_w = (bar_w - 2) * vol / 100;
		if (fill_w > 0) {
			if (muted) {
				XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
			} else {
				const char *cname = (vol > 50) ? "#00a000" :
				                    (vol > 20) ? "#c0a000" : "#c02020";
				XColor c, exact;
				Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
				if (XAllocNamedColor(dpy, cmap, cname, &c, &exact))
					XSetForeground(dpy, gc, c.pixel);
				else
					XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
			}
			XFillRectangle(dpy, win, gc, bar_x + 1, bar_y + 1,
			               fill_w, bar_h - 2);
		}
	} else {
		/* Short widget: paint a colored bar as a background hint.
		 * The bar's height equals widget height; width proportional
		 * to volume. Text is drawn on top. */
		int fill_w = (w - text_w_estimate - 2) * vol / 100;
		if (fill_w > 0) {
			if (muted) {
				XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
			} else {
				const char *cname = (vol > 50) ? "#00a000" :
				                    (vol > 20) ? "#c0a000" : "#c02020";
				XColor c, exact;
				Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
				if (XAllocNamedColor(dpy, cmap, cname, &c, &exact))
					XSetForeground(dpy, gc, c.pixel);
				else
					XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
			}
			XFillRectangle(dpy, win, gc, 1, 1, fill_w, h - 2);
			/* clear the right portion to keep the text area clean */
			XSetForeground(dpy, gc, bg_pixel);
			XFillRectangle(dpy, win, gc, fill_w + 1, 1,
			               w - fill_w - 1, h - 2);
		}
	}

	/* percentage label, right-aligned for the short-widget case */
	char label[16];
	snprintf(label, sizeof(label), muted ? "MUTE" : "%d%%", vol);
	XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
	int label_x;
	if (h >= 30) {
		/* centred below the bar */
		label_x = (w - text_w_estimate) / 2;
	} else {
		/* right-aligned so the bar fills the left side */
		label_x = w - text_w_estimate - 2;
	}
	XDrawString(dpy, win, gc, label_x, text_y, label, strlen(label));

	XFlush(dpy);
}

static int x_position_to_vol(int x) {
	int pad = 2;
	int bar_w = W - 2 * pad;
	int v = (x - pad) * 100 / (bar_w - 2);
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	return v;
}

static void handle_event(XEvent *ev) {
	switch (ev->type) {
	case Expose:
		draw();
		break;
	case ButtonPress:
		if (ev->xbutton.button == 1) {
			dragging = 1;
			set_volume(x_position_to_vol(ev->xbutton.x));
			draw();
		} else if (ev->xbutton.button == 3) {
			toggle_mute();
			draw();
		}
		break;
	case ButtonRelease:
		if (ev->xbutton.button == 1) dragging = 0;
		break;
	case MotionNotify:
		if (dragging) {
			set_volume(x_position_to_vol(ev->xmotion.x));
			draw();
		}
		break;
	case EnterNotify:
	case LeaveNotify:
		break;
	}
}

static void usage(const char *p) {
	fprintf(stderr, "usage: %s [-W width] [-H height] [-i interval-ms]\n", p);
	exit(1);
}

int main(int argc, char **argv) {
	int c;
	while ((c = getopt(argc, argv, "W:H:i:")) != -1) {
		switch (c) {
		case 'W': W = atoi(optarg); break;
		case 'H': H = atoi(optarg); break;
		case 'i': interval = atoi(optarg); break;
		default: usage(argv[0]);
		}
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "oxwm-volume: cannot open display\n");
		return 1;
	}
	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	XSetWindowAttributes wa;
	/* Match the menubar's apparent background (the menubar itself
	 * uses WhitePixel with a DrawShadowBox overlay, so widgets need
	 * a light gray to blend in). */
	{
		XColor c, exact;
		Colormap cmap = DefaultColormap(dpy, screen);
		if (XAllocNamedColor(dpy, cmap, "#dddddd", &c, &exact))
			wa.background_pixel = c.pixel;
		else
			wa.background_pixel = WhitePixel(dpy, screen);
	}
	bg_pixel = wa.background_pixel;
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	                ButtonMotionMask | EnterWindowMask | LeaveWindowMask;
	wa.backing_store = WhenMapped;
	win = XCreateWindow(dpy, root, 0, 0, W, H, 0,
	                    CopyFromParent, InputOutput,
	                    DefaultVisual(dpy, screen),
	                    CWBackPixel | CWEventMask | CWBackingStore, &wa);
	if (!win) {
		fprintf(stderr, "oxwm-volume: XCreateWindow failed\n");
		return 1;
	}

	gc = XCreateGC(dpy, win, 0, NULL);

	/* window title + WM_CLASS for identification when swallowed */
	XStoreName(dpy, win, "oxwm-volume");
	XSetIconName(dpy, win, "vol");
	{
		XClassHint ch = { "oxwm-volume", "oxwm-volume" };
		XSetClassHint(dpy, win, &ch);
	}

	/* handle WM_DELETE_WINDOW so swallow can reparent cleanly */
	a_wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	a_wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	XSetWMProtocols(dpy, win, &a_wm_delete_window, 1);

	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);

	XMapWindow(dpy, win);
	get_volume();
	draw();

	/* poll: get current volume + handle events */
	int last_vol = -1, last_muted = -1;
	while (1) {
		if (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == ClientMessage &&
			    (Atom)ev.xclient.data.l[0] == a_wm_delete_window)
				break;
			handle_event(&ev);
		} else {
			usleep(interval * 1000);
			if (!dragging) {
				get_volume();
				if (vol != last_vol || muted != last_muted) {
					last_vol = vol;
					last_muted = muted;
					draw();
				}
			}
		}
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
