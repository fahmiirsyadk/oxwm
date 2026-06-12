#ifndef _DESKTOP_
#define _DESKTOP_

#include <X11/Xlib.h>

extern void InitDesktop( void );
extern void HandleDesktopEvent( XEvent *ev );
extern void OpenNewLauncherDialog( void );
extern void DestroyDesktop( void );
extern Bool IsDesktopWindow( Window w );
extern void RedrawDesktopIcons( void );
extern void RedrawDesktopRegion( int x, int y, int w, int h );

/* Shared icon/path helpers (used by both the desktop wallpaper/icon
 * subsystem and the bottom dock). */
extern const char *home_dir(void);
extern char       *xdg_data_home(void);
extern char       *expand_path(const char *path);
extern char       *find_icon_in_theme(const char *name);

#endif
