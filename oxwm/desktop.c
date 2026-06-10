#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#include "oxwm.h"
#include "screen.h"
#include "misc.h"
#include "borders.h"
#include "add_window.h"
#include "event.h"
#include "functions.h"
#include "desktop.h"

#define ICON_SIZE     48
#define ICON_WIN_W    80
#define ICON_WIN_H    92
#define LABEL_MAX_H   36
#define LABEL_LINE_H  12
#define MAX_LINES      3
#define PAD            4
#define MARGIN_RIGHT   20
#define MARGIN_TOP     36
#define COL_W         90
#define ROW_H         100
#define MAX_ICONS     64
#define DBLCK_MS      400

#define ICON_THEME    "NineIcons48x"

typedef struct {
	Imlib_Image    icon;
	int            iw, ih;
	char           label[128];
	char           icon_name[128];
	char           cmd[512];
	int            x, y;
	int            ox, oy;
	int            drag_dx, drag_dy;
	unsigned long  last_click_ms;
	unsigned char  selected;
	unsigned char  kind;
} DeskIcon;

static DeskIcon        icons[MAX_ICONS];
static int             nicons;
static XftFont        *xft_font;
static XftColor        xft_black, xft_white, xft_gray;
static unsigned long   sel_pixel;
static GC              sel_gc = None;
static int             sw, sh;
static int             max_rows;
static Pixmap          wallpaper_pm = None;
static Atom            a_xrootpmap, a_esetroot;

static int             drag_active = 0;
static int             drag_idx = -1;
static int             drag_start_x, drag_start_y;
static int             drag_old_x, drag_old_y;

static int             sel_count = 0;

static Window          dmenu_win = None;
static int             dmenu_active = 0;
static int             dmenu_hovered = -1;
static int             dmenu_idx = -1;
static int             dmenu_x, dmenu_y;
static GC              field_bg_gc = None;
static GC              field_bg_active_gc = None;
static GC              placeholder_gc = None;
static GC              dmenu_gc = None;
#define DMENU_ITEM_H    22
#define DMENU_W         150
static const char *icon_menu_labels[] = {
	"Open", "New Launcher...", "Delete", "Arrange Icons", "Refresh Desktop"
};
static const char *empty_menu_labels[] = {
	"New Launcher...", "Arrange Icons", "Refresh Desktop"
};
static const char **dmenu_labels = icon_menu_labels;
static int dmenu_count = 3;
static int dmenu_is_icon = 0;

static const char *config_path_in_use = NULL;

static unsigned long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

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

static const char *home_dir(void) {
	const char *home = getenv("HOME");
	return (home && home[0]) ? home : NULL;
}

static char *xdg_path(const char *env, const char *fallback_subdir) {
	const char *val = getenv(env);
	if (val && val[0]) return strdup(val);
	const char *home = home_dir();
	if (!home) return NULL;
	char *out = malloc(strlen(home) + strlen(fallback_subdir) + 2);
	sprintf(out, "%s/%s", home, fallback_subdir);
	return out;
}

static char *xdg_config_home(void) {
	char *p = xdg_path("XDG_CONFIG_HOME", ".config");
	return p;
}

static char *xdg_data_home(void) {
	char *p = xdg_path("XDG_DATA_HOME", ".local/share");
	return p;
}

static char *expand_path(const char *path) {
	if (!path) return NULL;
	if (path[0] != '~') return strdup(path);
	const char *home = home_dir();
	if (!home) return strdup(path);
	char *out = malloc(strlen(home) + strlen(path) + 1);
	strcpy(out, home);
	strcat(out, path + 1);
	return out;
}

static char *find_default_wallpaper(void) {
	static const char *filenames[] = {
		"wallpaper.jpg", "wallpaper.png", "wallpaper2.jpg",
		"wallpaper3.jpg", "wallpaper4.jpg", "wallpaper5.jpg",
		"wallpaper6.jpg", NULL
	};
	char *dirs[8];
	int ndirs = 0;
	char *xdg_data = xdg_data_home();
	if (xdg_data) {
		dirs[ndirs++] = xdg_data;
		dirs[ndirs] = NULL;
		char *wp = malloc(strlen(xdg_data) + 20);
		sprintf(wp, "%s/wallpapers", xdg_data);
		dirs[ndirs++] = wp;
	}
	const char *home = home_dir();
	char *home_wp = NULL, *pics_wp = NULL;
	if (home) {
		home_wp = malloc(strlen(home) + 20);
		sprintf(home_wp, "%s/wallpapers", home);
		dirs[ndirs++] = home_wp;
		pics_wp = malloc(strlen(home) + 30);
		sprintf(pics_wp, "%s/Pictures/wallpapers", home);
		dirs[ndirs++] = pics_wp;
		char *pics = malloc(strlen(home) + 12);
		sprintf(pics, "%s/Pictures", home);
		dirs[ndirs++] = pics;
	}
	dirs[ndirs] = NULL;

	for (int d = 0; dirs[d]; d++) {
		for (int f = 0; filenames[f]; f++) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/%s", dirs[d], filenames[f]);
			if (access(path, R_OK) == 0) {
				char *result = strdup(path);
				for (int i = 0; i < ndirs; i++) free(dirs[i]);
				return result;
			}
		}
	}
	for (int i = 0; i < ndirs; i++) free(dirs[i]);
	return NULL;
}

static char *find_icon_in_theme(const char *name) {
	static const char *subdirs[] = {
		"apps/48", "apps/32", "apps/16",
		"mimes/48", "mimes/32", "mimes/16",
		"places/48", "places/32", "places/16",
		"devices/48", "devices/32", "devices/16",
		"categories/48", "categories/32", "categories/16",
		"status/16",
		NULL
	};
	static const char *exts[] = { ".png", ".xpm", ".ico", NULL };

	const char *home = home_dir();
	struct { const char *root; const char *icon_dir; } roots[4];
	int nroots = 0;

	const char *xdg_data = getenv("XDG_DATA_HOME");
	if (xdg_data && xdg_data[0]) {
		roots[nroots].root = xdg_data;
		roots[nroots].icon_dir = "icons";
		nroots++;
	}
	if (home) {
		roots[nroots].root = home;
		roots[nroots].icon_dir = ".icons";
		nroots++;
		static char local_share[512];
		snprintf(local_share, sizeof(local_share), "%s/.local/share", home);
		roots[nroots].root = local_share;
		roots[nroots].icon_dir = "icons";
		nroots++;
	}

	for (int r = 0; r < nroots; r++) {
		for (int i = 0; subdirs[i]; i++) {
			for (int e = 0; exts[e]; e++) {
				char path[512];
				snprintf(path, sizeof(path), "%s/%s/%s/%s/%s%s",
					roots[r].root, roots[r].icon_dir, ICON_THEME,
					subdirs[i], name, exts[e]);
				if (access(path, R_OK) == 0) return strdup(path);
			}
		}
	}
	return NULL;
}

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
	imlib_context_set_visual(DefaultVisual(dpy, Scr.screen));
	imlib_context_set_colormap(DefaultColormap(dpy, Scr.screen));
	imlib_context_set_drawable(Scr.Root);
	imlib_context_set_image(dst);
	Pixmap pm;
	imlib_render_pixmaps_for_whole_image(&pm, NULL);

	imlib_context_set_image(src);
	imlib_free_image();
	imlib_context_set_image(dst);
	imlib_free_image();
	return pm;
}

static int rects_overlap(int x1, int y1, int w1, int h1,
                          int x2, int y2, int w2, int h2) {
	return (x1 < x2 + w2 && x1 + w1 > x2 &&
	        y1 < y2 + h2 && y1 + h1 > y2);
}

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

static void paint_icon(XftDraw *draw, int idx) {
	DeskIcon *d = &icons[idx];
	int bx = d->x, by = d->y;
	int ix = bx + (ICON_WIN_W - d->iw) / 2;
	int iy = by + PAD + (ICON_SIZE - d->ih) / 2;

	if (d->icon) {
		imlib_context_set_image(d->icon);
		imlib_render_image_on_drawable(ix, iy);
	}

	draw_label_at(draw, d, bx, by, ICON_WIN_W - PAD * 2);
}

static void full_redraw(void) {
	XClearWindow(dpy, Scr.Desktop);
	XftDraw *draw = XftDrawCreate(dpy, Scr.Desktop,
		DefaultVisual(dpy, Scr.screen), DefaultColormap(dpy, Scr.screen));
	if (draw) {
		for (int i = 0; i < nicons; i++)
			paint_icon(draw, i);
		XftDrawDestroy(draw);
	}
}

void RedrawDesktopIcons(void) {
	if (Scr.Desktop != None) full_redraw();
}

static void redraw_region(int rx, int ry, int rw, int rh) {
	XftDraw *draw = XftDrawCreate(dpy, Scr.Desktop,
		DefaultVisual(dpy, Scr.screen), DefaultColormap(dpy, Scr.screen));
	if (draw) {
		for (int i = 0; i < nicons; i++) {
			if (rects_overlap(icons[i].x, icons[i].y, ICON_WIN_W, ICON_WIN_H,
			                  rx, ry, rw, rh))
				paint_icon(draw, i);
		}
		XftDrawDestroy(draw);
	}
}

void RedrawDesktopRegion(int x, int y, int w, int h) {
	if (Scr.Desktop != None) redraw_region(x, y, w, h);
}

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

static void calc_icon_pos(int idx, int *out_x, int *out_y);
static void resolve_overlap(int idx);
static void open_icon(int idx);
static void delete_icon(int idx);
static void new_launcher_dialog(void);

static char *trash_dir_path(void) {
	char *base = xdg_data_home();
	if (!base) {
		base = strdup("/tmp");
	}
	char *p = malloc(strlen(base) + 20);
	sprintf(p, "%s/Trash/files/", base);
	free(base);
	return p;
}

static void ensure_trash_dir_exists(void) {
	char *base = xdg_data_home();
	if (!base) {
		fprintf(stderr, "oxwm-desktop: cannot determine data dir for trash\n");
		return;
	}
	char path[1024];
	snprintf(path, sizeof(path), "%s", base);
	mkdir(path, 0700);
	snprintf(path, sizeof(path), "%s/Trash", base);
	mkdir(path, 0700);
	snprintf(path, sizeof(path), "%s/Trash/files", base);
	mkdir(path, 0700);
	free(base);
}

static void open_trash(void) {
	ensure_trash_dir_exists();
	char *td = trash_dir_path();
	if (fork() == 0) {
		execlp("thunar", "thunar", td, (char *)NULL);
		execlp("pcmanfm", "pcmanfm", td, (char *)NULL);
		execlp("xdg-open", "xdg-open", td, (char *)NULL);
		execlp("nautilus", "nautilus", td, (char *)NULL);
		_exit(1);
	}
	free(td);
}

static void open_icon(int idx) {
	if (idx < 0 || idx >= nicons) return;
	if (icons[idx].kind == 1) {
		open_trash();
		return;
	}
	if (fork() == 0) {
		execl("/bin/sh", "sh", "-c", icons[idx].cmd, NULL);
		_exit(1);
	}
}

static void add_trash_icon(void) {
	if (nicons >= MAX_ICONS) return;
	DeskIcon *t = &icons[nicons];
	memset(t, 0, sizeof(*t));
	snprintf(t->label, sizeof(t->label), "Trash");
	snprintf(t->icon_name, sizeof(t->icon_name), "trash");
	snprintf(t->cmd, sizeof(t->cmd), "@trash");
	t->x = MARGIN_RIGHT;
	t->y = sh - ICON_WIN_H - MARGIN_RIGHT;
	t->ox = t->x;
	t->oy = t->y;
	t->kind = 1;
	t->selected = 0;
	char *path = find_icon_in_theme("user-trash");
	if (!path) path = find_icon_in_theme("trash");
	if (path) {
		load_icon_imlib(t, path);
		free(path);
	}
	nicons++;
}

static void append_launcher_to_config(const char *icon_name, const char *label,
                                       const char *cmd) {
	if (!config_path_in_use) {
		fprintf(stderr, "oxwm-desktop: no config path set, cannot save launcher\n");
		return;
	}
	char *slash = strrchr(config_path_in_use, '/');
	if (slash && slash != config_path_in_use) {
		char dir[1024];
		int len = slash - config_path_in_use;
		if (len >= (int)sizeof(dir)) len = sizeof(dir) - 1;
		memcpy(dir, config_path_in_use, len);
		dir[len] = '\0';
		struct stat st;
		if (stat(dir, &st) != 0) {
			char mkcmd[1100];
			snprintf(mkcmd, sizeof(mkcmd), "mkdir -p '%s'", dir);
			if (system(mkcmd) != 0) {
				fprintf(stderr, "oxwm-desktop: cannot create config dir '%s'\n", dir);
				return;
			}
		}
	}
	FILE *fp = fopen(config_path_in_use, "a");
	if (!fp) {
		fprintf(stderr, "oxwm-desktop: cannot open config '%s' for writing\n",
			config_path_in_use);
		return;
	}
	fprintf(fp, "%s, %s, %s\n", icon_name, label, cmd);
	fclose(fp);
}

static int delete_from_config(int idx) {
	if (idx < 0 || idx >= nicons) return -1;
	if (!config_path_in_use) return -1;
	FILE *fp = fopen(config_path_in_use, "r");
	if (!fp) return -1;

	char **lines = NULL;
	int nlines = 0, cap = 0;
	char buf[1024];
	int deleted = 0;
	while (fgets(buf, sizeof(buf), fp)) {
		if (!deleted) {
			int len = strlen(buf);
			while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
				buf[--len] = '\0';
			char want[1024];
			snprintf(want, sizeof(want), "%s, %s,",
				icons[idx].icon_name, icons[idx].label);
			if (strncmp(buf, want, strlen(want)) == 0) {
				deleted = 1;
				continue;
			}
		}
		if (nlines + 1 > cap) {
			cap = cap ? cap * 2 : 32;
			lines = realloc(lines, cap * sizeof(char *));
			if (!lines) { fclose(fp); return -1; }
		}
		lines[nlines++] = strdup(buf);
	}
	fclose(fp);

	if (!deleted) {
		for (int i = 0; i < nlines; i++) free(lines[i]);
		free(lines);
		return -1;
	}

	fp = fopen(config_path_in_use, "w");
	if (!fp) {
		for (int i = 0; i < nlines; i++) free(lines[i]);
		free(lines);
		return -1;
	}
	for (int i = 0; i < nlines; i++) {
		fputs(lines[i], fp);
		free(lines[i]);
	}
	fclose(fp);
	free(lines);
	return 0;
}

static void remove_icon_from_array(int idx) {
	if (idx < 0 || idx >= nicons) return;
	if (icons[idx].icon) {
		imlib_context_set_image(icons[idx].icon);
		imlib_free_image();
	}
	for (int j = idx; j < nicons - 1; j++) {
		icons[j] = icons[j + 1];
	}
	nicons--;
	memset(&icons[nicons], 0, sizeof(icons[0]));
}

typedef struct {
	char label[128];
	char cmd[512];
	char iname[128];
} LauncherData;

static void launcher_draw_titlebar(Window w, int fw, int th) {
	DrawTitleBarCore(w, fw, th, "New Launcher", 1, 0);
}

static void launcher_draw_input(Window w, int x, int y, int wpx, int hpx,
                                const char *text, int active, GC fill_gc) {
	DrawInputField(w, x, y, wpx, hpx, text, active, fill_gc);
}

static void launcher_draw_button(Window w, int x, int y, int wpx, int hpx,
                                  const char *label, int active) {
	DrawPushButton(w, x, y, wpx, hpx, label, active);
}

#define LDR_MARGIN          2
#define LDR_ROW_H          32
#define LDR_LABEL_W        80
#define LDR_IN_H           22
#define LDR_BTN_W          80
#define LDR_BTN_H          24
#define LDR_BTN_GAP        6
#define LDR_LABEL_INPUT_GAP 4
#define LDR_BTN_SPACING    10
#define LDR_TEXT_XPAD      4
#define LDR_TEXT_BASELINE  5
#define LDR_FW             420
#define LDR_FH             (LDR_TH + 2*LDR_MARGIN + 3*LDR_ROW_H + LDR_BTN_GAP + LDR_BTN_H + 2*LDR_MARGIN)
#define LDR_TH             TITLE_HEIGHT

typedef struct {
	Window w;
	int input_x, input_w;
	int rows_y[3];
	int btn_y, btn_ok_x, btn_cancel_x;
	const char *labels[3];
	const char *placeholders[3];
	char fields[3][128];
	int lens[3];
	int active;
	int focus_btn;
	int in_button;
	int cancelled;
	int done;
} LDState;

static void ld_redraw(LDState *s) {
	int i, lw, lh, lo;
	char *l;
	Window w = s->w;
	const int FW = LDR_FW, FH = LDR_FH, TH = LDR_TH;

	DrawWindowFrame(w, FW, FH);

	launcher_draw_titlebar(w, FW, TH);

	XSetForeground(dpy, Scr.BlackGC, BlackPixel(dpy, Scr.screen));
	XDrawLine(dpy, w, Scr.BlackGC, 0, TH, FW, TH);
	XDrawLine(dpy, w, Scr.BlackGC, 0, TH + 1, FW, TH + 1);

	for (i = 0; i < 3; i++) {
		l = (char *)s->labels[i];
		StrWidthHeight(WINDOWFONT, &lw, &lh, &lo, l, strlen(l));
		XDRAWSTRING(dpy, w, WINDOWFONT, Scr.BlackGC,
		            LDR_MARGIN, s->rows_y[i] + LDR_IN_H / 2 - lo + LDR_TEXT_BASELINE - 1,
		            l, strlen(l));
		launcher_draw_input(w, s->input_x, s->rows_y[i], s->input_w, LDR_IN_H,
		                    s->fields[i],
		                    !s->in_button && s->active == i,
		                    (s->active == i && !s->in_button)
		                        ? field_bg_active_gc : field_bg_gc);
		if (s->lens[i] == 0 && (!s->in_button && s->active == i)) {
			int text_off = 0;
			char *ph = (char *)s->placeholders[i];
			StrWidthHeight(WINDOWFONT, NULL, NULL, &text_off, "Mg", 2);
			XDRAWSTRING(dpy, w, WINDOWFONT, placeholder_gc,
			            s->input_x + LDR_TEXT_XPAD,
			            s->rows_y[i] + LDR_IN_H / 2 - text_off + LDR_TEXT_BASELINE,
			            ph, strlen(ph));
		}
	}
	launcher_draw_button(w, s->btn_ok_x, s->btn_y, LDR_BTN_W, LDR_BTN_H,
	                     "OK", s->in_button && s->focus_btn == 0);
	launcher_draw_button(w, s->btn_cancel_x, s->btn_y, LDR_BTN_W, LDR_BTN_H,
	                     "Cancel", s->in_button && s->focus_btn == 1);
}

static int ld_field_at(LDState *s, int x, int y) {
	int i;
	if (x < s->input_x || x >= s->input_x + s->input_w) return -1;
	for (i = 0; i < 3; i++) {
		if (y >= s->rows_y[i] && y < s->rows_y[i] + LDR_IN_H) return i;
	}
	return -1;
}

static int ld_btn_at(LDState *s, int x, int y) {
	if (y < s->btn_y || y >= s->btn_y + LDR_BTN_H) return -1;
	if (x >= s->btn_ok_x && x < s->btn_ok_x + LDR_BTN_W) return 0;
	if (x >= s->btn_cancel_x && x < s->btn_cancel_x + LDR_BTN_W) return 1;
	return -1;
}

static int RunLauncherDialog(LauncherData *out) {
	LDState s;
	XSetWindowAttributes wa;
	XEvent ev;
	int fx = (sw - LDR_FW) / 2, fy = (sh - LDR_FH) / 3;

	if (fx < 0) fx = 0;
	if (fy < LDR_TH) fy = LDR_TH;

	memset(&s, 0, sizeof(s));
	s.labels[0] = "Label:";
	s.labels[1] = "Command:";
	s.labels[2] = "Icon name:";
	s.placeholders[0] = "Firefox";
	s.placeholders[1] = "firefox %u";
	s.placeholders[2] = "firefox";
	s.rows_y[0] = LDR_TH + LDR_MARGIN;
	s.rows_y[1] = LDR_TH + LDR_MARGIN + LDR_ROW_H;
	s.rows_y[2] = LDR_TH + LDR_MARGIN + 2 * LDR_ROW_H;
	s.input_x = LDR_MARGIN + LDR_LABEL_W + LDR_LABEL_INPUT_GAP;
	s.input_w = LDR_FW - s.input_x - LDR_MARGIN;
	s.btn_y = LDR_TH + LDR_MARGIN + 3 * LDR_ROW_H + LDR_BTN_GAP;
	s.btn_ok_x = (LDR_FW - 2 * LDR_BTN_W - LDR_BTN_SPACING) / 2;
	s.btn_cancel_x = s.btn_ok_x + LDR_BTN_W + LDR_BTN_SPACING;
	s.cancelled = 1;

	wa.override_redirect = True;
	wa.event_mask = KeyPressMask | ButtonPressMask | ButtonReleaseMask |
	                ExposureMask | StructureNotifyMask | FocusChangeMask;
	wa.background_pixel = WhitePixel(dpy, Scr.screen);
	wa.border_pixel = BlackPixel(dpy, Scr.screen);
	s.w = XCreateWindow(dpy, Scr.Root, fx, fy, LDR_FW, LDR_FH, 1,
	                    CopyFromParent, InputOutput, CopyFromParent,
	                    CWOverrideRedirect | CWEventMask |
	                    CWBackPixel | CWBorderPixel, &wa);
	XMapRaised(dpy, s.w);

	XSetInputFocus(dpy, s.w, RevertToParent, CurrentTime);
	ld_redraw(&s);
	XGrabPointer(dpy, s.w, False,
	             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, s.w, None, CurrentTime);
	XGrabKeyboard(dpy, s.w, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	XFlush(dpy);

	while (!s.done) {
		XNextEvent(dpy, &ev);
		if (ev.xany.window != s.w) continue;

		switch (ev.type) {
		case Expose:
		case FocusIn:
		case FocusOut:
			ld_redraw(&s);
			break;
		case ButtonPress: {
			int x = ev.xbutton.x, y = ev.xbutton.y;
			int fi, bi;
			fi = ld_field_at(&s, x, y);
			if (fi >= 0) {
				s.in_button = 0;
				s.active = fi;
				ld_redraw(&s);
				break;
			}
			bi = ld_btn_at(&s, x, y);
			if (bi >= 0) {
				s.in_button = 1;
				s.focus_btn = bi;
				ld_redraw(&s);
				s.cancelled = (bi != 0);
				s.done = 1;
				break;
			}
			break;
		}
		case KeyPress: {
			KeySym ks;
			char buf[16];
			int n = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &ks, NULL);
			if (n > 0) buf[n] = '\0';

			if (ks == XK_Escape) {
				s.cancelled = 1; s.done = 1;
			} else if (ks == XK_Return || ks == XK_KP_Enter) {
				if (s.in_button) {
					s.cancelled = (s.focus_btn == 1);
					s.done = 1;
				} else if (s.active == 2) {
					s.cancelled = 0; s.done = 1;
				} else {
					s.active++;
					ld_redraw(&s);
				}
			} else if (ks == XK_Tab) {
				if (ev.xkey.state & ShiftMask) {
					if (s.in_button) {
						s.in_button = 0;
						s.active = 2;
					} else {
						s.active--;
						if (s.active < 0) {
							s.active = 0;
							s.in_button = 1;
							s.focus_btn = 1;
						}
					}
				} else {
					if (s.in_button) {
						s.in_button = 0;
						s.active = 0;
					} else {
						s.active++;
						if (s.active > 2) {
							s.active = 0;
							s.in_button = 1;
							s.focus_btn = 0;
						}
					}
				}
				ld_redraw(&s);
			} else if (ks == XK_BackSpace || ks == XK_Delete) {
				if (!s.in_button && s.lens[s.active] > 0) {
					s.fields[s.active][--s.lens[s.active]] = '\0';
					ld_redraw(&s);
				}
			} else if (ks == XK_u && (ev.xkey.state & ControlMask)) {
				if (!s.in_button) {
					s.lens[s.active] = 0;
					s.fields[s.active][0] = '\0';
					ld_redraw(&s);
				}
			} else if (n > 0 && !IsModifierKey(ks) && !IsCursorKey(ks) &&
			           !IsFunctionKey(ks) && !IsMiscFunctionKey(ks) &&
			           !IsKeypadKey(ks)) {
				if (!s.in_button && s.lens[s.active] + n < 127) {
					memcpy(s.fields[s.active] + s.lens[s.active], buf, n);
					s.lens[s.active] += n;
					s.fields[s.active][s.lens[s.active]] = '\0';
					ld_redraw(&s);
				}
			}
			break;
		}
		}
	}

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XDestroyWindow(dpy, s.w);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XSync(dpy, False);

	if (!s.cancelled) {
		strncpy(out->label, s.fields[0], 127); out->label[127] = '\0';
		strncpy(out->cmd, s.fields[1], 511); out->cmd[511] = '\0';
		strncpy(out->iname, s.fields[2], 127); out->iname[127] = '\0';
	}
	return s.cancelled;
}

static void new_launcher_dialog(void) {
	char *label = NULL, *cmd = NULL, *iname = NULL;
	DeskIcon *d;
	LauncherData ld;
	int rc;

	rc = RunLauncherDialog(&ld);
	if (rc != 0) return;

	label = ld.label;
	cmd = ld.cmd;
	iname = ld.iname;

	if (!label[0] || !cmd[0] || !iname[0]) goto out;
	if (nicons >= MAX_ICONS) goto out;

	d = &icons[nicons];
	memset(d, 0, sizeof(*d));
	strncpy(d->label, label, 127); d->label[127] = '\0';
	strncpy(d->icon_name, iname, 127); d->icon_name[127] = '\0';
	strncpy(d->cmd, cmd, 511); d->cmd[511] = '\0';
	d->kind = 0;
	d->selected = 0;

	char *path = find_icon_in_theme(iname);
	if (path) {
		load_icon_imlib(d, path);
		free(path);
	}

	calc_icon_pos(nicons, &d->x, &d->y);
	d->ox = d->x;
	d->oy = d->y;
	d->last_click_ms = 0;
	nicons++;
	resolve_overlap(nicons - 1);

	append_launcher_to_config(iname, label, cmd);
	full_redraw();

out:
	return;
}

static void delete_icon(int idx) {
	if (idx < 0 || idx >= nicons) return;
	if (icons[idx].kind == 1) return;
	if (icons[idx].kind == 0) {
		delete_from_config(idx);
	}
	remove_icon_from_array(idx);
	if (drag_idx == idx || drag_idx >= nicons) {
		drag_active = 0;
		drag_idx = -1;
	} else if (drag_idx > idx) {
		drag_idx--;
	}
}

static void create_dmenu_window(void) {
	if (dmenu_win) return;

	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.event_mask = ExposureMask;
	wa.background_pixel = WhitePixel(dpy, Scr.screen);
	wa.border_pixel = BlackPixel(dpy, Scr.screen);
	dmenu_win = XCreateWindow(dpy, Scr.Root, 0, 0, DMENU_W, 5 * DMENU_ITEM_H, 1,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel, &wa);

	dmenu_gc = XCreateGC(dpy, dmenu_win, 0, NULL);

	Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, dmenu_win, arrow);
}

static void draw_dmenu(void);

static void show_dmenu(int x, int y, int idx, const char **labels, int count) {
	if (!dmenu_win) create_dmenu_window();
	dmenu_active = 1;
	dmenu_hovered = -1;
	dmenu_idx = idx;
	dmenu_labels = labels;
	dmenu_count = count;
	dmenu_is_icon = (labels == icon_menu_labels);
	dmenu_x = x;
	dmenu_y = y;

	if (dmenu_x < 0) dmenu_x = 0;
	if (dmenu_y < 0) dmenu_y = 0;
	if (dmenu_x + DMENU_W > sw) dmenu_x = sw - DMENU_W - 2;
	if (dmenu_y + dmenu_count * DMENU_ITEM_H > sh)
		dmenu_y = sh - dmenu_count * DMENU_ITEM_H - 2;

	XMoveWindow(dpy, dmenu_win, dmenu_x, dmenu_y);
	XResizeWindow(dpy, dmenu_win, DMENU_W, dmenu_count * DMENU_ITEM_H);
	XMapRaised(dpy, dmenu_win);
	draw_dmenu();
}

static void hide_dmenu(void) {
	if (dmenu_win) XUnmapWindow(dpy, dmenu_win);
	dmenu_active = 0;
	dmenu_hovered = -1;
	dmenu_idx = -1;
}

static int dmenu_hit_root(int x, int y);
static void dmenu_action(int item);

static void press_dmenu(void) {
	XEvent ev;
	int ignore = 1;
	int done = 0;
	int activated_item = -1;
	int item;
	int root_x, root_y, win_x, win_y;
	Window junk_root, junk_child;
	unsigned int mask;

	if (!GrabEvent(DEFAULT)) {
		XBell(dpy, 30);
		hide_dmenu();
		return;
	}

	while (!done) {
		XMaskEvent(dpy, ExposureMask | ButtonReleaseMask | ButtonPressMask |
		            PointerMotionMask | ButtonMotionMask, &ev);
		switch (ev.type) {
		case Expose:
			if (dmenu_active && ev.xany.window == dmenu_win &&
			    ev.xexpose.count == 0)
				draw_dmenu();
			break;
		case MotionNotify:
			if (dmenu_active) {
				XQueryPointer(dpy, Scr.Root, &junk_root, &junk_child,
				              &root_x, &root_y, &win_x, &win_y, &mask);
				item = dmenu_hit_root(root_x, root_y);
				if (item != dmenu_hovered) {
					dmenu_hovered = item;
					draw_dmenu();
				}
			}
			break;
		case ButtonRelease:
			if (ignore) {
				ignore = 0;
			} else {
				XQueryPointer(dpy, Scr.Root, &junk_root, &junk_child,
				              &root_x, &root_y, &win_x, &win_y, &mask);
				activated_item = dmenu_hit_root(root_x, root_y);
				done = 1;
			}
			break;
		case ButtonPress:
			break;
		}
	}

	UnGrabEvent();
	dmenu_action(activated_item);
}

static void draw_dmenu(void) {
	if (!dmenu_gc) return;

	for (int i = 0; i < dmenu_count; i++) {
		int iy = i * DMENU_ITEM_H;
		if (i == dmenu_hovered) {
			XSetForeground(dpy, dmenu_gc, 0x000080);
			XFillRectangle(dpy, dmenu_win, dmenu_gc, 1, iy + 1, DMENU_W - 2, DMENU_ITEM_H - 2);
			XSetForeground(dpy, dmenu_gc, WhitePixel(dpy, Scr.screen));
		} else {
			XSetForeground(dpy, dmenu_gc, WhitePixel(dpy, Scr.screen));
			XFillRectangle(dpy, dmenu_win, dmenu_gc, 1, iy + 1, DMENU_W - 2, DMENU_ITEM_H - 2);
			XSetForeground(dpy, dmenu_gc, BlackPixel(dpy, Scr.screen));
		}
		XDRAWSTRING(dpy, dmenu_win, MENUFONT, dmenu_gc, 8, iy + 15,
			dmenu_labels[i], strlen(dmenu_labels[i]));
	}

	XSetForeground(dpy, dmenu_gc, BlackPixel(dpy, Scr.screen));
	XDrawRectangle(dpy, dmenu_win, dmenu_gc, 0, 0, DMENU_W - 1, dmenu_count * DMENU_ITEM_H - 1);
}

static int dmenu_hit_root(int x, int y) {
	if (x < dmenu_x || x >= dmenu_x + DMENU_W) return -1;
	if (y < dmenu_y || y >= dmenu_y + dmenu_count * DMENU_ITEM_H) return -1;
	return (y - dmenu_y) / DMENU_ITEM_H;
}

static void calc_icon_pos(int idx, int *out_x, int *out_y) {
	int col = idx / max_rows;
	int row = idx % max_rows;
	*out_x = sw - MARGIN_RIGHT - ICON_WIN_W - col * COL_W;
	*out_y = MARGIN_TOP + row * ROW_H;
}

static int overlaps_any(int idx, int tx, int ty) {
	for (int i = 0; i < nicons; i++) {
		if (i == idx) continue;
		if (rects_overlap(tx, ty, ICON_WIN_W, ICON_WIN_H,
		                  icons[i].x, icons[i].y, ICON_WIN_W, ICON_WIN_H))
			return 1;
	}
	return 0;
}

static void resolve_overlap(int idx) {
	DeskIcon *d = &icons[idx];
	if (!overlaps_any(idx, d->x, d->y)) return;

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
		d->x = d->ox;
		d->y = d->oy;
	}
}

static int load_config(const char *cfgfile) {
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
		strncpy(d->icon_name, icon_raw, 127);
		d->icon_name[127] = '\0';
		strncpy(d->cmd, cmd, 511);
		d->cmd[511] = '\0';
		d->kind = 0;
		d->selected = 0;

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

void OpenNewLauncherDialog( void ) {
	new_launcher_dialog();
}

Bool IsDesktopWindow( Window w ) {
	if (w == Scr.Desktop) return True;
	if (dmenu_active && w == dmenu_win) return True;
	return False;
}

static void dmenu_action(int item) {
	int idx = dmenu_idx;
	int was_icon_menu = dmenu_is_icon;
	hide_dmenu();
	if (item < 0) return;
	if (was_icon_menu) {
		if (item == 0 && idx >= 0) {
			open_icon(idx);
		} else if (item == 1) {
			new_launcher_dialog();
		} else if (item == 2 && idx >= 0) {
			delete_icon(idx);
			full_redraw();
		} else if (item == 3) {
			for (int i = 0; i < nicons; i++) {
				calc_icon_pos(i, &icons[i].x, &icons[i].y);
				icons[i].ox = icons[i].x;
				icons[i].oy = icons[i].y;
			}
			full_redraw();
		} else if (item == 4) {
			full_redraw();
		}
	} else {
		if (item == 0) {
			new_launcher_dialog();
		} else if (item == 1) {
			for (int i = 0; i < nicons; i++) {
				calc_icon_pos(i, &icons[i].x, &icons[i].y);
				icons[i].ox = icons[i].x;
				icons[i].oy = icons[i].y;
			}
			full_redraw();
		} else if (item == 2) {
			full_redraw();
		}
	}
}

static void handle_dmenu_event(XEvent *ev) {
	if (ev->type == Expose && dmenu_active &&
	    ev->xany.window == dmenu_win && ev->xexpose.count == 0)
		draw_dmenu();
}

static void send_drop_focus(void) {
	XClientMessageEvent cm;
	memset(&cm, 0, sizeof(cm));
	cm.type = ClientMessage;
	cm.window = Scr.Root;
	cm.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	cm.format = 32;
	cm.data.l[0] = 0;
	cm.data.l[1] = CurrentTime;
	cm.data.l[2] = 0;
	XSendEvent(dpy, Scr.Root, False,
		SubstructureRedirectMask | SubstructureNotifyMask,
		(XEvent *)&cm);
	XFlush(dpy);
}

void HandleDesktopEvent( XEvent *ev ) {
	if (ev->xany.window == dmenu_win) {
		handle_dmenu_event(ev);
		return;
	}
	if (ev->xany.window != Scr.Desktop) return;

	switch (ev->type) {
	case Expose:
		redraw_region(ev->xexpose.x, ev->xexpose.y,
		              ev->xexpose.width, ev->xexpose.height);
		break;

	case FocusIn:
		XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
		XFlush(dpy);
		break;

	case ButtonPress: {
		send_drop_focus();

		if (ev->xbutton.button == 3) {
			int idx = hit_test(ev->xbutton.x, ev->xbutton.y);
			if (idx >= 0)
				show_dmenu(ev->xbutton.x, ev->xbutton.y, idx,
				          icon_menu_labels, 5);
			else
				show_dmenu(ev->xbutton.x, ev->xbutton.y, -1,
				          empty_menu_labels, 3);
			press_dmenu();
			break;
		}
		if (ev->xbutton.button != 1) break;

		{
			int idx = hit_test(ev->xbutton.x, ev->xbutton.y);
			unsigned int state = ev->xbutton.state;
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
					drag_start_x = ev->xbutton.x;
					drag_start_y = ev->xbutton.y;
					drag_old_x = icons[idx].x;
					drag_old_y = icons[idx].y;
					icons[idx].drag_dx = ev->xbutton.x - icons[idx].x;
					icons[idx].drag_dy = ev->xbutton.y - icons[idx].y;
				}
			} else if (sel_count > 0) {
				selection_clear();
				full_redraw();
			}
		}
		break;
	}

	case MotionNotify:
		if (!drag_active || drag_idx < 0) break;

		{
			DeskIcon *d = &icons[drag_idx];
			int nx = ev->xmotion.x - d->drag_dx;
			int ny = ev->xmotion.y - d->drag_dy;

			if (nx < 0) nx = 0;
			if (ny < 0) ny = 0;
			if (nx > sw - ICON_WIN_W) nx = sw - ICON_WIN_W;
			if (ny > sh - ICON_WIN_H) ny = sh - ICON_WIN_H;

			int dx = nx - d->x;
			int dy = ny - d->y;
			if (dx == 0 && dy == 0) break;

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

			XGrabServer(dpy);

			XftDraw *draw = XftDrawCreate(dpy, Scr.Desktop,
				DefaultVisual(dpy, Scr.screen), DefaultColormap(dpy, Scr.screen));
			if (draw) {
				if (any)
					XClearArea(dpy, Scr.Desktop, minx, miny, bw, bh, False);

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

				if (any) {
					for (int i = 0; i < nicons; i++) {
						if (icons[i].selected) continue;
						if (rects_overlap(icons[i].x, icons[i].y,
						                  ICON_WIN_W, ICON_WIN_H,
						                  minx, miny, bw, bh))
							paint_icon(draw, i);
					}
				}

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
			int moved = (abs(ev->xbutton.x - drag_start_x) > 3 ||
			             abs(ev->xbutton.y - drag_start_y) > 3);

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
					open_icon(drag_idx);
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

void DestroyDesktop( void ) {
	int i;
	for (i = 0; i < nicons; i++) {
		if (icons[i].icon) {
			imlib_context_set_image(icons[i].icon);
			imlib_free_image();
		}
	}
	if (wallpaper_pm != None) {
		XFreePixmap(dpy, wallpaper_pm);
		wallpaper_pm = None;
	}
	if (dmenu_gc) { XFreeGC(dpy, dmenu_gc); dmenu_gc = None; }
	if (dmenu_win) { XDestroyWindow(dpy, dmenu_win); dmenu_win = None; }
	if (sel_gc) { XFreeGC(dpy, sel_gc); sel_gc = None; }
	if (field_bg_gc) { XFreeGC(dpy, field_bg_gc); field_bg_gc = None; }
	if (field_bg_active_gc) { XFreeGC(dpy, field_bg_active_gc); field_bg_active_gc = None; }
	if (placeholder_gc) { XFreeGC(dpy, placeholder_gc); placeholder_gc = None; }
	if (xft_font) { XftFontClose(dpy, xft_font); xft_font = NULL; }
	XftColorFree(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), &xft_black);
	XftColorFree(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), &xft_white);
	XftColorFree(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), &xft_gray);
}

void InitDesktop( void ) {
	const char *home;
	char def[512];
	const char *cfgfile = NULL;
	const char *bg_mode = "fill";
	const char *bg_path = NULL;

	nicons = 0;
	sw = Scr.MyDisplayWidth;
	sh = Scr.MyDisplayHeight;
	max_rows = (sh - MARGIN_TOP) / ROW_H;
	if (max_rows < 1) max_rows = 1;

	a_xrootpmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	a_esetroot  = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, Scr.screen));
	imlib_context_set_colormap(DefaultColormap(dpy, Scr.screen));

	xft_font = XftFontOpenName(dpy, Scr.screen, "Charcoal-9");
	if (!xft_font) xft_font = XftFontOpenName(dpy, Scr.screen, "Charcoal-10");
	if (!xft_font) xft_font = XftFontOpenName(dpy, Scr.screen, "sans-9");
	if (!xft_font) xft_font = XftFontOpenName(dpy, Scr.screen, "fixed-9");

	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "black", &xft_black);
	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "white", &xft_white);
	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "#666666", &xft_gray);

	{
		XColor scol;
		if (XAllocNamedColor(dpy, DefaultColormap(dpy, Scr.screen),
		                     "#3a6cd8", &scol, &scol))
			sel_pixel = scol.pixel;
		else
			sel_pixel = WhitePixel(dpy, Scr.screen);
		sel_gc = XCreateGC(dpy, Scr.Root, 0, NULL);
	}

	{
		XColor sc;
		field_bg_gc = XCreateGC(dpy, Scr.Root, 0, NULL);
		XSetForeground(dpy, field_bg_gc, WhitePixel(dpy, Scr.screen));
		field_bg_active_gc = XCreateGC(dpy, Scr.Root, 0, NULL);
		if (XAllocNamedColor(dpy, DefaultColormap(dpy, Scr.screen),
		                     "#ffffe0", &sc, &sc))
			XSetForeground(dpy, field_bg_active_gc, sc.pixel);
		else
			XSetForeground(dpy, field_bg_active_gc, WhitePixel(dpy, Scr.screen));
		placeholder_gc = XCreateGC(dpy, Scr.Root, 0, NULL);
		if (XAllocNamedColor(dpy, DefaultColormap(dpy, Scr.screen),
		                     "#888888", &sc, &sc))
			XSetForeground(dpy, placeholder_gc, sc.pixel);
		else
			XSetForeground(dpy, placeholder_gc, BlackPixel(dpy, Scr.screen));
	}

	home = getenv("HOME");
	if (!home) home = "/tmp";

	{
		static const char *probe_paths[] = {
			NULL,
			NULL,
			NULL,
			NULL
		};
		char *xdg_cfg = xdg_config_home();
		char *oxwm_cfg = NULL;
		char *mlvwm_cfg = NULL;
		char *legacy_cfg = NULL;
		int n = 0;
		if (xdg_cfg) {
			oxwm_cfg = malloc(strlen(xdg_cfg) + 20);
			sprintf(oxwm_cfg, "%s/oxwm/desktop.conf", xdg_cfg);
			probe_paths[n++] = oxwm_cfg;
		}
		if (home) {
			mlvwm_cfg = malloc(strlen(home) + 30);
			sprintf(mlvwm_cfg, "%s/.mlvwm/desktop.conf", home);
			probe_paths[n++] = mlvwm_cfg;
			legacy_cfg = malloc(strlen(home) + 30);
			sprintf(legacy_cfg, "%s/.oxwm-desktop.cfg", home);
			probe_paths[n++] = legacy_cfg;
		}
		probe_paths[n] = ".oxwm-desktop.cfg";
		for (int p = 0; p <= n; p++) {
			FILE *probe = fopen(probe_paths[p], "r");
			if (!probe) continue;
			char line[256];
			while (fgets(line, sizeof(line), probe)) {
				if (line[0] == '#' || line[0] == '\n') continue;
				char *nl = strchr(line, '\n');
				if (nl) *nl = '\0';
				char *eq = strchr(line, '=');
				if (!eq) continue;
				*eq = '\0';
				char *k = line;
				char *v = eq + 1;
				while (*k == ' ' || *k == '\t') k++;
				char *ke = k + strlen(k) - 1;
				while (ke >= k && (*ke == ' ' || *ke == '\t' || *ke == '\r')) {
					*ke-- = '\0';
				}
				while (*v == ' ' || *v == '\t') v++;
				char *ve = v + strlen(v) - 1;
				while (ve >= v && (*ve == ' ' || *ve == '\t' || *ve == '\r')) {
					*ve-- = '\0';
				}
				if (!strcmp(k, "wallpaper") && v[0]) {
					bg_path = v;
				} else if (!strcmp(k, "wallpaper_mode") && v[0]) {
					bg_mode = v;
				} else if (!strcmp(k, "desktop_config") && v[0]) {
					cfgfile = v;
				}
			}
			fclose(probe);
			break;
		}
		free(xdg_cfg);
		free(oxwm_cfg);
		free(mlvwm_cfg);
		free(legacy_cfg);
	}

	if (bg_path && bg_path[0]) {
		char *wp = bg_path[0] == '~' ? expand_path(bg_path) : strdup(bg_path);
		wallpaper_pm = load_wallpaper(wp, bg_mode, sw, sh);
		free(wp);
	}
	if (wallpaper_pm == None) {
		char *def = find_default_wallpaper();
		if (def) {
			wallpaper_pm = load_wallpaper(def, bg_mode, sw, sh);
			fprintf(stderr, "oxwm-desktop: using default wallpaper %s\n", def);
			free(def);
		}
	}

	{
		XSetWindowAttributes wa;
		unsigned long vm;
		wa.override_redirect = False;
		wa.event_mask = ButtonPressMask | ButtonReleaseMask |
			Button1MotionMask | ExposureMask | StructureNotifyMask |
			FocusChangeMask;
		wa.bit_gravity = NorthWestGravity;
		wa.backing_store = WhenMapped;
		vm = CWEventMask | CWBitGravity | CWBackingStore;
		if (wallpaper_pm != None) {
			wa.background_pixmap = wallpaper_pm;
			vm |= CWBackPixmap;
		} else {
			XColor scol;
			if (XAllocNamedColor(dpy, DefaultColormap(dpy, Scr.screen),
			                     "#D4D0C8", &scol, &scol))
				wa.background_pixel = scol.pixel;
			else
				wa.background_pixel = WhitePixel(dpy, Scr.screen);
			vm |= CWBackPixel;
		}
		Scr.Desktop = XCreateWindow(dpy, Scr.Root, 0, 0, sw, sh, 0,
			CopyFromParent, InputOutput, CopyFromParent, vm, &wa);
	}

	XLowerWindow(dpy, Scr.Desktop);
	XMapWindow(dpy, Scr.Desktop);

	imlib_context_set_drawable(Scr.Desktop);

	{
		Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr);
		XDefineCursor(dpy, Scr.Desktop, arrow);
	}

	if (wallpaper_pm != None) {
		XChangeProperty(dpy, Scr.Root, a_xrootpmap, XA_PIXMAP, 32,
			PropModeReplace, (unsigned char *)&wallpaper_pm, 1);
		XChangeProperty(dpy, Scr.Root, a_esetroot, XA_PIXMAP, 32,
			PropModeReplace, (unsigned char *)&wallpaper_pm, 1);
		XSetWindowBackgroundPixmap(dpy, Scr.Root, wallpaper_pm);
	}

	if (!cfgfile) {
		char *xdg_cfg = xdg_config_home();
		if (xdg_cfg) {
			snprintf(def, sizeof(def), "%s/oxwm/desktop.conf", xdg_cfg);
			free(xdg_cfg);
		} else {
			snprintf(def, sizeof(def), "%s/.mlvwm/desktop.conf", home);
		}
		cfgfile = def;
	}
	config_path_in_use = cfgfile;

	if (!load_config(cfgfile)) {
		fprintf(stderr, "oxwm-desktop: no icons loaded\n");
	}

	add_trash_icon();

	full_redraw();
}
