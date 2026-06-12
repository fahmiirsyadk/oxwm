/*
 * oxwm-clock — clock MenuExtra for oxwm
 *
 * Draws the current time (and optionally date) in a small X
 * window suitable for swallowing into the oxwm menubar.
 *
 * Usage: oxwm-clock [-W width] [-H height] [-i interval-ms] [-f fmt] [-F datefmt]
 *
 * Formats: strftime()-style. Default time: "%H:%M", date: "" (no date).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

static Display *dpy;
static Window win;
static GC gc;
static int W = 60, H = 18;
static unsigned long bg_pixel;
static int interval = 1000;
static const char *time_fmt = "%H:%M";
static const char *date_fmt = "";

static Atom a_wm_delete_window;

static void draw(void) {
	Window root;
	int x, y;
	unsigned int w, h, bw, depth;
	XGetGeometry(dpy, win, &root, &x, &y, &w, &h, &bw, &depth);

	XClearWindow(dpy, win);

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);

	char line1[64], line2[64];
	int n1 = strftime(line1, sizeof(line1), time_fmt, tm);
	int n2 = (date_fmt && *date_fmt) ?
		strftime(line2, sizeof(line2), date_fmt, tm) : 0;

	XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));

	/* Default font is usually 6x13 on most X11 systems. Use that
	 * for centering math. The slight off-center is fine for a
	 * menubar widget. */
	int char_w = 6;
	int char_h = 13;

	if (n2 > 0) {
		/* two-line layout */
		int x1 = (w - n1 * char_w) / 2;
		if (x1 < 2) x1 = 2;
		int x2 = (w - n2 * char_w) / 2;
		if (x2 < 2) x2 = 2;
		XDrawString(dpy, win, gc, x1, char_h, line1, n1);
		XDrawString(dpy, win, gc, x2, h - 2, line2, n2);
	} else {
		/* single-line, centered horizontally and vertically */
		int x_pos = (w - n1 * char_w) / 2;
		if (x_pos < 2) x_pos = 2;
		int y_pos = (h + char_h) / 2 - 2;
		XDrawString(dpy, win, gc, x_pos, y_pos, line1, n1);
	}

	XFlush(dpy);
}

static void handle_event(XEvent *ev) {
	if (ev->type == Expose) draw();
}

static void usage(const char *p) {
	fprintf(stderr,
	        "usage: %s [-W w] [-H h] [-i ms] [-f timefmt] [-F datefmt]\n", p);
	exit(1);
}

int main(int argc, char **argv) {
	int c;
	while ((c = getopt(argc, argv, "W:H:i:f:F:")) != -1) {
		switch (c) {
		case 'W': W = atoi(optarg); break;
		case 'H': H = atoi(optarg); break;
		case 'i': interval = atoi(optarg); break;
		case 'f': time_fmt = optarg; break;
		case 'F': date_fmt = optarg; break;
		default: usage(argv[0]);
		}
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "oxwm-clock: cannot open display\n");
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
		fprintf(stderr, "oxwm-clock: XCreateWindow failed\n");
		return 1;
	}

	gc = XCreateGC(dpy, win, 0, NULL);
	XStoreName(dpy, win, "oxwm-clock");
	{
		XClassHint ch = { "oxwm-clock", "oxwm-clock" };
		XSetClassHint(dpy, win, &ch);
	}
	a_wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &a_wm_delete_window, 1);

	XMapWindow(dpy, win);
	draw();

	int last_min = -1;
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
			time_t now = time(NULL);
			struct tm *tm = localtime(&now);
			if (tm->tm_min != last_min) {
				last_min = tm->tm_min;
				draw();
			}
		}
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
