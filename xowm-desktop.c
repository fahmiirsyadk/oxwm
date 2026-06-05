/* oxwm-desktop — XFCE-style desktop icons + wallpaper for OXWM
 *
 * Originally developed for MLVWM (see ../NOTICE for credits).  Renamed
 * to oxwm-desktop as part of the OXWM fork.
 *
 * Architecture (inspired by xfdesktop):
 *   1. ONE fullscreen desktop window (override_redirect, child of root)
 *   2. Wallpaper rendered via Imlib2 → Pixmap → desktop window background
 *   3. _XROOTPMAP_ID + ESETROOT_PMAP_ID set on root for compatibility
 *   4. Icons are DRAWN directly onto the desktop window (not separate windows)
 *   5. All mouse events handled on the desktop window with hit-testing
 *   6. Icons arranged column-major from RIGHT edge, filling DOWN
 *
 * Config: ~/.oxwm/desktop.conf (with fallback to ~/.mlvwm/desktop.conf)
 *   icon_name, Label Text, shell command
 *
 * Options:
 *   oxwm-desktop                   icons only (no wallpaper)
 *   oxwm-desktop cfgfile           custom config
 *   oxwm-desktop -bg image.png     fill wallpaper (default)
 *   oxwm-desktop -bg fill:img      same as above
 *   oxwm-desktop -bg scale:img     scale to fit (letterbox)
 *   oxwm-desktop -bg center:img    centered, no scaling
 *   oxwm-desktop -bg tile:img      repeated tile
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
#include <Imlib2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#define ICON_SIZE     48
#define ICON_WIN_W    80
#define ICON_WIN_H    92   /* 88 content + 4 for shadow descent overflow */
#define LABEL_MAX_H   36
#define LABEL_LINE_H  12
#define MAX_LINES      3
#define PAD            4
#define MARGIN_RIGHT   20
#define MARGIN_TOP     36   /* menu bar 20 + 16 pad */
#define COL_W         90
#define ROW_H         100
#define MAX_ICONS     64
#define DBLCK_MS      400

#define ICON_THEME    "NineIcons48x"

typedef struct {
	Imlib_Image    icon;
	int            iw, ih;
	char           label[128];
	char           cmd[512];
	int            x, y;          /* current position on desktop */
	int            ox, oy;          /* original position (for snap-back) */
	int            drag_dx, drag_dy;
	unsigned long  last_click_ms;
	unsigned char  selected;
} DeskIcon;

static DeskIcon        icons[MAX_ICONS];
static int             nicons;
static Display        *dpy;
static int             screen;
static Window          root;
static Window          desktop;
static XftFont        *xft_font;
static XftColor        xft_black, xft_white;
static Atom            a_xrootpmap, a_esetroot;
static int             sw, sh;
static int             max_rows;     /* how many rows fit vertically */
static Pixmap          wallpaper_pm = None;

/* Active drag state */
static int             drag_active = 0;
static int             drag_idx = -1;
static int             drag_start_x, drag_start_y;
static int             drag_old_x, drag_old_y;

/* Selection state */
static int             sel_count = 0;
static GC              sel_gc = None;
static unsigned long   sel_pixel;

/* Context menu */
static Window          menu_win = None;
static int             menu_active = 0;
static int             menu_hovered = -1;
static int             menu_idx = -1;  /* -1 = desktop, >=0 = icon */
static int             menu_x, menu_y;
static GC             menu_gc = None;
#define MENU_ITEM_H    22
#define MENU_W         120
#define MENU_ITEMS     3
static const char *menu_labels[] = { "Open", "Arrange Icons", "Refresh Desktop" };

static unsigned long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Wrap text into lines, returns number of lines */
static int wrap_text(const char *text, char out[MAX_LINES][64], int maxw) {
	int n = 0, i = 0;
	const char *p = text, *word_start;
	while (*p && n < MAX_LINES) {
		while (*p == ' ') p++;
		if (!*p) break;
		word_start = p;
		while (*p && *p != ' ') p++;
		int wlen = p - word_start;
		if (i + wlen + (i > 0) <= maxw) {
			if (i > 0) out[n][i++] = ' ';
			memcpy(out[n] + i, word_start, wlen);
			i += wlen;
			out[n][i] = '\0';
		} else {
			if (i == 0) {
				int fit = maxw;
				if (fit > 60) fit = 60;
				memcpy(out[n], word_start, fit - 1);
				out[n][fit - 1] = '\0';
				n++;
			} else {
				n++;
				i = 0;
				int fit = wlen;
				if (fit > maxw - 1) fit = maxw - 1;
				if (fit > 60) fit = 60;
				memcpy(out[n], word_start, fit);
				i += fit;
				out[n][i] = '\0';
			}
		}
	}
	return n + (i > 0 ? 1 : 0);
}

static char *expand_path(const char *path) {
	if (path[0] != '~') return strdup(path);
	const char *home = getenv("HOME");
	if (!home) home = "/tmp";
	char *out = malloc(strlen(home) + strlen(path) + 1);
	strcpy(out, home);
	strcat(out, path + 1);
	return out;
}

/* Search icon theme for a named icon */
static char *find_icon_in_theme(const char *name) {
	static const char *dirs[] = {
		"apps/48", "apps/32", "apps/16",
		"mimes/48", "mimes/32", "mimes/16",
		"places/48", "places/32", "places/16",
		"devices/48", "devices/32", "devices/16",
		"categories/48", "categories/32", "categories/16",
		"status/16",
		NULL
	};
	static const char *exts[] = { ".png", ".xpm", ".ico", NULL };

	const char *home = getenv("HOME");
	if (!home) home = "/tmp";

	for (int i = 0; dirs[i]; i++) {
		for (int e = 0; exts[e]; e++) {
			char path[512];
			snprintf(path, sizeof(path), "%s/.icons/%s/%s/%s%s",
				home, ICON_THEME, dirs[i], name, exts[e]);
			if (access(path, R_OK) == 0) return strdup(path);
		}
	}
	return NULL;
}

/* Load image via Imlib2, scale to ICON_SIZE, keep Imlib_Image for alpha */
static int load_icon_imlib(DeskIcon *d, const char *path) {
	Imlib_Image src = imlib_load_image(path);
	if (!src) return 0;

	imlib_context_set_image(src);
	int iw = imlib_image_get_width();
	int ih = imlib_image_get_height();

	int nw = ICON_SIZE, nh = ICON_SIZE;
	if (iw > ih) {
		nh = (int)round((double)ih * ICON_SIZE / iw);
		if (nh < 1) nh = 1;
	} else {
		nw = (int)round((double)iw * ICON_SIZE / ih);
		if (nw < 1) nw = 1;
	}

	Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, nw, nh);
	if (!scaled) {
		imlib_context_set_image(src);
		imlib_free_image();
		return 0;
	}

	imlib_context_set_image(src);
	imlib_free_image();

	d->icon = scaled;
	d->iw = nw;
	d->ih = nh;
	return 1;
}

/* Load wallpaper to pixmap */
static Pixmap load_wallpaper(const char *path, const char *mode, int w, int h) {
	Imlib_Image src = imlib_load_image(path);
	if (!src) {
		fprintf(stderr, "oxwm-desktop: failed to load wallpaper '%s'\n", path);
		return None;
	}

	imlib_context_set_image(src);
	int iw = imlib_image_get_width();
	int ih = imlib_image_get_height();

	Imlib_Image dst = imlib_create_image(w, h);
	if (!dst) {
		imlib_context_set_image(src);
		imlib_free_image();
		return None;
	}
	imlib_context_set_image(dst);
	imlib_image_set_has_alpha(0);
	imlib_context_set_color(0xD4, 0xD0, 0xC8, 0xFF);
	imlib_image_fill_rectangle(0, 0, w, h);

	imlib_context_set_image(src);
	imlib_context_set_image(dst);

	if (strcmp(mode, "center") == 0) {
		int nx = (w - iw) / 2, ny = (h - ih) / 2;
		imlib_blend_image_onto_image(src, 0, 0, 0, iw, ih, nx, ny, iw, ih);
	} else if (strcmp(mode, "tile") == 0) {
		for (int y = 0; y < h; y += ih)
			for (int x = 0; x < w; x += iw) {
				int dw = iw, dh = ih;
				if (x + dw > w) dw = w - x;
				if (y + dh > h) dh = h - y;
				imlib_blend_image_onto_image(src, 0, 0, 0, dw, dh, x, y, dw, dh);
			}
	} else if (strcmp(mode, "scale") == 0) {
		double sx = (double)w / iw, sy = (double)h / ih;
		double s = sx < sy ? sx : sy;
		int nw = (int)round(iw * s), nh = (int)round(ih * s);
		imlib_blend_image_onto_image(src, 0, 0, 0, iw, ih, (w-nw)/2, (h-nh)/2, nw, nh);
	} else {
		double sx = (double)w / iw, sy = (double)h / ih;
		double s = sx > sy ? sx : sy;
		int nw = (int)round(iw * s), nh = (int)round(ih * s);
		imlib_blend_image_onto_image(src, 0, 0, 0, iw, ih, (w-nw)/2, (h-nh)/2, nw, nh);
	}

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, screen));
	imlib_context_set_colormap(DefaultColormap(dpy, screen));
	imlib_context_set_drawable(root);
	imlib_context_set_image(dst);
	Pixmap pm;
	imlib_render_pixmaps_for_whole_image(&pm, NULL);

	imlib_context_set_image(src);
	imlib_free_image();
	imlib_context_set_image(dst);
	imlib_free_image();
	return pm;
}

/* Check if two rectangles overlap */
static int rects_overlap(int x1, int y1, int w1, int h1,
                         int x2, int y2, int w2, int h2) {
	return (x1 < x2 + w2 && x1 + w1 > x2 &&
	        y1 < y2 + h2 && y1 + h1 > y2);
}

/* Draw label with Mac OS 9 shadow style at given base position */
static void draw_label_at(XftDraw *draw, DeskIcon *d, int bx, int by, int maxw_px) {
	if (!xft_font || !d->label[0]) return;

	char lines[MAX_LINES][64];
	int max_chars = maxw_px / 6;
	if (max_chars < 4) max_chars = 4;
	int nlines = wrap_text(d->label, lines, max_chars);

	int start_y = ICON_SIZE + PAD + 2 + xft_font->ascent;

	for (int i = 0; i < nlines && i < MAX_LINES; i++) {
		if (!lines[i][0]) continue;
		char disp[68];
		strncpy(disp, lines[i], 63);
		disp[63] = '\0';

		int len = strlen(disp);
		XGlyphInfo extents;
		XftTextExtents8(dpy, xft_font, (unsigned char *)disp, len, &extents);
		while (len > 1 && extents.width > maxw_px - 8) {
			len--;
			disp[len] = '\0';
			XftTextExtents8(dpy, xft_font, (unsigned char *)disp, len, &extents);
		}
		if (len < (int)strlen(lines[i])) {
			strcat(disp, "\342\200\246");
			XftTextExtents8(dpy, xft_font, (unsigned char *)disp, strlen(disp), &extents);
		}

		int tx = (ICON_WIN_W - extents.width) / 2;
		if (tx < 0) tx = 0;
		int ty = start_y + i * LABEL_LINE_H;

		XftDrawString8(draw, &xft_black, xft_font, bx + tx + 1, by + ty + 1,
			(unsigned char *)disp, strlen(disp));
		XftDrawString8(draw, &xft_white, xft_font, bx + tx, by + ty,
			(unsigned char *)disp, strlen(disp));
	}
}

/* Draw one icon at a specific position */
static void paint_icon_at(XftDraw *draw, DeskIcon *d, int bx, int by) {
	int ix = bx + (ICON_WIN_W - d->iw) / 2;
	int iy = by + PAD + (ICON_SIZE - d->ih) / 2;

	if (d->icon) {
		imlib_context_set_image(d->icon);
		imlib_render_image_on_drawable(ix, iy);
	}

	draw_label_at(draw, d, bx, by, ICON_WIN_W - PAD * 2);
}

/* Draw one icon at its current stored position */
static void paint_icon(XftDraw *draw, int idx) {
	paint_icon_at(draw, &icons[idx], icons[idx].x, icons[idx].y);
	if (icons[idx].selected && sel_gc) {
		XSetForeground(dpy, sel_gc, sel_pixel);
		XSetLineAttributes(dpy, sel_gc, 2, LineSolid, CapButt, JoinMiter);
		XDrawRectangle(dpy, XftDrawDrawable(draw), sel_gc,
			icons[idx].x - 2, icons[idx].y - 2,
			ICON_WIN_W + 3, ICON_WIN_H + 3);
	}
}

/* Full redraw — wallpaper bg + all icons */
static void full_redraw(void) {
	XClearWindow(dpy, desktop);
	XftDraw *draw = XftDrawCreate(dpy, desktop,
		DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
	if (draw) {
		for (int i = 0; i < nicons; i++)
			paint_icon(draw, i);
		XftDrawDestroy(draw);
	}
}

/* Redraw icons that intersect a given region */
static void redraw_region(int rx, int ry, int rw, int rh) {
	XftDraw *draw = XftDrawCreate(dpy, desktop,
		DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
	if (draw) {
		for (int i = 0; i < nicons; i++) {
			if (rects_overlap(icons[i].x, icons[i].y, ICON_WIN_W, ICON_WIN_H,
			                  rx, ry, rw, rh))
				paint_icon(draw, i);
		}
		XftDrawDestroy(draw);
	}
}

/* Find icon index under (x,y), or -1 */
static int hit_test(int x, int y) {
	for (int i = 0; i < nicons; i++) {
		if (x >= icons[i].x && x < icons[i].x + ICON_WIN_W &&
		    y >= icons[i].y && y < icons[i].y + ICON_WIN_H)
			return i;
	}
	return -1;
}

static void selection_clear(void) {
	for (int i = 0; i < nicons; i++) icons[i].selected = 0;
	sel_count = 0;
}

static void selection_add(int idx) {
	if (idx < 0 || idx >= nicons) return;
	if (icons[idx].selected) return;
	icons[idx].selected = 1;
	sel_count++;
}

static void selection_toggle(int idx) {
	if (idx < 0 || idx >= nicons) return;
	if (icons[idx].selected) {
		icons[idx].selected = 0;
		sel_count--;
	} else {
		icons[idx].selected = 1;
		sel_count++;
	}
}

/* === CONTEXT MENU === */
static void create_menu_window(void) {
	if (menu_win) return;

	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.event_mask = ButtonPressMask | ButtonReleaseMask |
		ExposureMask | PointerMotionMask;
	wa.background_pixel = WhitePixel(dpy, screen);
	wa.border_pixel = BlackPixel(dpy, screen);
	menu_win = XCreateWindow(dpy, root, 0, 0, MENU_W,
		MENU_ITEMS * MENU_ITEM_H, 1,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel, &wa);

	menu_gc = XCreateGC(dpy, menu_win, 0, NULL);

	Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, menu_win, arrow);
}

static void show_menu(int x, int y, int idx) {
	if (!menu_win) create_menu_window();
	menu_active = 1;
	menu_hovered = -1;
	menu_idx = idx;
	menu_x = x;
	menu_y = y;

	/* Keep on screen */
	if (menu_x < 0) menu_x = 0;
	if (menu_y < 0) menu_y = 0;
	if (menu_x + MENU_W > sw) menu_x = sw - MENU_W - 2;
	if (menu_y + MENU_ITEMS * MENU_ITEM_H > sh)
		menu_y = sh - MENU_ITEMS * MENU_ITEM_H - 2;

	XMoveWindow(dpy, menu_win, menu_x, menu_y);
	XMapRaised(dpy, menu_win);
}

static void hide_menu(void) {
	if (menu_win) XUnmapWindow(dpy, menu_win);
	menu_active = 0;
	menu_hovered = -1;
	menu_idx = -1;
}

static void draw_menu(void) {
	if (!menu_gc) return;

	for (int i = 0; i < MENU_ITEMS; i++) {
		int iy = i * MENU_ITEM_H;
		/* Hilite if hovering */
		if (i == menu_hovered) {
			XSetForeground(dpy, menu_gc, 0x000080);  /* Mac OS blue */
			XFillRectangle(dpy, menu_win, menu_gc, 1, iy + 1, MENU_W - 2, MENU_ITEM_H - 2);
			XSetForeground(dpy, menu_gc, WhitePixel(dpy, screen));
		} else {
			XSetForeground(dpy, menu_gc, WhitePixel(dpy, screen));
			XFillRectangle(dpy, menu_win, menu_gc, 1, iy + 1, MENU_W - 2, MENU_ITEM_H - 2);
			XSetForeground(dpy, menu_gc, BlackPixel(dpy, screen));
		}
		XDrawString(dpy, menu_win, menu_gc, 8, iy + 15,
			menu_labels[i], strlen(menu_labels[i]));
	}

	/* Outer border */
	XSetForeground(dpy, menu_gc, BlackPixel(dpy, screen));
	XDrawRectangle(dpy, menu_win, menu_gc, 0, 0, MENU_W - 1, MENU_ITEMS * MENU_ITEM_H - 1);
}

static int menu_hit(int x, int y) {
	if (x < 0 || x >= MENU_W) return -1;
	int item = y / MENU_ITEM_H;
	if (item < 0 || item >= MENU_ITEMS) return -1;
	return item;
}

static void menu_set_hover(int x, int y) {
	int item = menu_hit(x, y);
	if (item != menu_hovered) {
		menu_hovered = item;
		draw_menu();
	}
}

/* Compute icon position for index: column-major from RIGHT edge */
static void calc_icon_pos(int idx, int *out_x, int *out_y) {
	int col = idx / max_rows;
	int row = idx % max_rows;
	/* Right-align: icon's right edge sits MARGIN_RIGHT from screen right edge */
	*out_x = sw - MARGIN_RIGHT - ICON_WIN_W - col * COL_W;
	*out_y = MARGIN_TOP + row * ROW_H;
}

/* Check if icon idx overlaps with any OTHER icon at position (tx,ty) */
static int overlaps_any(int idx, int tx, int ty) {
	for (int i = 0; i < nicons; i++) {
		if (i == idx) continue;
		if (rects_overlap(tx, ty, ICON_WIN_W, ICON_WIN_H,
		                  icons[i].x, icons[i].y, ICON_WIN_W, ICON_WIN_H))
			return 1;
	}
	return 0;
}

/* Snap a free-drag position to the nearest free grid cell, or revert if none */
static void resolve_overlap(int idx) {
	DeskIcon *d = &icons[idx];
	if (!overlaps_any(idx, d->x, d->y)) return;

	/* Try every grid cell, prefer the one closest to the drop point */
	int best_x = -1, best_y = -1, best_dist = 0;
	for (int col = 0; col * COL_W + ICON_WIN_W + MARGIN_RIGHT <= sw; col++) {
		for (int row = 0; row < max_rows; row++) {
			int tx = sw - MARGIN_RIGHT - ICON_WIN_W - col * COL_W;
			int ty = MARGIN_TOP + row * ROW_H;
			if (tx < 0 || ty < 0) continue;
			if (overlaps_any(idx, tx, ty)) continue;
			int dist = abs(tx - d->x) + abs(ty - d->y);
			if (best_x < 0 || dist < best_dist) {
				best_x = tx;
				best_y = ty;
				best_dist = dist;
			}
		}
	}
	if (best_x >= 0) {
		d->x = best_x;
		d->y = best_y;
	} else {
		/* No free cell — revert to original position */
		d->x = d->ox;
		d->y = d->oy;
	}
}

static int load(const char *cfgfile) {
	FILE *fp = fopen(cfgfile, "r");
	if (!fp) return 0;

	char line[512];
	while (fgets(line, sizeof(line), fp) && nicons < MAX_ICONS) {
		if (line[0] == '#' || line[0] == '\n') continue;

		char icon_raw[256] = "", lbl[128] = "", cmd[512] = "";

		char *comma1 = strchr(line, ',');
		char *comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
		if (!comma1 || !comma2) continue;

		int len = comma1 - line;
		if (len >= (int)sizeof(icon_raw)) len = sizeof(icon_raw) - 1;
		memcpy(icon_raw, line, len);
		icon_raw[len] = '\0';
		char *t = icon_raw + len - 1;
		while (t >= icon_raw && (*t == ' ' || *t == '\t')) *(t--) = '\0';
		t = icon_raw;
		while (*t == ' ' || *t == '\t') t++;
		if (t != icon_raw) memmove(icon_raw, t, strlen(t) + 1);

		char *ls = comma1 + 1;
		while (*ls == ' ' || *ls == '\t') ls++;
		len = comma2 - ls;
		if (len >= (int)sizeof(lbl)) len = sizeof(lbl) - 1;
		memcpy(lbl, ls, len);
		lbl[len] = '\0';
		t = lbl + len - 1;
		while (t >= lbl && (*t == ' ' || *t == '\t')) *(t--) = '\0';

		char *cs = comma2 + 1;
		while (*cs == ' ' || *cs == '\t') cs++;
		len = strlen(cs);
		if (len > 0 && cs[len - 1] == '\n') len--;
		if (len >= (int)sizeof(cmd)) len = sizeof(cmd) - 1;
		memcpy(cmd, cs, len);
		cmd[len] = '\0';

		if (!icon_raw[0] || !cmd[0]) continue;

		DeskIcon *d = &icons[nicons];
		d->icon = NULL;
		strncpy(d->label, lbl, 127);
		d->label[127] = '\0';
		strncpy(d->cmd, cmd, 511);
		d->cmd[511] = '\0';

		char *icon_path = NULL;
		if (strchr(icon_raw, '/')) {
			icon_path = expand_path(icon_raw);
		} else {
			icon_path = find_icon_in_theme(icon_raw);
			if (!icon_path) icon_path = strdup(icon_raw);
		}

		if (!load_icon_imlib(d, icon_path)) {
			fprintf(stderr, "oxwm-desktop: skip icon '%s' (tried '%s')\n",
				icon_raw, icon_path);
			free(icon_path);
			continue;
		}
		free(icon_path);

		calc_icon_pos(nicons, &d->x, &d->y);
		d->ox = d->x;
		d->oy = d->y;
		d->last_click_ms = 0;
		nicons++;
	}
	fclose(fp);
	return nicons;
}

int main(int argc, char **argv) {
	/* Auto-reap forked children to prevent zombies */
	signal(SIGCHLD, SIG_IGN);

	dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "oxwm-desktop: no display\n"); return 1; }
	screen = DefaultScreen(dpy);
	root   = RootWindow(dpy, screen);

	a_xrootpmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	a_esetroot  = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	max_rows = (sh - MARGIN_TOP) / ROW_H;
	if (max_rows < 1) max_rows = 1;

	/* Parse args */
	const char *bg_mode = "fill";
	const char *bg_path = NULL;
	const char *cfgfile = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-bg") && i + 1 < argc) {
			i++;
			char *bg_arg = argv[i];
			char *colon = strchr(bg_arg, ':');
			if (colon) {
				*colon = '\0';
				bg_mode = bg_arg;
				bg_arg = colon + 1;
			}
			bg_path = bg_arg;
		} else {
			cfgfile = argv[i];
		}
	}

	/* === DESKTOP WINDOW === */
	if (bg_path && bg_path[0]) {
		char *wp = bg_path[0] == '~' ? expand_path(bg_path) : strdup(bg_path);
		wallpaper_pm = load_wallpaper(wp, bg_mode, sw, sh);
		free(wp);
	}

	{
		XSetWindowAttributes wa;
		wa.override_redirect = True;
		wa.event_mask = ButtonPressMask | ButtonReleaseMask |
			Button1MotionMask | ExposureMask | StructureNotifyMask;
		wa.bit_gravity = NorthWestGravity;
		if (wallpaper_pm != None)
			wa.background_pixmap = wallpaper_pm;
		else
			wa.background_pixmap = ParentRelative;
		desktop = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			CWBackPixmap | CWOverrideRedirect | CWEventMask | CWBitGravity, &wa);
	}
	XLowerWindow(dpy, desktop);
	XMapWindow(dpy, desktop);
	/* Note: XLowerWindow is called once here, not in the Expose handler,
	 * to avoid cascading ConfigureNotify/Expose events. */

	/* Set imlib2 context once — these never change at runtime */
	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, screen));
	imlib_context_set_colormap(DefaultColormap(dpy, screen));
	imlib_context_set_drawable(desktop);

	/* Set proper arrow cursor (not the X cursor) */
	Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, desktop, arrow);

	if (wallpaper_pm != None) {
		XChangeProperty(dpy, root, a_xrootpmap, XA_PIXMAP, 32,
			PropModeReplace, (unsigned char *)&wallpaper_pm, 1);
		XChangeProperty(dpy, root, a_esetroot, XA_PIXMAP, 32,
			PropModeReplace, (unsigned char *)&wallpaper_pm, 1);
		XSetWindowBackgroundPixmap(dpy, root, wallpaper_pm);
	}

	/* === FONTS === */
	Visual *visual = DefaultVisual(dpy, screen);
	Colormap cmap = DefaultColormap(dpy, screen);

	xft_font = XftFontOpenName(dpy, screen, "Charcoal-9");
	if (!xft_font) xft_font = XftFontOpenName(dpy, screen, "Charcoal-10");
	if (!xft_font) xft_font = XftFontOpenName(dpy, screen, "sans-9");
	if (!xft_font) xft_font = XftFontOpenName(dpy, screen, "fixed-9");

	XftColorAllocName(dpy, visual, cmap, "black", &xft_black);
	XftColorAllocName(dpy, visual, cmap, "white", &xft_white);

	/* Selection highlight color/GC */
	{
		XColor scol;
		if (XAllocNamedColor(dpy, cmap, "#3a6cd8", &scol, &scol))
			sel_pixel = scol.pixel;
		else
			sel_pixel = WhitePixel(dpy, screen);
		sel_gc = XCreateGC(dpy, desktop, 0, NULL);
	}

	/* === ICONS === */
	char def[512];
	if (!cfgfile) {
		const char *home = getenv("HOME");
		if (!home) home = "/tmp";
		snprintf(def, sizeof(def), "%s/.oxwm/desktop.conf", home);
		if (access(def, R_OK) != 0)
			snprintf(def, sizeof(def), "%s/.mlvwm/desktop.conf", home);
		cfgfile = def;
	}

	if (!load(cfgfile)) {
		fprintf(stderr, "oxwm-desktop: no icons loaded\n");
	}

	/* Initial draw */
	full_redraw();

	/* === EVENT LOOP === */
	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);

		if (menu_active && ev.xany.window == menu_win) {
			switch (ev.type) {
			case Expose:
				draw_menu();
				break;
			case MotionNotify:
				menu_set_hover(ev.xmotion.x, ev.xmotion.y);
				break;
			case ButtonPress:
				hide_menu();
				break;
			case ButtonRelease: {
				int item = menu_hit(ev.xbutton.x, ev.xbutton.y);
				int idx = menu_idx;
				hide_menu();
				if (item == 0 && idx >= 0) {
					/* Open */
					if (fork() == 0) {
						execl("/bin/sh", "sh", "-c", icons[idx].cmd, NULL);
						_exit(1);
					}
				} else if (item == 1) {
					/* Arrange Icons: reset all to grid */
					for (int i = 0; i < nicons; i++) {
						calc_icon_pos(i, &icons[i].x, &icons[i].y);
					}
					full_redraw();
				} else if (item == 2) {
					/* Refresh Desktop */
					full_redraw();
				}
				break;
			}
			}
			continue;
		}

		/* Dismiss menu on any click outside it */
		if (menu_active) {
			if (ev.type == ButtonPress) {
				hide_menu();
				/* Fall through to process the click normally */
			} else if (ev.type == MotionNotify) {
				/* Ignore motion while menu is up unless on menu */
				continue;
			}
		}

		if (ev.xany.window != desktop) continue;

		switch (ev.type) {
		case Expose:
			redraw_region(ev.xexpose.x, ev.xexpose.y,
			              ev.xexpose.width, ev.xexpose.height);
			break;

		case ButtonPress:
			if (ev.xbutton.button == 3) {
				/* Right-click: context menu */
				int idx = hit_test(ev.xbutton.x, ev.xbutton.y);
				show_menu(ev.xbutton.x, ev.xbutton.y, idx);
				break;
			}
			if (ev.xbutton.button != 1) break;

			{
				int idx = hit_test(ev.xbutton.x, ev.xbutton.y);
				unsigned int state = ev.xbutton.state;
				int shift = state & ShiftMask;
				int ctrl  = state & ControlMask;

				if (idx >= 0) {
					if (ctrl)       selection_toggle(idx);
					else if (shift) selection_add(idx);
					else {
						if (!icons[idx].selected || sel_count > 1) {
							selection_clear();
							selection_add(idx);
						}
					}
					full_redraw();

					if (sel_count > 0) {
						drag_active = 1;
						drag_idx = idx;
						drag_start_x = ev.xbutton.x;
						drag_start_y = ev.xbutton.y;
						drag_old_x = icons[idx].x;
						drag_old_y = icons[idx].y;
						icons[idx].drag_dx = ev.xbutton.x - icons[idx].x;
						icons[idx].drag_dy = ev.xbutton.y - icons[idx].y;
					}
				} else if (sel_count > 0) {
					selection_clear();
					full_redraw();
				}
			}
			break;

		case MotionNotify:
			if (!drag_active || drag_idx < 0) break;

			{
				DeskIcon *d = &icons[drag_idx];
				int nx = ev.xmotion.x - d->drag_dx;
				int ny = ev.xmotion.y - d->drag_dy;

				if (nx < 0) nx = 0;
				if (ny < 0) ny = 0;
				if (nx > sw - ICON_WIN_W) nx = sw - ICON_WIN_W;
				if (ny > sh - ICON_WIN_H) ny = sh - ICON_WIN_H;

				int dx = nx - d->x;
				int dy = ny - d->y;
				if (dx == 0 && dy == 0) break;

				/* Compute bounding box of old selected positions BEFORE moving
				 * anything, so we know what to clear. */
				int minx = sw, miny = sh, maxx = 0, maxy = 0;
				int any = 0;
				for (int i = 0; i < nicons; i++) {
					if (!icons[i].selected) continue;
					if (icons[i].x < minx)     minx = icons[i].x;
					if (icons[i].y < miny)     miny = icons[i].y;
					if (icons[i].x + ICON_WIN_W  > maxx) maxx = icons[i].x + ICON_WIN_W;
					if (icons[i].y + ICON_WIN_H  > maxy) maxy = icons[i].y + ICON_WIN_H;
					any = 1;
				}
				int bw = any ? maxx - minx : 0;
				int bh = any ? maxy - miny : 0;

				/* Grab server: make the clear+repaint+paint sequence atomic
				 * so the user never sees a partial frame. */
				XGrabServer(dpy);

				XftDraw *draw = XftDrawCreate(dpy, desktop,
					DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
				if (draw) {
					if (any)
						XClearArea(dpy, desktop, minx, miny, bw, bh, False);

					/* Update positions of all selected icons. */
					for (int i = 0; i < nicons; i++) {
						if (!icons[i].selected) continue;
						if (i == drag_idx) {
							icons[i].x = nx;
							icons[i].y = ny;
						} else {
							icons[i].x += dx;
							icons[i].y += dy;
							if (icons[i].x < 0) icons[i].x = 0;
							if (icons[i].y < 0) icons[i].y = 0;
							if (icons[i].x > sw - ICON_WIN_W) icons[i].x = sw - ICON_WIN_W;
							if (icons[i].y > sh - ICON_WIN_H) icons[i].y = sh - ICON_WIN_H;
						}
					}

					/* Repaint unselected icons that overlapped the cleared region
					 * (using their current position, which is unchanged). */
					if (any) {
						for (int i = 0; i < nicons; i++) {
							if (icons[i].selected) continue;
							if (rects_overlap(icons[i].x, icons[i].y,
							                  ICON_WIN_W, ICON_WIN_H,
							                  minx, miny, bw, bh))
								paint_icon(draw, i);
						}
					}

					/* Paint all selected, drag_idx last so it ends up on top. */
					for (int i = 0; i < nicons; i++) {
						if (!icons[i].selected || i == drag_idx) continue;
						paint_icon(draw, i);
					}
					paint_icon(draw, drag_idx);

					XftDrawDestroy(draw);

					drag_old_x = nx;
					drag_old_y = ny;
				}

				XUngrabServer(dpy);
				XSync(dpy, False);
			}
			break;

		case ButtonRelease:
			if (!drag_active || drag_idx < 0) break;

			{
				DeskIcon *d = &icons[drag_idx];
				int moved = (abs(ev.xbutton.x - drag_start_x) > 3 ||
				             abs(ev.xbutton.y - drag_start_y) > 3);

				if (moved) {
					for (int i = 0; i < nicons; i++) {
						if (icons[i].selected) {
							resolve_overlap(i);
							icons[i].ox = icons[i].x;
							icons[i].oy = icons[i].y;
						}
					}
					full_redraw();
				} else {
					unsigned long now = now_ms();
					if (d->last_click_ms && now - d->last_click_ms < DBLCK_MS) {
						if (fork() == 0) {
							execl("/bin/sh", "sh", "-c", d->cmd, NULL);
							_exit(1);
						}
						d->last_click_ms = 0;
					} else {
						d->last_click_ms = now;
					}
				}
			}

			drag_active = 0;
			drag_idx = -1;
			break;
		}
	}
}
