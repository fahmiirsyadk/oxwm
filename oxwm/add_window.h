/****************************************************************************
 * This module is all original code 
 * by TakaC Hasegawa (tac.hasegawa@gmail.com)
 * Copyright 1996, TakaC Hasegawa
 *     You may use this code for any purpose, as long as the original
 *     copyright remains in the source code and all documentation
 ****************************************************************************/
#ifndef _ADD_WINDOW_
#define _ADD_WINDOW_

extern void FetchWmProtocols( OxwmWindow * );
extern styles *lookupstyles( char *, XClassHint * );
extern void create_resizebox( OxwmWindow * );
extern void create_scrollbar( OxwmWindow * );
extern void create_titlebar( OxwmWindow * );
extern OxwmWindow *AddWindow( Window );
extern void GetWindowSizeHints( OxwmWindow * );

extern char NoName[];
#endif
