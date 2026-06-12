#ifndef _DOCK_H_
#define _DOCK_H_

#include <Imlib2.h>
#include "oxwm.h"

typedef struct DockItem {
	Imlib_Image    icon;
	int            iw, ih;
	Pixmap         pix;
	int            pw, ph;
	int            px, py;
	char           label[128];
	char           icon_name[128];
	char           cmd[512];
	struct DockItem *next;
} DockItem;

void InitDock(void);
void DestroyDock(void);
void RedrawDock(void);
void HandleDockEvent(XEvent *ev);
Bool IsDockWindow(Window w);
void DockToggleVisible(void);
int  AddDockItem(const char *icon, const char *label, const char *cmd);

#endif
