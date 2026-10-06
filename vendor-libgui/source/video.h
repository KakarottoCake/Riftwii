/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * video.h
 * Video routines
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: StopGXKeepPicture, the frame buffer accessors and
 * the display scale.
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */

#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <ogcsys.h>

void InitVideo ();
void StopGX();
void StopGXKeepPicture();
void * Menu_CurrentXfb();
int Menu_XfbWidth();
int Menu_XfbHeight();
int Menu_EfbHeight();  // RiftWii: for scissor boxes (scrolling lists)
// RiftWii: the display scale (widescreen and screen size), see video.cpp.
void Menu_SetDisplayScale(f32 sx, f32 sy);
void Menu_ScaleProjection(Mtx44 p);
void Menu_LoadOrtho();
f32 Menu_ScreenToMenuX(f32 x);
f32 Menu_ScreenToMenuY(f32 y);
void Menu_VisibleArea(f32* x, f32* y, f32* w, f32* h);  // the whole screen, in menu units
void Menu_FillScreen(f32 y, f32 height, GXColor color);  // a band across the whole screen
void Menu_FillWholeScreen(GXColor color);
void Menu_Scissor(f32 x, f32 y, f32 w, f32 h);  // a scissor box in menu units
void ResetVideo_Menu();
void Menu_Render();
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, u8 data[], f32 degrees, f32 scaleX, f32 scaleY, u8 alphaF );
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled);

extern int screenheight;
extern int screenwidth;
extern u32 FrameTimer;

#endif
