/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * video.cpp
 * Video routines
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: no text console on the menu's frame buffer, StopGXKeepPicture, the frame buffer accessors the launch screen and screenshots use, the display scale (widescreen and screen size), and a camera
 * that moves and scales everything drawn (the menu's transitions).
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "input.h"
#include "libwiigui/gui.h"

#define DEFAULT_FIFO_SIZE 256 * 1024
static u32 *xfb[2] = { nullptr, nullptr }; // Double buffered
static int whichfb = 0; // Switch
static GXRModeObj *vmode; // Menu video mode
static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN (32);
static Mtx GXmodelView2D;
int screenheight;
int screenwidth;
u32 FrameTimer = 0;

/****************************************************************************
 * ResetVideo_Menu
 *
 * Reset the video/rendering mode for the menu
****************************************************************************/
void
ResetVideo_Menu()
{
	Mtx44 p;
	f32 yscale;
	u32 xfbHeight;

	VIDEO_Configure (vmode);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	else
		while (VIDEO_GetNextField())
			VIDEO_WaitVSync();

	// clears the bg to color and clears the z buffer
	GXColor background = {0, 0, 0, 255};
	GX_SetCopyClear (background, 0x00ffffff);

	yscale = GX_GetYScaleFactor(vmode->efbHeight,vmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetDispCopySrc(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth,xfbHeight);
	GX_SetCopyFilter(vmode->aa,vmode->sample_pattern,GX_TRUE,vmode->vfilter);
	GX_SetFieldMode(vmode->field_rendering,((vmode->viHeight==2*vmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (vmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	// setup the vertex descriptor
	// tells the flipper to expect direct data
	GX_ClearVtxDesc();
	GX_InvVtxCache ();
	GX_InvalidateTexAll();

	GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc (GX_VA_CLR0, GX_DIRECT);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetZMode (GX_FALSE, GX_LEQUAL, GX_TRUE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	guMtxIdentity(GXmodelView2D);
	guMtxTransApply (GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -50.0F);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);

	Menu_LoadOrtho();

	GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
}

/****************************************************************************
 * InitVideo
 *
 * This function MUST be called at startup.
 * - also sets up menu video mode
 ***************************************************************************/

void
InitVideo ()
{
	VIDEO_Init();
	vmode = VIDEO_GetPreferredMode(nullptr); // get default video mode

	// widescreen fix
	if(CONF_GetAspectRatio() == CONF_ASPECT_16_9)
		vmode->viWidth = VI_MAX_WIDTH_PAL;

	VIDEO_Configure (vmode);

	screenheight = 480;
	screenwidth = vmode->fbWidth;

	// Allocate the video buffers
	xfb[0] = (u32 *) SYS_AllocateFramebuffer (vmode);
	xfb[1] = (u32 *) SYS_AllocateFramebuffer (vmode);
	DCInvalidateRange(xfb[0], VIDEO_GetFrameBufferSize(vmode));
	DCInvalidateRange(xfb[1], VIDEO_GetFrameBufferSize(vmode));
	xfb[0] = (u32 *) MEM_K0_TO_K1 (xfb[0]);
	xfb[1] = (u32 *) MEM_K0_TO_K1 (xfb[1]);

	// A console is always useful while debugging
	// Riftwii: no text console on the GUI's framebuffer; stray printf
	// output drew over the menus. The launch phase sets up its own.

	// Clear framebuffers etc.
	VIDEO_ClearFrameBuffer (vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer (vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer (xfb[0]);

	VIDEO_SetBlack (FALSE);
	VIDEO_Flush ();
	VIDEO_WaitVSync ();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync ();

	// Initialize GX
	GXColor background = { 0, 0, 0, 0xff };
	memset (&gp_fifo, 0, DEFAULT_FIFO_SIZE);
	GX_Init (&gp_fifo, DEFAULT_FIFO_SIZE);
	GX_SetCopyClear (background, 0x00ffffff);
	GX_SetDispCopyGamma (GX_GM_1_0);
	GX_SetCullMode (GX_CULL_NONE);
	
	ResetVideo_Menu();
	// Finally, the video is up and ready for use :)
}

/****************************************************************************
 * StopGX
 *
 * Stops GX (when exiting)
 ***************************************************************************/
void StopGX()
{
	GX_AbortFrame();
	GX_Flush();

	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
}

/****************************************************************************
 * StopGXKeepPicture (Riftwii)
 *
 * Stops GX like StopGX but leaves the last frame on screen: the launch
 * screen's log then prints into it (see wii/rift_menu.cpp).
 ***************************************************************************/
void StopGXKeepPicture()
{
	GX_AbortFrame();
	GX_Flush();
}

// The frame on screen, its width and height (Riftwii: screenshots and the
// launch screen's console).
void * Menu_CurrentXfb()
{
	return xfb[whichfb];
}
int Menu_XfbWidth()
{
	return vmode ? vmode->fbWidth : 640;
}
int Menu_XfbHeight()
{
	return vmode ? vmode->xfbHeight : 480;
}
int Menu_EfbHeight()
{
	return vmode ? vmode->efbHeight : 480;
}

/****************************************************************************
 * The display scale (RiftWii). The menu is laid out in 640x480 units;
 * these draw it smaller than the screen about the centre: across by
 * `sx` (3/4 on a 16:9 TV, so a 4:3 menu keeps its shape) and both ways by
 * the screen-size setting, inside what a TV's overscan crops. Everything
 * drawn goes through the projection, so only the projection, the scissor
 * boxes and the Wii Remote's pointer need to know.
 ***************************************************************************/
static f32 displayX = 1.0f, displayY = 1.0f;
static volatile bool displayChanged = false;  // the projection to load again, on the GUI thread

/****************************************************************************
 * The camera (RiftWii): every point drawn, in menu units, goes to
 * k * p + (tx, ty) before the display scale. A transition zooms or slides
 * the whole screen with it; a popup scales its own window. Pushed and
 * popped on the GUI thread while drawing; each change loads the 2D
 * projection again (a 3D view, the shelf, takes it in Menu_ScaleProjection).
 ***************************************************************************/
struct MenuCamera { f32 k, tx, ty; };
static MenuCamera cameraStack[8] = {{1.0f, 0.0f, 0.0f}};
static int cameraDepth = 0;

static void ApplyCamera(Mtx44 p)
{
	const MenuCamera& c = cameraStack[cameraDepth];
	if (c.k == 1.0f && c.tx == 0.0f && c.ty == 0.0f) return;
	// In the projection's terms (x across -1 to 1 from 0 to 640, y down
	// from 1 to -1 from 0 to 480): x' = k x + bx, y' = k y + by, as rows.
	const f32 bx = (320.0f * (c.k - 1.0f) + c.tx) / 320.0f;
	const f32 by = -(240.0f * (c.k - 1.0f) + c.ty) / 240.0f;
	for (int col = 0; col < 4; ++col)
	{
		p[0][col] = c.k * p[0][col] + bx * p[3][col];
		p[1][col] = c.k * p[1][col] + by * p[3][col];
	}
}

void Menu_ScaleProjection(Mtx44 p)
{
	ApplyCamera(p);
	for (int c = 0; c < 4; ++c)
	{
		p[0][c] *= displayX;
		p[1][c] *= displayY;
	}
}

// Pushes past the stack's top keep the top camera; their pops are counted
// off first, so every pop matches its push.
static int cameraOver = 0;

void Menu_SetCamera(f32 k, f32 tx, f32 ty)
{
	cameraDepth = 0;
	cameraOver = 0;
	cameraStack[0] = MenuCamera{k, tx, ty};
	Menu_LoadOrtho();
}

void Menu_PushCamera(f32 k, f32 cx, f32 cy, f32 dx, f32 dy)
{
	// Scaled by k about (cx, cy) and moved by (dx, dy), inside whatever
	// camera is on.
	const MenuCamera& o = cameraStack[cameraDepth];
	const f32 tx = cx * (1.0f - k) + dx, ty = cy * (1.0f - k) + dy;
	if (cameraDepth >= 7) {
		++cameraOver;
		return;
	}
	++cameraDepth;
	cameraStack[cameraDepth] = MenuCamera{o.k * k, o.k * tx + o.tx, o.k * ty + o.ty};
	Menu_LoadOrtho();
}

void Menu_PushNoCamera()
{
	if (cameraDepth >= 7) {
		++cameraOver;
		return;
	}
	++cameraDepth;
	cameraStack[cameraDepth] = MenuCamera{1.0f, 0.0f, 0.0f};
	Menu_LoadOrtho();
}

void Menu_PopCamera()
{
	if (cameraOver > 0) {
		--cameraOver;
		return;
	}
	if (cameraDepth > 0) --cameraDepth;
	Menu_LoadOrtho();
}

// A point in menu units, through the camera.
static void CameraPoint(f32& x, f32& y)
{
	const MenuCamera& c = cameraStack[cameraDepth];
	x = c.k * x + c.tx;
	y = c.k * y + c.ty;
}

void Menu_LoadOrtho()
{
	Mtx44 p;
	guOrtho(p,0,479,0,639,0,300);
	Menu_ScaleProjection(p);
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);
}

// Any thread: the next frame (Menu_Render, on the GUI thread) loads it.
void Menu_SetDisplayScale(f32 sx, f32 sy)
{
	displayX = sx;
	displayY = sy;
	displayChanged = true;
}

// A point in menu units, as a pixel of the frame buffer (RiftWii: the
// launch screen's console and bar, which print into the frame itself).
void Menu_MenuToXfb(f32 x, f32 y, int* px, int* py)
{
	*px = (int)((320.0f + (x - 320.0f) * displayX) * Menu_XfbWidth() / 640.0f + 0.5f);
	*py = (int)((240.0f + (y - 240.0f) * displayY) * Menu_XfbHeight() / 480.0f + 0.5f);
}

f32 Menu_ScreenToMenuX(f32 x) { return 320.0f + (x - 320.0f) / displayX; }
f32 Menu_ScreenToMenuY(f32 y) { return 240.0f + (y - 240.0f) / displayY; }

void Menu_VisibleArea(f32* x, f32* y, f32* w, f32* h)
{
	*x = 320.0f - 320.0f / displayX;
	*y = 240.0f - 240.0f / displayY;
	*w = 640.0f / displayX;
	*h = 480.0f / displayY;
}

void Menu_FillScreen(f32 y, f32 height, GXColor color)
{
	f32 vx, vy, vw, vh;
	Menu_VisibleArea(&vx, &vy, &vw, &vh);
	// Cut to the screen: callers pass "from here down" as a band running
	// far past it, and a polygon that far outside the picture is not
	// always rasterised right on a console (Dolphin draws it fine).
	f32 top = y, bottom = y + height;
	if (top < vy) top = vy;
	if (bottom > vy + vh) bottom = vy + vh;
	if (bottom <= top) return;
	Menu_DrawRectangle(vx, top, vw, bottom - top, color, 1);
}

void Menu_FillWholeScreen(GXColor color)
{
	// The whole screen whatever the camera (a popup's dimming while it
	// pops, a fade).
	Menu_PushNoCamera();
	f32 vx, vy, vw, vh;
	Menu_VisibleArea(&vx, &vy, &vw, &vh);
	Menu_DrawRectangle(vx, vy, vw, vh, color, 1);
	Menu_PopCamera();
}

void Menu_Scissor(f32 x, f32 y, f32 w, f32 h)
{
	// Where the camera puts the box.
	const f32 k = cameraStack[cameraDepth].k;
	CameraPoint(x, y);
	w *= k;
	h *= k;
	const f32 ex = Menu_XfbWidth() / 640.0f, ey = Menu_EfbHeight() / 480.0f;
	f32 x0 = (320.0f + (x - 320.0f) * displayX) * ex, x1 = (320.0f + (x + w - 320.0f) * displayX) * ex;
	f32 y0 = (240.0f + (y - 240.0f) * displayY) * ey, y1 = (240.0f + (y + h - 240.0f) * displayY) * ey;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > Menu_XfbWidth()) x1 = Menu_XfbWidth();
	if (y1 > Menu_EfbHeight()) y1 = Menu_EfbHeight();
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	GX_SetScissor((u32)x0, (u32)y0, (u32)(x1 - x0 + 0.5f), (u32)(y1 - y0 + 0.5f));
}

// The menu's 480 rows high and, across, as wide as that is on the TV: 640
// on a 4:3 menu, about 853 on a widescreen one. Never past what the
// screen size keeps on screen.
void Menu_SafeArea(f32* x, f32* w)
{
	*w = 640.0f * displayY / displayX;
	*x = 320.0f - *w / 2;
}

void Menu_DrawImgPart(f32 x, f32 y, f32 w, f32 h, u16 texW, u16 texH, u8 data[], f32 u0, f32 v0, f32 u1, f32 v1,
	u8 alpha)
{
	if (data == nullptr || w <= 0 || h <= 0)
		return;
	GXTexObj texObj;
	GX_InitTexObj(&texObj, data, texW, texH, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);
	GX_InvalidateTexAll();
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(x, y, 0);
	GX_Color4u8(0xFF, 0xFF, 0xFF, alpha);
	GX_TexCoord2f32(u0, v0);
	GX_Position3f32(x + w, y, 0);
	GX_Color4u8(0xFF, 0xFF, 0xFF, alpha);
	GX_TexCoord2f32(u1, v0);
	GX_Position3f32(x + w, y + h, 0);
	GX_Color4u8(0xFF, 0xFF, 0xFF, alpha);
	GX_TexCoord2f32(u1, v1);
	GX_Position3f32(x, y + h, 0);
	GX_Color4u8(0xFF, 0xFF, 0xFF, alpha);
	GX_TexCoord2f32(u0, v1);
	GX_End();
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

/****************************************************************************
 * Menu_Render
 *
 * Renders everything current sent to GX, and flushes video
 ***************************************************************************/
void Menu_Render()
{
	whichfb ^= 1; // flip framebuffer
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	if (displayChanged)
	{
		displayChanged = false;
		Menu_LoadOrtho();
	}
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(xfb[whichfb],GX_TRUE);
	GX_DrawDone();
	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	FrameTimer++;
}

/****************************************************************************
 * Menu_DrawImg
 *
 * Draws the specified image on screen using GX
 ***************************************************************************/
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, u8 data[],
	f32 degrees, f32 scaleX, f32 scaleY, u8 alpha)
{
	if(data == nullptr)
		return;

	GXTexObj texObj;

	GX_InitTexObj(&texObj, data, width,height, GX_TF_RGBA8,GX_CLAMP, GX_CLAMP,GX_FALSE);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);
	GX_InvalidateTexAll();

	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
	GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

	Mtx m,m1,m2, mv;
	width  >>= 1;
	height >>= 1;

	guMtxIdentity (m1);
	guMtxScaleApply(m1,m1,scaleX,scaleY,1.0);
	guVector axis = (guVector) {0 , 0, 1 };
	guMtxRotAxisDeg (m2, &axis, degrees);
	guMtxConcat(m2,m1,m);

	guMtxTransApply(m,m, xpos+width,ypos+height,0);
	guMtxConcat (GXmodelView2D, m, mv);
	GX_LoadPosMtxImm (mv, GX_PNMTX0);

	GX_Begin(GX_QUADS, GX_VTXFMT0,4);
	GX_Position3f32(-width, -height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(0, 0);

	GX_Position3f32(width, -height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(1, 0);

	GX_Position3f32(width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(1, 1);

	GX_Position3f32(-width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
	GX_TexCoord2f32(0, 1);
	GX_End();
	GX_LoadPosMtxImm (GXmodelView2D, GX_PNMTX0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetVtxDesc (GX_VA_TEX0, GX_NONE);
}

/****************************************************************************
 * Menu_DrawRectangle
 *
 * Draws a rectangle at the specified coordinates using GX
 ***************************************************************************/
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled)
{
	long n = 4;
	f32 x2 = x+width;
	f32 y2 = y+height;
	guVector v[] = {{x,y,0.0f}, {x2,y,0.0f}, {x2,y2,0.0f}, {x,y2,0.0f}, {x,y,0.0f}};
	u8 fmt = GX_TRIANGLEFAN;

	if(!filled)
	{
		fmt = GX_LINESTRIP;
		n = 5;
	}

	GX_Begin(fmt, GX_VTXFMT0, n);
	for(long i=0; i<n; ++i)
	{
		GX_Position3f32(v[i].x, v[i].y,  v[i].z);
		GX_Color4u8(color.r, color.g, color.b, color.a);
	}
	GX_End();
}
