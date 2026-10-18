/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The render mode a GameCube game's VIConfigure(rm) is given, as the
 * picture settings change it (gc_on_vi_configure): kept apart from the
 * blob so the host tests can check the rules. A copy changes, never the
 * game's own mode, which it keeps using for its copies to the frame
 * buffer.
 *
 * GXRenderModeObj as VIConfigure reads it (big endian): viTVMode is the
 * word at 0, the TV format << 2 | 0 interlaced, 1 double-strike (240
 * lines), 2 progressive; fbWidth the half word at 4; viXOrigin at 10 and
 * viWidth at 14, where the picture sits in the 720 pixels of the TV line;
 * xfbMode the word at 0x14, 0 single field, 1 double field (the frame
 * buffer holds whole frames); field_rendering the byte at 0x18. */
#ifndef RIFTWII_GC_VIDEO_H
#define RIFTWII_GC_VIDEO_H

#include <stdint.h>

#define GC_RM_BYTES 60u
#define GC_RM_TV_MODE 0u
#define GC_RM_FB_WIDTH 4u
#define GC_RM_VI_X 10u
#define GC_RM_VI_WIDTH 14u
#define GC_RM_XFB_MODE 0x14u
#define GC_RM_FIELD_RENDERING 0x18u
#define GC_VI_NTSC_INTERLACED 0u
#define GC_VI_NTSC_PROGRESSIVE 2u
#define GC_VI_XFB_SINGLE 0u
#define GC_VI_XFB_DOUBLE 1u
/* The TV line the VI can fill. */
#define GC_VI_LINE 720u
/* Picture width (gc_rt_context.vi_width): 0 the game's own, this the
 * frame buffer's width (no side scaling), else the width in pixels. */
#define GC_VI_WIDTH_FRAMEBUFFER 1u

static inline uint32_t gc_video_get32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint32_t gc_video_get16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
static inline void gc_video_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static inline void gc_video_put16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Copies `mode` (GC_RM_BYTES) into `copy` and changes the copy:
 *  - `progressive`: an NTSC interlaced mode whose frames are whole in the
 *    frame buffer becomes its progressive twin, which shows the same
 *    buffer a whole frame each retrace. Modes drawn a field at a time and
 *    double-strike (240-line) ones stay as they are: their buffer holds
 *    half a picture. Only NTSC modes: a European game, even at 60 Hz
 *    in EURGB60, is never asked for 480p (gc_progressive_on).
 *  - `vi_width` (GC_VI_WIDTH_FRAMEBUFFER or pixels): the picture is drawn
 *    that wide in the TV line, centred. The VI only widens the frame
 *    buffer, so a width under the buffer's is left alone.
 * Returns 1 when the copy differs from the game's mode (use the copy),
 * 0 when it is the same (use the game's own). */
static inline uint32_t gc_video_pick(const uint8_t* mode, uint32_t progressive, uint32_t vi_width, uint8_t* copy) {
    uint32_t i;
    uint32_t changed = 0;
    for (i = 0; i < GC_RM_BYTES; ++i) copy[i] = mode[i];
    if (progressive && gc_video_get32(mode + GC_RM_TV_MODE) == GC_VI_NTSC_INTERLACED &&
        gc_video_get32(mode + GC_RM_XFB_MODE) == GC_VI_XFB_DOUBLE && mode[GC_RM_FIELD_RENDERING] == 0) {
        gc_video_put32(copy + GC_RM_TV_MODE, GC_VI_NTSC_PROGRESSIVE);
        gc_video_put32(copy + GC_RM_XFB_MODE, GC_VI_XFB_SINGLE);
        changed = 1;
    }
    if (vi_width != 0) {
        const uint32_t buffer_width = gc_video_get16(mode + GC_RM_FB_WIDTH);
        const uint32_t width = vi_width == GC_VI_WIDTH_FRAMEBUFFER ? buffer_width : vi_width;
        const uint32_t fits = width >= buffer_width && width <= GC_VI_LINE;
        if (fits && width != gc_video_get16(mode + GC_RM_VI_WIDTH)) {
            gc_video_put16(copy + GC_RM_VI_WIDTH, width);
            gc_video_put16(copy + GC_RM_VI_X, (GC_VI_LINE - width) / 2);
            changed = 1;
        }
    }
    return changed;
}

/* 1 when a picture width was asked for but the mode's frame buffer is
 * wider than it (the VI only widens): the setting does nothing in that
 * mode, which the trace says (gc_rt_context.vi_mode's GC_VI_WIDTH_KEPT). */
#define GC_VI_WIDTH_KEPT 0x10000u
static inline uint32_t gc_video_width_kept(const uint8_t* mode, uint32_t vi_width) {
    const uint32_t buffer_width = gc_video_get16(mode + GC_RM_FB_WIDTH);
    return vi_width > GC_VI_WIDTH_FRAMEBUFFER && vi_width < buffer_width;
}

#endif
