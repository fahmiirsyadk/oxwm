/****************************************************************************/
/* This module is all original code                                         */
/* by TakaC Hasegawa (tac.hasegawa@gmail.com)                               */
/* Copyright 1996, TakaC Hasegawa                                           */
/*     You may use this code for any purpose, as long as the original       */
/*     copyright remains in the source code and all documentation           */
/****************************************************************************/
#ifndef _EVENT_
#define _EVENT_

extern void InstallWindowColormaps (OxwmWindow * );
extern Bool GrabEvent( int );
extern void UnGrabEvent( void );
extern void RestoreWithdrawnLocation( OxwmWindow *, Bool );
extern int GetContext( OxwmWindow *, XEvent *, Window * );
extern void Destroy( OxwmWindow * );
extern void HandleDestroy( XEvent * );
extern void handle_configure_request( XConfigureRequestEvent );
extern void MoveWindow( OxwmWindow *, XEvent *, Bool );
extern void DisplayPush( Window );
extern void CloseWindow( OxwmWindow *, XEvent * );
extern void DrawResizeFrame( int, int, int, int );
extern void ResizeWindow( OxwmWindow *, XEvent *, Bool );
extern void MinMaxWindow( OxwmWindow *, XEvent * );
extern void HandleMapRequest( Window );
extern void handle_button_press( XEvent * );
extern void handle_expose( XEvent * );
extern void HandleEnterNotify( XEvent * );
extern void HandleLeaveNotify( XEvent * );
extern void HandleShapeNotify ( XEvent * );
extern void HandleEvents( XEvent );
extern void send_clientmessage (Window, Atom, Time);
extern void WaitEvents( void );
extern OxwmWindow *NextActiveWin( OxwmWindow * );
extern void SetMapStateProp( OxwmWindow *, int );
extern void GetWindowSizeHints( OxwmWindow * );
#endif
