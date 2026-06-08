#ifndef _DESKTOP_
#define _DESKTOP_

extern void InitDesktop( void );
extern void HandleDesktopEvent( XEvent *ev );
extern void OpenNewLauncherDialog( void );
extern void DestroyDesktop( void );
extern Bool IsDesktopWindow( Window w );
extern void RedrawDesktopIcons( void );
extern void RedrawDesktopRegion( int x, int y, int w, int h );

#endif
