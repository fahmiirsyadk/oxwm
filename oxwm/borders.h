/****************************************************************************/
/* This module is all original code                                         */
/* by TakaC Hasegawa (tac.hasegawa@gmail.com)                               */
/* Copyright 1996, TakaC Hasegawa                                           */
/*     You may use this code for any purpose, as long as the original       */
/*     copyright remains in the source code and all documentation           */
/****************************************************************************/
#ifndef _BORDERS_
#define _BORDERS_

#define SHADOW_BOTTOM   1
#define SHADOW_LEFT     2
#define SHADOW_RIGHT    4
#define SHADOW_TOP      8
#define SHADOW_ALL      (SHADOW_BOTTOM|SHADOW_LEFT|SHADOW_RIGHT|SHADOW_TOP)

extern void SetShape( OxwmWindow *, int );
extern void SetUpFrame( OxwmWindow *, int, int, int, int, Bool );
extern void SetTitleBar( OxwmWindow *, Bool );
extern void DrawArrow( Window, int, GC, GC );
extern void DrawSbarAnk( OxwmWindow *, int, Bool );
extern void DrawSbarArrow( OxwmWindow *, int, Bool );
extern void DrawSbarBar( OxwmWindow *, int, Bool );
extern void DrawResizeBox( OxwmWindow *, Bool );
extern void DrawAllDecorations( OxwmWindow *, Bool );
extern void DrawFrameShadow( OxwmWindow *, Bool );
extern void SetFocus( OxwmWindow * );
extern void DrawShadowBox( int, int, int, int, Window, int, GC, GC, char );
extern void DrawMinMax( OxwmWindow *, Bool );
extern void DrawCloseBox( OxwmWindow *, Bool );
extern void DrawShadeR( OxwmWindow *, Bool );

void DrawWindowFrame( Window w, int fw, int fh );
void DrawTitleBarCore( Window w, int fw, int th, const char *title, int active );
void DrawCloseButton( Window w, int cx, int cy, int size );
void DrawInputField( Window w, int x, int y, int fw, int fh,
                     const char *text, int active, GC fill_gc );
void DrawPushButton( Window w, int x, int y, int fw, int fh,
                     const char *label, int active );
#endif
