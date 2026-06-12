/*
 * oxwm-battery — battery indicator MenuExtra for oxwm
 *
 * Reads /sys/class/power_supply/BAT0/ and renders the battery
 * percentage and charge state in a small X window suitable for
 * swallowing into the oxwm menubar.
 *
 * Usage: oxwm-battery [-W width] [-H height] [-i interval-ms] [-b bat]
 *
 * Environment:
 *   Reads sysfs (no environment variables).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

static Display *dpy;
static Window win;
static GC gc;
static int W = 50, H = 18;
static unsigned long bg_pixel;
static int interval = 5000;
static const char *bat_path = "/sys/class/power_supply/BAT0";

static Atom a_wm_delete_window;

static int read_int(const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp) return -1;
	int v = -1;
	if (fscanf(fp, "%d", &v) != 1) v = -1;
	fclose(fp);
	return v;
}

static void read_battery(int *capacity, int *charging, int *present) {
	char path[256];
	snprintf(path, sizeof(path), "%s/capacity", bat_path);
	*capacity = read_int(path);
	snprintf(path, sizeof(path), "%s/status", bat_path);
	FILE *fp = fopen(path, "r");
	*charging = 0;
	*present = 1;
	if (!fp) { *present = 0; return; }
	char buf[64];
	if (fgets(buf, sizeof(buf), fp)) {
		if (strstr(buf, "Charging") || strstr(buf, "Full"))
			*charging = 1;
	}
	fclose(fp);
}

static void draw(void) {
	Window root;
	int x, y;
	unsigned int w, h, bw, depth;
	XGetGeometry(dpy, win, &root, &x, &y, &w, &h, &bw, &depth);

	int capacity, charging, present;
	read_battery(&capacity, &charging, &present);

	XClearWindow(dpy, win);

	if (!present) {
		XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
		XDrawString(dpy, win, gc, 2, h - 4, "N/A", 3);
		XFlush(dpy);
		return;
	}

	/* Short menubar widget: use a small battery body and overlay
	 * percentage text. The whole widget is only 18px tall so we
	 * have very little vertical room. */
	int bx, by, bw_, bh;
	if (h >= 30) {
		/* tall widget: body in upper half, label below */
		bx = 4;
		by = 4;
		bw_ = w - 12;
		bh = h / 2 - 2;
	} else {
		/* short widget: body fills vertical extent, no label below */
		bx = 4;
		by = 3;
		bw_ = w - 16;
		bh = h - 6;
		if (bh < 4) bh = 4;
	}

	XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
	XDrawRectangle(dpy, win, gc, bx, by, bw_, bh);
	/* battery tip */
	XFillRectangle(dpy, win, gc, bw_ + 2, by + 2, 2, bh - 4);

	/* fill */
	int fill_w = (bw_ - 2) * capacity / 100;
	const char *cname;
	if (charging) cname = "#00a000";
	else if (capacity < 15) cname = "#c02020";
	else if (capacity < 30) cname = "#c0a000";
	else cname = "#404040";
	XColor c, exact;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (XAllocNamedColor(dpy, cmap, cname, &c, &exact))
		XSetForeground(dpy, gc, c.pixel);
	if (fill_w > 0 && !charging)
		XFillRectangle(dpy, win, gc, bx + 1, by + 1, fill_w, bh - 2);

	/* label: for tall widgets below the body, for short widgets
	 * overlaid on the body with a white rect to make it readable. */
	char label[8];
	snprintf(label, sizeof(label), "%d%%%c", capacity, charging ? '+' : ' ');
	XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
	if (h >= 30) {
		XDrawString(dpy, win, gc, 2, h - 4, label, strlen(label));
	} else {
		/* clear a small rect (in bg colour) for legibility, then draw text */
		XSetForeground(dpy, gc, bg_pixel);
		XFillRectangle(dpy, win, gc, bx, by, bw_ + 4, bh);
		XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
		XDrawString(dpy, win, gc, bx + 1, by + bh - 2,
		            label, strlen(label));
	}

	XFlush(dpy);
}

static void handle_event(XEvent *ev) {
	(void)ev;
	draw();
}

static void usage(const char *p) {
	fprintf(stderr,
	        "usage: %s [-W width] [-H height] [-i interval-ms] [-b batpath]\n",
	        p);
	exit(1);
}

int main(int argc, char **argv) {
	int c;
	while ((c = getopt(argc, argv, "W:H:i:b:")) != -1) {
		switch (c) {
		case 'W': W = atoi(optarg); break;
		case 'H': H = atoi(optarg); break;
		case 'i': interval = atoi(optarg); break;
		case 'b': bat_path = optarg; break;
		default: usage(argv[0]);
		}
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "oxwm-battery: cannot open display\n");
		return 1;
	}
	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	XSetWindowAttributes wa;
	/* Match the menubar's apparent background. */
	{
		XColor c, exact;
		Colormap cmap = DefaultColormap(dpy, screen);
		if (XAllocNamedColor(dpy, cmap, "#dddddd", &c, &exact))
			wa.background_pixel = c.pixel;
		else
			wa.background_pixel = WhitePixel(dpy, screen);
	}
	bg_pixel = wa.background_pixel;
	wa.event_mask = ExposureMask;
	wa.backing_store = WhenMapped;
	win = XCreateWindow(dpy, root, 0, 0, W, H, 0,
	                    CopyFromParent, InputOutput,
	                    DefaultVisual(dpy, screen),
	                    CWBackPixel | CWEventMask | CWBackingStore, &wa);
	if (!win) {
		fprintf(stderr, "oxwm-battery: XCreateWindow failed\n");
		return 1;
	}

	gc = XCreateGC(dpy, win, 0, NULL);
	XStoreName(dpy, win, "oxwm-battery");
	{
		XClassHint ch = { "oxwm-battery", "oxwm-battery" };
		XSetClassHint(dpy, win, &ch);
	}
	a_wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &a_wm_delete_window, 1);

	XMapWindow(dpy, win);
	draw();

	int last_cap = -1, last_charging = -1;
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
			int capacity, charging, present;
			read_battery(&capacity, &charging, &present);
			if (capacity != last_cap || charging != last_charging) {
				last_cap = capacity;
				last_charging = charging;
				draw();
			}
		}
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
