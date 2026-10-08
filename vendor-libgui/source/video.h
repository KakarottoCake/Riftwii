/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * video.h
 * Video routines
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: StopGXKeepPicture, the frame buffer accessors,
 * the display scale and the camera.
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
// RiftWii: the camera (video.cpp). Set: everything drawn goes to
// k * p + (tx, ty), in menu units. Push: scaled by k about (cx, cy) and
// moved by (dx, dy) inside the current one; PushNoCamera: none, for what stays put (the backdrop,
// a popup's dimming). GUI thread only, while drawing.
void Menu_SetCamera(f32 k, f32 tx, f32 ty);
void Menu_PushCamera(f32 k, f32 cx, f32 cy, f32 dx = 0, f32 dy = 0);
void Menu_PushNoCamera();
void Menu_PopCamera();
f32 Menu_ScreenToMenuX(f32 x);
f32 Menu_ScreenToMenuY(f32 y);
void Menu_MenuToXfb(f32 x, f32 y, int* px, int* py);  // a menu point as a frame-buffer pixel
void Menu_VisibleArea(f32* x, f32* y, f32* w, f32* h);  // the whole screen, in menu units
void Menu_FillScreen(f32 y, f32 height, GXColor color);  // a band across the whole screen
void Menu_FillWholeScreen(GXColor color);
void Menu_Scissor(f32 x, f32 y, f32 w, f32 h);  // a scissor box in menu units
void Menu_SafeArea(f32* x, f32* w);  // where the menu's corners go across: wider on a widescreen menu
// Texels [u0, u1) x [v0, v1) (0-1; reversed for a mirror image) of a w x h
// RGBA8 picture, on the rectangle at x, y, w by h.
void Menu_DrawImgPart(f32 x, f32 y, f32 w, f32 h, u16 texW, u16 texH, u8 data[], f32 u0, f32 v0, f32 u1, f32 v1,
	u8 alpha);
void ResetVideo_Menu();
void Menu_Render();
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, u8 data[], f32 degrees, f32 scaleX, f32 scaleY, u8 alphaF );
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled);

extern int screenheight;
extern int screenwidth;
extern u32 FrameTimer;

#endif
