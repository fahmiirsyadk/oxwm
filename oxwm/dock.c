#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#include "oxwm.h"
#include "screen.h"
#include "misc.h"
#include "borders.h"
#include "functions.h"
#include "event.h"
#include "desktop.h"
#include "dock.h"

/* Mac OS 9 Control Strip / Applications Drawer dimensions */
#define DOCK_BAR_H         32
#define DOCK_ITEM_SIZE     32
#define DOCK_PAD           4
#define DOCK_BORDER        1
#define DOCK_HANDLE_W      12
#define DOCK_HANDLE_EXTEND 15
#define DOCK_ARROW_W       6
#define DOCK_LEFT_BTN      16
#define DOCK_MAX_ITEMS     64
#define DOCK_MENU_ITEM_H   22
#define DOCK_MENU_W        140

/* Mac OS 9 Platinum colors */
#define PLATINUM   0xC0C0C0
#define WHITE      0xFFFFFF
#define SHADOW     0x808080
#define BLACK      0x000000

static DockItem *dock_root = NULL;
static int       dock_nitems = 0;
static XftFont  *dock_xft_font = NULL;
static XftColor  dock_xft_black, dock_xft_white, dock_xft_gray;
static GC        dock_gc = NULL;
static Pixmap    dock_backing = None;
static int       dock_backing_w = 0, dock_backing_h = 0;
static int       dock_drag_idx = -1;
static int       dock_hover_idx = -1;

static Window    dock_menu_win = None;
static GC        dock_menu_gc = NULL;
static int       dock_menu_active = 0;
static int       dock_menu_idx = -1;
static int       dock_menu_hovered = -1;
static int       dock_menu_count = 0;
static int       dock_menu_x = 0, dock_menu_y = 0;
static const char *dock_menu_labels[] = {
	"Launch", "Edit...", "Remove", NULL
};
static int       dock_menu_actions[] = { 0, 1, 2 };

static int dock_xft_init(void) {
	if (dock_xft_font) return 1;
	dock_xft_font = XftFontOpen(dpy, Scr.screen,
		XFT_FAMILY, XftTypeString, "sans-serif",
		XFT_SIZE, XftTypeDouble, 9.0,
		NULL);
	if (!dock_xft_font) {
		dock_xft_font = XftFontOpen(dpy, Scr.screen,
			XFT_FAMILY, XftTypeString, "sans",
			XFT_SIZE, XftTypeDouble, 9.0,
			NULL);
	}
	if (!dock_xft_font) return 0;

	XRenderColor rblack = { 0, 0, 0, 0xffff };
	XRenderColor rwhite = { 0xffff, 0xffff, 0xffff, 0xffff };
	XRenderColor rgray  = { 0x8888, 0x8888, 0x8888, 0xffff };
	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "#000000", &dock_xft_black);
	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "#ffffff", &dock_xft_white);
	XftColorAllocName(dpy, DefaultVisual(dpy, Scr.screen),
		DefaultColormap(dpy, Scr.screen), "#888888", &dock_xft_gray);
	(void)rblack; (void)rwhite; (void)rgray;

	dock_gc = XCreateGC(dpy, Scr.Root, 0, NULL);
	XSetForeground(dpy, dock_gc, BlackPixel(dpy, Scr.screen));
	return 1;
}

static int dock_load_icon(DockItem *it, const char *icon_raw) {
	char *icon_path = NULL;
	if (strchr(icon_raw, '/')) {
		icon_path = expand_path(icon_raw);
	} else {
		icon_path = find_icon_in_theme(icon_raw);
		if (!icon_path) icon_path = strdup(icon_raw);
	}
	if (!icon_path) return 0;

	Imlib_Image src = imlib_load_image(icon_path);
	if (!src) {
		fprintf(stderr, "oxwm-dock: skip icon '%s' (tried '%s')\n",
			icon_raw, icon_path);
		free(icon_path);
		return 0;
	}
	free(icon_path);

	imlib_context_set_image(src);
	int iw = imlib_image_get_width();
	int ih = imlib_image_get_height();

	int nw = Scr.DockItemSize, nh = Scr.DockItemSize;
	if (iw > ih) {
		nh = (int)round((double)ih * Scr.DockItemSize / iw);
		if (nh < 1) nh = 1;
	} else {
		nw = (int)round((double)iw * Scr.DockItemSize / ih);
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

	it->icon = scaled;
	it->iw = nw;
	it->ih = nh;
	return 1;
}

static void dock_item_to_pixmap(DockItem *it) {
	if (!it->icon) return;
	int w = Scr.DockItemSize;
	int h = Scr.DockItemSize;

	if (it->pix) XFreePixmap(dpy, it->pix);
	it->pix = XCreatePixmap(dpy, Scr.Root, w, h,
		DefaultDepth(dpy, Scr.screen));
	it->pw = w; it->ph = h;

	int ix = (w - it->iw) / 2;
	int iy = (h - it->ih) / 2;
	imlib_context_set_image(it->icon);
	imlib_context_set_drawable(it->pix);
	imlib_render_image_on_drawable(ix, iy);
}

static int dock_total_width(void) {
	int items_w = dock_nitems * Scr.DockItemSize;
	int arrows_w = (dock_nitems - 1) * DOCK_ARROW_W;
	if (dock_nitems <= 1) arrows_w = 0;
	return DOCK_LEFT_BTN + DOCK_PAD +
		items_w + arrows_w + (dock_nitems * DOCK_PAD) +
		DOCK_PAD + DOCK_HANDLE_W + DOCK_BORDER * 4;
}

static void dock_layout(int *out_w, int *out_h) {
	int w = dock_nitems > 0 ? dock_total_width() : DOCK_LEFT_BTN + DOCK_HANDLE_W + DOCK_BORDER * 6;
	int h = DOCK_BAR_H + DOCK_HANDLE_EXTEND;
	if (w < 4) w = 4;
	*out_w = w;
	*out_h = h;
}

static void dock_paint_item(DockItem *it, int dst_x, int dst_y) {
	if (!it || !it->pix) return;
	XCopyArea(dpy, it->pix, dock_backing, dock_gc,
		0, 0, it->pw, it->ph, dst_x, dst_y);
}

static void dock_paint_selection(int x, int y, int w, int h) {
	if (x < 0) return;
	XGCValues gcv;
	unsigned long gcm = GCForeground;
	gcv.foreground = BLACK;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	gcv.line_style = LineDoubleDash;
	XChangeGC(dpy, dock_gc, GCLineStyle, &gcv);
	XDrawRectangle(dpy, dock_backing, dock_gc, x - 1, y - 1, w + 1, h + 1);
	gcv.line_style = LineSolid;
	XChangeGC(dpy, dock_gc, GCLineStyle, &gcv);
}

static void dock_rebuild_backing(int w, int h) {
	if (dock_backing) XFreePixmap(dpy, dock_backing);
	dock_backing = XCreatePixmap(dpy, Scr.Root, w, h,
		DefaultDepth(dpy, Scr.screen));
	dock_backing_w = w;
	dock_backing_h = h;

	if (!dock_xft_init()) return;

	XGCValues gcv;
	unsigned long gcm = GCForeground;

	/* Fill entire window with platinum gray */
	gcv.foreground = PLATINUM;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XFillRectangle(dpy, dock_backing, dock_gc, 0, 0, w, h);

	/* Main bar area: from y = DOCK_HANDLE_EXTEND to bottom */
	int bar_y = DOCK_HANDLE_EXTEND;
	int bar_h = h - DOCK_HANDLE_EXTEND;

	/* Main bar bevel: 1px white top, 1px dark gray bottom, 1px black line */
	gcv.foreground = WHITE;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, 0, bar_y, w - 1, bar_y);
	XDrawLine(dpy, dock_backing, dock_gc, 0, bar_y, 0, h - 1);

	gcv.foreground = SHADOW;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, 0, h - 1, w - 1, h - 1);
	XDrawLine(dpy, dock_backing, dock_gc, w - 1, bar_y, w - 1, h - 1);

	gcv.foreground = BLACK;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, 0, h - 2, w - 1, h - 2);
	XDrawLine(dpy, dock_backing, dock_gc, w - 2, bar_y, w - 2, h - 1);

	/* Left button (small square on far left of bar) */
	int lbx = DOCK_BORDER + DOCK_PAD;
	int lby = bar_y + (bar_h - DOCK_LEFT_BTN) / 2;
	int lbw = DOCK_LEFT_BTN;
	int lbh = DOCK_LEFT_BTN;

	gcv.foreground = PLATINUM;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XFillRectangle(dpy, dock_backing, dock_gc, lbx, lby, lbw, lbh);

	gcv.foreground = WHITE;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, lbx, lby, lbx + lbw - 1, lby);
	XDrawLine(dpy, dock_backing, dock_gc, lbx, lby, lbx, lby + lbh - 1);

	gcv.foreground = SHADOW;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, lbx, lby + lbh - 1, lbx + lbw - 1, lby + lbh - 1);
	XDrawLine(dpy, dock_backing, dock_gc, lbx + lbw - 1, lby, lbx + lbw - 1, lby + lbh - 1);

	gcv.foreground = BLACK;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, lbx, lby + lbh - 2, lbx + lbw - 1, lby + lbh - 2);
	XDrawLine(dpy, dock_backing, dock_gc, lbx + lbw - 2, lby, lbx + lbw - 2, lby + lbh - 1);

	/* Vertical handle tab on right side, extending UPWARD from bar */
	int hx = w - DOCK_BORDER * 2 - DOCK_HANDLE_W;
	int hw = DOCK_HANDLE_W;
	int handle_top = 0;
	int handle_bottom = bar_y + DOCK_BAR_H / 2;
	int hh = handle_bottom - handle_top;

	/* Handle background */
	gcv.foreground = PLATINUM;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XFillRectangle(dpy, dock_backing, dock_gc, hx, handle_top, hw, hh);

	/* Handle bevel - white highlight on left */
	gcv.foreground = WHITE;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, hx, handle_top, hx, handle_bottom - 1);
	XDrawLine(dpy, dock_backing, dock_gc, hx, handle_top, hx + hw - 1, handle_top);

	/* Handle bevel - dark shadow on right */
	gcv.foreground = SHADOW;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, hx + hw - 1, handle_top, hx + hw - 1, handle_bottom - 1);
	XDrawLine(dpy, dock_backing, dock_gc, hx, handle_bottom - 1, hx + hw - 1, handle_bottom - 1);

	/* Handle black line */
	gcv.foreground = BLACK;
	XChangeGC(dpy, dock_gc, gcm, &gcv);
	XDrawLine(dpy, dock_backing, dock_gc, hx + hw - 2, handle_top, hx + hw - 2, handle_bottom - 1);
	XDrawLine(dpy, dock_backing, dock_gc, hx, handle_bottom - 2, hx + hw - 1, handle_bottom - 2);

	/* Handle grip lines (3 horizontal lines centered) */
	int grip_cy = handle_top + hh / 2;
	int grip_x1 = hx + 2;
	int grip_x2 = hx + hw - 3;
	for (int i = -3; i <= 3; i += 3) {
		gcv.foreground = BLACK;
		XChangeGC(dpy, dock_gc, gcm, &gcv);
		XDrawLine(dpy, dock_backing, dock_gc,
			grip_x1, grip_cy + i, grip_x2, grip_cy + i);
	}

	/* Items: start after left button, end before handle */
	int content_x = lbx + lbw + DOCK_PAD;
	int content_y = bar_y + (bar_h - Scr.DockItemSize) / 2;
	int content_right = hx - DOCK_PAD;

	int ix = content_x;
	int iy = content_y;

	for (DockItem *it = dock_root; it; it = it->next) {
		if (ix + Scr.DockItemSize > content_right) break;

		dock_paint_item(it, ix, iy);
		it->px = ix;
		it->py = iy;
		ix += Scr.DockItemSize + DOCK_PAD;

		/* Draw arrow separator (▶) between items */
		if (it->next && ix + Scr.DockItemSize <= content_right) {
			gcv.foreground = BLACK;
			XChangeGC(dpy, dock_gc, gcm, &gcv);
			int ax = ix - DOCK_PAD / 2;
			int ay = content_y + Scr.DockItemSize / 2;
			XPoint arrow[3];
			arrow[0].x = ax;
			arrow[0].y = ay - 3;
			arrow[1].x = ax;
			arrow[1].y = ay + 3;
			arrow[2].x = ax + 4;
			arrow[2].y = ay;
			XFillPolygon(dpy, dock_backing, dock_gc,
				arrow, 3, Convex, CoordModeOrigin);
			ix += DOCK_ARROW_W;
		}
	}

	if (dock_hover_idx >= 0 && dock_hover_idx < dock_nitems) {
		DockItem *it = dock_root;
		for (int i = 0; i < dock_hover_idx && it; i++) it = it->next;
		if (it) {
			int sx = it->px - 1;
			int sy = it->py - 1;
			int sw = Scr.DockItemSize + 2;
			int sh = Scr.DockItemSize + 2;
			dock_paint_selection(sx, sy, sw, sh);
		}
	}
}

void RedrawDock(void) {
	if (Scr.DockWin == None) return;
	int w, h;
	dock_layout(&w, &h);
	dock_rebuild_backing(w, h);
	XCopyArea(dpy, dock_backing, Scr.DockWin, dock_gc,
		0, 0, w, h, 0, 0);
	XFlush(dpy);
}

static void dock_create_or_resize(int n) {
	int w, h;
	if (n == 0) {
		w = 4;
		h = DOCK_BAR_H;
	} else {
		dock_layout(&w, &h);
	}

	int x = 0;
	int y = Scr.MyDisplayHeight - h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	if (Scr.DockWin == None) {
		XSetWindowAttributes wa;
		wa.override_redirect = True;
		wa.event_mask = ButtonPressMask | ButtonReleaseMask |
			Button1MotionMask | ExposureMask | EnterWindowMask |
			LeaveWindowMask | PointerMotionMask;
		wa.background_pixel = PLATINUM;
		wa.backing_store = WhenMapped;
		wa.bit_gravity = NorthWestGravity;
		unsigned long vm = CWOverrideRedirect | CWEventMask |
			CWBackPixel | CWBackingStore | CWBitGravity;
		Scr.DockWin = XCreateWindow(dpy, Scr.Root, x, y, w, h, 0,
			CopyFromParent, InputOutput, CopyFromParent, vm, &wa);
		Cursor hand = XCreateFontCursor(dpy, XC_hand2);
		XDefineCursor(dpy, Scr.DockWin, hand);
		XMapWindow(dpy, Scr.DockWin);
		Scr.DockVisible = 1;
	} else {
		XMoveResizeWindow(dpy, Scr.DockWin, x, y, w, h);
		if (!Scr.DockVisible) {
			XMapRaised(dpy, Scr.DockWin);
			Scr.DockVisible = 1;
		}
	}
}

static int dock_hit(int x, int y) {
	if (x < DOCK_BORDER || y < DOCK_BORDER) return -1;
	int rx = x - DOCK_BORDER;
	int stride = Scr.DockItemSize + DOCK_PAD +
		((dock_nitems > 1) ? DOCK_ARROW_W : 0);
	if (rx >= dock_nitems * stride - DOCK_PAD) return -1;
	int idx = rx / stride;
	if (idx < 0 || idx >= dock_nitems) return -1;
	return idx;
}

static void dock_launch(int idx) {
	if (idx < 0 || idx >= dock_nitems) return;
	DockItem *it = dock_root;
	for (int i = 0; i < idx && it; i++) it = it->next;
	if (!it) return;

	char action[1024];
	snprintf(action, sizeof(action), "Exec \"%s\" %s",
		it->label, it->cmd);
	ExecuteFunction(action);
}

static void dock_remove(int idx) {
	if (idx < 0 || idx >= dock_nitems) return;
	DockItem *prev = NULL, *it = dock_root;
	for (int i = 0; i < idx && it; i++) { prev = it; it = it->next; }
	if (!it) return;
	if (prev) prev->next = it->next;
	else dock_root = it->next;
	if (it->icon) {
		imlib_context_set_image(it->icon);
		imlib_free_image();
	}
	if (it->pix) XFreePixmap(dpy, it->pix);
	free(it);
	dock_nitems--;
	Scr.DockNItems = dock_nitems;
	if (dock_nitems == 0) {
		if (Scr.DockWin != None) {
			XUnmapWindow(dpy, Scr.DockWin);
			Scr.DockVisible = 0;
		}
	} else {
		dock_create_or_resize(dock_nitems);
		RedrawDock();
	}
}

static void dock_edit(int idx) {
	if (idx < 0 || idx >= dock_nitems) return;
	DockItem *it = dock_root;
	for (int i = 0; i < idx && it; i++) it = it->next;
	if (!it) return;
	fprintf(stderr, "oxwm-dock: edit '%s' (not yet implemented in GUI)\n",
		it->label);
}

static void dock_menu_create(void) {
	if (dock_menu_win != None) return;
	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.event_mask = ExposureMask;
	wa.background_pixel = WhitePixel(dpy, Scr.screen);
	wa.border_pixel = BlackPixel(dpy, Scr.screen);
	dock_menu_win = XCreateWindow(dpy, Scr.Root, 0, 0,
		DOCK_MENU_W, DOCK_MENU_ITEM_H * 3, 1,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel,
		&wa);
	dock_menu_gc = XCreateGC(dpy, dock_menu_win, 0, NULL);
	Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, dock_menu_win, arrow);
}

static void dock_menu_paint(void) {
	for (int i = 0; i < dock_menu_count; i++) {
		int iy = i * DOCK_MENU_ITEM_H;
		if (i == dock_menu_hovered) {
			XSetForeground(dpy, dock_menu_gc, 0x000080);
			XFillRectangle(dpy, dock_menu_win, dock_menu_gc,
				1, iy + 1, DOCK_MENU_W - 2, DOCK_MENU_ITEM_H - 2);
			XSetForeground(dpy, dock_menu_gc, WhitePixel(dpy, Scr.screen));
		} else {
			XSetForeground(dpy, dock_menu_gc, WhitePixel(dpy, Scr.screen));
			XFillRectangle(dpy, dock_menu_win, dock_menu_gc,
				1, iy + 1, DOCK_MENU_W - 2, DOCK_MENU_ITEM_H - 2);
			XSetForeground(dpy, dock_menu_gc, BlackPixel(dpy, Scr.screen));
		}
		XDRAWSTRING(dpy, dock_menu_win, MENUFONT, dock_menu_gc, 8, iy + 15,
			dock_menu_labels[i], strlen(dock_menu_labels[i]));
	}
	XSetForeground(dpy, dock_menu_gc, BlackPixel(dpy, Scr.screen));
	for (int i = 1; i < dock_menu_count; i++) {
		int y = i * DOCK_MENU_ITEM_H;
		XDrawLine(dpy, dock_menu_win, dock_menu_gc, 1, y, DOCK_MENU_W - 1, y);
	}
}

static int dock_menu_hit(int root_x, int root_y) {
	int lx = root_x - dock_menu_x;
	int ly = root_y - dock_menu_y;
	if (lx < 0 || lx >= DOCK_MENU_W) return -1;
	if (ly < 0 || ly >= dock_menu_count * DOCK_MENU_ITEM_H) return -1;
	return ly / DOCK_MENU_ITEM_H;
}

static void dock_menu_show(int x, int y, int item_idx) {
	if (!dock_menu_win) dock_menu_create();
	dock_menu_active = 1;
	dock_menu_idx = item_idx;
	dock_menu_count = 3;
	dock_menu_hovered = -1;
	dock_menu_x = x;
	dock_menu_y = y - DOCK_MENU_ITEM_H * 3;
	if (dock_menu_y < 0) dock_menu_y = y;
	if (dock_menu_x + DOCK_MENU_W > Scr.MyDisplayWidth)
		dock_menu_x = Scr.MyDisplayWidth - DOCK_MENU_W - 2;
	if (dock_menu_y + dock_menu_count * DOCK_MENU_ITEM_H > Scr.MyDisplayHeight)
		dock_menu_y = Scr.MyDisplayHeight - dock_menu_count * DOCK_MENU_ITEM_H - 2;
	XMoveResizeWindow(dpy, dock_menu_win, dock_menu_x, dock_menu_y,
		DOCK_MENU_W, dock_menu_count * DOCK_MENU_ITEM_H);
	XMapRaised(dpy, dock_menu_win);
	dock_menu_paint();
}

static void dock_menu_hide(void) {
	if (dock_menu_win) XUnmapWindow(dpy, dock_menu_win);
	dock_menu_active = 0;
	dock_menu_idx = -1;
	dock_menu_hovered = -1;
}

static void dock_menu_press(void) {
	XEvent ev;
	int ignore = 1;
	int activated = -1;

	if (!GrabEvent(DEFAULT)) {
		XBell(dpy, 30);
		dock_menu_hide();
		return;
	}

	int done = 0;
	while (!done) {
		XMaskEvent(dpy,
			ExposureMask | ButtonReleaseMask | ButtonPressMask |
			PointerMotionMask | ButtonMotionMask, &ev);

		switch (ev.type) {
		case Expose:
			dock_menu_paint();
			break;
		case MotionNotify:
			dock_menu_hovered = dock_menu_hit(
				ev.xmotion.x_root, ev.xmotion.y_root);
			dock_menu_paint();
			break;
		case ButtonRelease:
			if (ignore) { ignore = 0; break; }
			activated = dock_menu_hit(
				ev.xbutton.x_root, ev.xbutton.y_root);
			done = 1;
			break;
		case ButtonPress:
			break;
		}
	}
	UnGrabEvent();

	int idx = dock_menu_idx;
	dock_menu_hide();
	RedrawDock();
	if (activated < 0) return;
	switch (activated) {
	case 0: dock_launch(idx); break;
	case 1: dock_edit(idx); break;
	case 2: dock_remove(idx); break;
	}
}

void HandleDockEvent(XEvent *ev) {
	if (Scr.DockWin == None) return;

	switch (ev->type) {
	case Expose:
		if (ev->xexpose.count == 0) RedrawDock();
		break;

	case MotionNotify:
		{
			int idx = dock_hit(ev->xmotion.x, ev->xmotion.y);
			if (idx != dock_hover_idx) {
				dock_hover_idx = idx;
				RedrawDock();
			}
		}
		break;

	case LeaveNotify:
		if (dock_hover_idx != -1) {
			dock_hover_idx = -1;
			RedrawDock();
		}
		break;

	case ButtonPress:
		{
			int idx = dock_hit(ev->xbutton.x, ev->xbutton.y);
			if (ev->xbutton.button == 3 && idx >= 0) {
				int rx, ry;
				Window child;
				XTranslateCoordinates(dpy, Scr.DockWin, Scr.Root,
                                        ev->xbutton.x, ev->xbutton.y, &rx, &ry, &child);
				dock_menu_show(rx, ry, idx);
				dock_menu_press();
			} else if (ev->xbutton.button == 1 && idx >= 0) {
				dock_drag_idx = idx;
			}
		}
		break;

	case ButtonRelease:
		if (ev->xbutton.button == 1 && dock_drag_idx >= 0) {
			int idx = dock_hit(ev->xbutton.x, ev->xbutton.y);
			if (idx == dock_drag_idx) {
				dock_launch(idx);
			}
			dock_drag_idx = -1;
		}
		break;
	}
}

Bool IsDockWindow(Window w) {
	if (w == Scr.DockWin) return True;
	if (dock_menu_active && w == dock_menu_win) return True;
	return False;
}

void DockToggleVisible(void) {
	if (Scr.DockWin == None) return;
	if (Scr.DockVisible) {
		XUnmapWindow(dpy, Scr.DockWin);
		Scr.DockVisible = 0;
	} else {
		XMapRaised(dpy, Scr.DockWin);
		Scr.DockVisible = 1;
		RedrawDock();
	}
}

static void dock_clear_items(void) {
	while (dock_root) {
		DockItem *it = dock_root;
		dock_root = it->next;
		if (it->icon) {
			imlib_context_set_image(it->icon);
			imlib_free_image();
		}
		if (it->pix) XFreePixmap(dpy, it->pix);
		free(it);
	}
	dock_nitems = 0;
	Scr.DockNItems = 0;
}

int AddDockItem(const char *icon, const char *label, const char *cmd) {
	if (dock_nitems >= DOCK_MAX_ITEMS) return -1;

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, Scr.screen));
	imlib_context_set_colormap(DefaultColormap(dpy, Scr.screen));
	imlib_context_set_drawable(Scr.Root);

	DockItem *it = calloc(1, sizeof(DockItem));
	if (!it) return -1;

	strncpy(it->label, label, 127);
	it->label[127] = '\0';
	strncpy(it->icon_name, icon, 127);
	it->icon_name[127] = '\0';
	strncpy(it->cmd, cmd, 511);
	it->cmd[511] = '\0';
	it->next = NULL;

	if (!dock_load_icon(it, icon)) {
		fprintf(stderr, "oxwm-dock: could not load icon for '%s'\n", label);
		free(it);
		return -1;
	}

	if (dock_xft_init()) dock_item_to_pixmap(it);

	DockItem **tail = &dock_root;
	while (*tail) tail = &(*tail)->next;
	*tail = it;
	dock_nitems++;
	Scr.DockNItems = dock_nitems;
	return 0;
}

void InitDock(void) {
	if (Scr.DockItemSize == 0) Scr.DockItemSize = DOCK_ITEM_SIZE;
	if (Scr.DockHeight == 0) Scr.DockHeight = DOCK_BAR_H;
	if (Scr.DockPadding == 0) Scr.DockPadding = DOCK_PAD;
	Scr.DockWin = None;
	Scr.DockVisible = 0;

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, Scr.screen));
	imlib_context_set_colormap(DefaultColormap(dpy, Scr.screen));
	imlib_context_set_drawable(Scr.Root);

	if (!dock_xft_init()) {
		fprintf(stderr, "oxwm-dock: failed to init Xft, dock disabled\n");
		return;
	}

	if (dock_nitems == 0) {
		Scr.DockHeight = DOCK_BAR_H;
	} else {
		dock_create_or_resize(dock_nitems);
		RedrawDock();
	}
}

void DestroyDock(void) {
	dock_menu_hide();
	if (dock_menu_win) { XDestroyWindow(dpy, dock_menu_win); dock_menu_win = None; }
	if (dock_menu_gc) { XFreeGC(dpy, dock_menu_gc); dock_menu_gc = NULL; }
	if (Scr.DockWin) { XDestroyWindow(dpy, Scr.DockWin); Scr.DockWin = None; }
	if (dock_backing) { XFreePixmap(dpy, dock_backing); dock_backing = None; }
	if (dock_gc) { XFreeGC(dpy, dock_gc); dock_gc = NULL; }
	if (dock_xft_font) {
		XftFontClose(dpy, dock_xft_font);
		dock_xft_font = NULL;
	}
	dock_clear_items();
}