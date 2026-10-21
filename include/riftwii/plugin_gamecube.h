/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_PLUGIN_GAMECUBE_H
#define RIFTWII_PLUGIN_GAMECUBE_H

/* What RiftWii hands the gamecube plugin's launch (docs/PLUGINS.md,
 * "Stage (b)"): one GameCube game to start, with everything the launch
 * used to read from the menu's C++ (Settings, the launch's extras, the
 * crash screen, the way back) already looked up, as plain C. The DOL
 * fills it (wii/launch/gcplugin.cpp), the plugin reads it
 * (plugins/gamecube/gcentry.cpp); riftwii/gcrequest.hpp packs and unpacks
 * the packs' plan. Every pointer stays valid until launch returns. */

#include <stdint.h>

#include "riftwii/plugin_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One piece of the packs' window (riftwii/gcoverlay.hpp's
 * GcOverlayPlan::Entry): kind is RWP_GC_PIECE_*. */
typedef struct rwp_gc_pack_entry {
    uint64_t vstart;
    uint64_t length;
    uint64_t source; /* image: a byte offset in the image; device: a 512-byte sector */
    uint32_t kind;
    uint32_t skip;   /* device: bytes into sector `source` */
} rwp_gc_pack_entry;

#define RWP_GC_PIECE_IMAGE 0u
#define RWP_GC_PIECE_DEVICE 1u
#define RWP_GC_PIECE_ZERO 2u
#define RWP_GC_PIECE_USB 3u    /* `source` is a 512-byte sector of the USB drive */

/* The way back (wii/launch/gcplugin.hpp's GcWayBack): kind. */
#define RWP_GC_WAY_BACK_NONE 0u
#define RWP_GC_WAY_BACK_CHANNEL 1u
#define RWP_GC_WAY_BACK_WII_MENU 2u
#define RWP_GC_WAY_BACK_HBC 3u

typedef struct rwp_gc_launch {
    uint32_t size; /* sizeof(rwp_gc_launch) as the DOL built it */

    /* The game (wii/launch/gclaunch.hpp's GcImage). */
    const char* path;      /* sd:/games/.../game.iso, or .rvz */
    const char* id;        /* its ID6 */
    uint32_t is_rvz;       /* the image is an RVZ (by its name, as the list says) */
    uint32_t shared_card;  /* the shared memory card, not the game's own */
    uint32_t drive_timing; /* reads take a GameCube drive's time */
    uint32_t progressive;  /* 480p */
    uint32_t widescreen;   /* 3D drawn for 16:9 */
    uint32_t sharp;        /* the sharper picture */
    uint32_t sixty_hz;     /* a European game told the TV takes 60 Hz */
    int32_t language;      /* the SRAM language the game is told; -1 the console's */
    uint32_t vi_width;     /* the picture width (riftwii/gcvideo.hpp); 0 the game's own */
    /* An image on the USB drive (path "usb:/..."): d2x's /dev/usb2, open
     * under the d2x cIOS the launch reloaded into (-1: the image is on
     * the SD card), and the image file's pieces in the drive's 512-byte
     * sectors, in the order of its bytes, with its size. */
    int32_t usb_fd;
    const rwp_fragment* usb_fragments;
    uint32_t usb_fragment_count;
    uint64_t usb_file_bytes;

    /* The packs' plan (riftwii/gcoverlay.hpp's GcOverlayPlan); pack_fst
     * NULL when the game has no packs. */
    const uint8_t* pack_fst;
    uint32_t pack_fst_bytes;
    const uint8_t* pack_header; /* disc header bytes 0x420-0x440 */
    uint32_t pack_header_bytes;
    uint64_t pack_fst_offset;
    const rwp_gc_pack_entry* pack_entries;
    uint32_t pack_entry_count;

    /* What the launch reads from the settings and the launch's extras
     * (wii/launch/boot.hpp's LaunchExtras), as they are now. */
    uint32_t rumble_off;       /* Wii Remote rumble off */
    uint32_t in_dolphin;       /* running in Dolphin, not on a Wii */
    const uint8_t* cheat_gct;  /* the code list; NULL: no cheats */
    uint32_t cheat_gct_bytes;
    uint32_t cheat_count;
    uint32_t code_build_codes; /* a code build's own codes were picked (not for GameCube games) */
    const uint8_t* code_handler; /* the Gecko code handler (codehandleronly.bin) */
    uint32_t code_handler_bytes;

    /* The crash screen the menu made (gc_crash.h's layout); NULL: none. */
    const uint8_t* crash_screen;
    uint32_t crash_screen_bytes;
    uint32_t crash_screen_way_back; /* its hint promises HOME or A goes back */

    /* The GameCube adapter for Wii U (runtime/gc/gc_gcad.h): the
     * /dev/usb/hid handle the menu opened for the game (-1: no adapter),
     * its HID version (4 or 5), the adapter's v5 device id from the USB
     * list (-1: none), and whether the game's rumble reaches it. */
    int32_t adapter_fd;
    uint32_t adapter_version;
    int32_t adapter_dev;
    uint32_t adapter_rumble;

    /* What holding HOME goes to: the title and its first ticket view
     * (libogc's tikview, 32-byte aligned), or kind NONE. */
    uint64_t way_back_title;
    uint32_t way_back_kind;
    const void* way_back_view;
    uint32_t way_back_view_bytes;

    /* A switch to another program on the disc, carried out by starting
     * the image again (runtime/gc's gc_on_boot_dol, exec.txt): exec set,
     * the program's offset on the disc (0: the disc's own), the restart
     * code the game passed, and its arguments, exec_arg_count strings one
     * after another, each ending in a NUL. */
    uint32_t exec;
    uint32_t exec_dol;
    uint32_t exec_restart;
    const char* exec_args;
    uint32_t exec_args_bytes;
    uint32_t exec_arg_count;

    /* Out: the reason when launch returns RWP_LAUNCH_FAILED. */
    char* error;
    uint32_t error_bytes;
} rwp_gc_launch;

#ifdef __cplusplus
}
#endif

#endif /* RIFTWII_PLUGIN_GAMECUBE_H */
