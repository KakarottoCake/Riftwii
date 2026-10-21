/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_PLUGIN_API_H
#define RIFTWII_PLUGIN_API_H

/* The plugin interface (docs/PLUGINS.md): the two tables a plugin and
 * RiftWii hand each other, and the numbers the build and the loader share.
 * Plain C, so a plugin's C sources, the loader's C++ and tools/make_plugin.py
 * (which reads RWP_API_VERSION and RWP_ARENA_BYTES from this file) all
 * agree on one copy.
 *
 * The API is private to RiftWii and changes whenever RiftWii needs it to.
 * Any change to the layout or the meaning of a function bumps
 * RWP_API_VERSION. A function added at the end of a table is the one change
 * that needs no bump, because each table starts with its size: a plugin
 * checks RWP_HOST_HAS before it calls a function that came later. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The version of both tables below. tools/make_plugin.py writes it into
 * every plugin's header; the loader refuses a plugin built for another. */
#define RWP_API_VERSION 2

/* The plugin arena in MEM2, taken from the top of the loader's MEM2 heap
 * the first time a plugin loads (wii/core/plugins.cpp). Every plugin's
 * image, .bss and state must fit in it; the build refuses one that does
 * not. */
#define RWP_ARENA_BYTES (256u * 1024u)

/* A path's facts (path_info). */
typedef struct rwp_path_info {
    uint64_t size;         /* bytes; 0 for a folder */
    uint32_t is_directory; /* 1 a folder, 0 a file */
} rwp_path_info;

/* One piece of a file on the SD card, in 512-byte sectors of the device
 * (sd_file_fragments), in the order of the file's bytes. */
typedef struct rwp_fragment {
    uint64_t sector;
    uint64_t sector_count;
} rwp_fragment;

/* The SD card as a game keeps using it after the hand-over
 * (sd_hand_over): IOS's handle to the slot, the card's address and
 * whether CMD18 takes sectors (SDHC) or bytes. */
typedef struct rwp_sd_card {
    int32_t fd;
    uint32_t rca;
    uint32_t sdhc;
} rwp_sd_card;

/* What a plugin reads of the console's SRAM (read_sram): the RTC's bias,
 * the language and the flash ID of memory card slot A. */
typedef struct rwp_sram {
    uint32_t counter_bias;
    uint32_t language;
    uint8_t flash_id_a[12];
} rwp_sram;

/* shut_down_for_game's TV modes: the video encoder set for the game's
 * region (libogc's TVNtsc480IntDf and TVPal528IntDf). */
#define RWP_TV_NTSC 0u
#define RWP_TV_PAL 1u
/* Test switches only (the GameCube plugin's sd:/riftwii/gc/test/handover480p
 * and avekeep), for colours a Wii shows swapped in 480p: NTSC 480p
 * (TVNtsc480Prog), so the encoder is set while the VI is already
 * progressive; and the encoder left as the menu's own mode set it. */
#define RWP_TV_NTSC_480P 2u
#define RWP_TV_KEEP 3u

/* What RiftWii gives a plugin's init. Every pointer is set: a function
 * the loader cannot offer is still there and reports a failure. */
typedef struct rwp_host {
    uint32_t size;        /* sizeof(rwp_host) as the loader built it */
    uint32_t api_version; /* RWP_API_VERSION of the loader */

    /* Log: one line in session.log (or boot.log), "Plugin <name>: " put in
     * front. printf's %s, %u, %d, %x and %08x; no floating point. */
    void (*log)(const char* format, ...);

    /* Memory. arena_alloc: from the plugin arena in MEM2, 32-byte aligned,
     * released when the plugin is unloaded (there is no free). mem1_alloc
     * and mem1_free: the loader's MEM1 heap, for short-lived buffers that
     * must be in MEM1. Both return NULL when there is no room. */
    void* (*arena_alloc)(uint32_t bytes);
    void* (*mem1_alloc)(uint32_t bytes);
    void (*mem1_free)(void* block);
    /* Cache maintenance. flush_range: data the plugin wrote, out to memory
     * (before a DMA reads it). invalidate_range: the data cache dropped
     * (before reading what a DMA wrote). sync_code: code the plugin wrote,
     * flushed and the instruction cache invalidated, so it can run. */
    void (*flush_range)(void* start, uint32_t bytes);
    void (*invalidate_range)(void* start, uint32_t bytes);
    void (*sync_code)(void* start, uint32_t bytes);

    /* Files, through the loader's own mounts ("sd:/...", "usb:/..."). Bytes
     * read (fewer at the file's end), or -1 when the file cannot be read. */
    int32_t (*file_read)(const char* path, uint32_t offset, void* buffer, uint32_t bytes);
    /* The file's size, or -1 when it is not there. */
    int32_t (*file_size)(const char* path);

    /* RiftWii's version ("2610-460") and the build stamp both the DOL and
     * this plugin carry ("2610-460-g1a2b3c4d"). */
    const char* (*version)(void);
    const char* (*build_stamp)(void);

    /* Crash reports name an address inside [start, start + bytes) as
     * "<what> +0x..", for code or data the plugin placed outside its own
     * image (its image is named already). Up to 4 ranges per plugin;
     * 0 when the table is full. */
    int32_t (*crash_register)(const char* what, uint32_t start, uint32_t bytes);

    /* ---- API 2: what a launcher needs (stage (b), the GameCube loader).
     * Each is a plain C function over RiftWii's own code; a C++ plugin's
     * runtime (plugins/common/rwpruntime.cpp) puts the C library's names
     * on them. ---- */

    /* The C library: vsnprintf (no floating point, as log), memmove and
     * memset. */
    int32_t (*format)(char* out, uint32_t bytes, const char* format, va_list args);
    void* (*move_bytes)(void* to, const void* from, uint32_t bytes);
    void* (*fill_bytes)(void* to, int32_t value, uint32_t bytes);

    /* Log: `text` as it is, with its own line ends and no "Plugin <name>: "
     * in front, for lines that already say what they are about (the
     * GameCube loader's "GameCube: ..."). log_close ends the log file, as
     * a launch does before the SD card is handed over. */
    void (*log_text)(const char* text);
    void (*log_close)(void);

    /* Files by handle, through the loader's mounts: a C library FILE
     * behind an opaque pointer. stream_open's mode is fopen's ("rb",
     * "wb", "r+b"); NULL when it does not open. stream_read and
     * stream_write return the bytes moved; stream_seek (whence: 0 the
     * start, 1 here, 2 the end) and stream_close 0 or -1; stream_tell the
     * place, or -1. */
    void* (*stream_open)(const char* path, const char* mode);
    int32_t (*stream_close)(void* stream);
    uint32_t (*stream_read)(void* stream, void* buffer, uint32_t bytes);
    uint32_t (*stream_write)(void* stream, const void* buffer, uint32_t bytes);
    int32_t (*stream_seek)(void* stream, int64_t offset, int32_t whence);
    int64_t (*stream_tell)(void* stream);
    /* 0 and `out` filled when `path` is there, -1 when it is not. */
    int32_t (*path_info)(const char* path, rwp_path_info* out);
    /* One folder (its parent must exist): 0 made or already there, -1 not. */
    int32_t (*make_directory)(const char* path);

    /* The SD card read raw, for code that reads it after the hand-over.
     * sd_file_fragments: where the file at "sd:/..." lies, up to `max`
     * pieces into `out`; `*count` is how many it has (more than `max`:
     * call again with room for all), `*file_bytes` its size. 1 found,
     * 0 not, with the reason in `error`. sd_forget_layout: drops what the
     * raw lookups remember of the card's folders, after a file was
     * written through the mount. sd_hand_over: the mount let go, the SD
     * interface shut down and the card opened and selected for the game
     * (`out`); 1, or 0 with the reason (the log is closed by then). */
    int32_t (*sd_file_fragments)(const char* path, rwp_fragment* out, uint32_t max, uint32_t* count,
                                 uint64_t* file_bytes, char* error, uint32_t error_bytes);
    void (*sd_forget_layout)(void);
    int32_t (*sd_hand_over)(rwp_sd_card* out, char* error, uint32_t error_bytes);

    /* System. ios_open: IOS_Open, an fd or IOS's negative error.
     * read_sram: the console's SRAM facts. video_framebuffer: the frame on
     * screen now. shut_down_for_game: the video encoder set to `tv_mode`
     * (RWP_TV_*) on the frame on screen, two retraces for libogc to apply
     * it, then libogc and IOS's handles shut down for a game
     * (SYS_ResetSystem(SYS_SHUTDOWN)): only code and memory are left. */
    int32_t (*ios_open)(const char* path, uint32_t mode);
    void (*read_sram)(rwp_sram* out);
    uint32_t (*video_framebuffer)(void);
    void (*shut_down_for_game)(uint32_t tv_mode);

    /* Zstandard's one-shot decoder (ZSTD_decompress,
     * ZSTD_getFrameContentSize, ZSTD_isError, ZSTD_getErrorName), the
     * loader's own copy. */
    size_t (*zstd_decompress)(void* to, size_t to_bytes, const void* from, size_t from_bytes);
    unsigned long long (*zstd_frame_size)(const void* from, size_t from_bytes);
    uint32_t (*zstd_is_error)(size_t code);
    const char* (*zstd_error_name)(size_t code);

    /* Added after API 2 (check RWP_HOST_HAS). usb_read: `count` 512-byte
     * sectors of the USB drive from `sector` into `out` (any buffer),
     * through d2x's /dev/usb2 once a GameCube game's launch has opened it
     * (rwp_gc_launch's usb_fd); 1 read, 0 not. */
    int32_t (*usb_read)(uint64_t sector, uint32_t count, void* out);

    /* Added after usb_read (check RWP_HOST_HAS). Changes only slot A's
     * 12-byte flash ID and its checksum in SRAM-ex, when either differs.
     * 1 written to RTC SRAM, 0 already matched, -1 unable to commit (the
     * caller logs it and carries on; the game may report the card damaged).
     * Call before shut_down_for_game, while libogc's EXI still works. */
    int32_t (*set_sram_flash_id_a)(const uint8_t flash_id[12]);
} rwp_host;

/* What a plugin gives RiftWii: a constant table in its image, found by
 * the header's table offset (its pointers are relocated with the rest). */
typedef struct rwp_plugin {
    uint32_t size;        /* sizeof(rwp_plugin) as the plugin was built */
    uint32_t api_version; /* RWP_API_VERSION the plugin was built against */
    uint32_t version;     /* the plugin's own version, for logs */
    uint32_t flags;       /* RWP_FLAG_* */
    /* Whether the plugin has games on a mounted drive ("sd:/", "usb:/"):
     * a directory test, not a scan. 1 yes, 0 no; NULL when the plugin has
     * no games of its own. */
    int32_t (*probe)(const char* drive_root);
    /* Releases what init took. Called once, before the loader forgets the
     * plugin (a launch, RiftWii closing). May be NULL. */
    void (*shutdown)(void);
    /* API 2. Starts what the plugin starts, with `request` its own
     * structure (the gamecube plugin's: rwp_gc_launch,
     * riftwii/plugin_gamecube.h), which begins with its size. Returns only
     * on failure, an RWP_LAUNCH_* result, with the reason in the request.
     * NULL when the plugin starts nothing. */
    int32_t (*launch)(const void* request, uint32_t request_bytes);
} rwp_plugin;

/* launch's failures. */
#define RWP_LAUNCH_FAILED 1     /* the reason is in the request's error */
#define RWP_LAUNCH_NO_MEMORY 2  /* the loader's heap had no room */

/* rwp_plugin.flags and the header's flags. */
#define RWP_FLAG_UNLOADABLE 1u /* may be dropped under memory pressure */

/* The entry point every plugin has, named rwp_init (tools/make_plugin.py
 * looks the symbol up): 0 when the plugin is ready, otherwise a reason
 * the plugin has already logged. */
typedef int32_t (*rwp_init_fn)(const rwp_host* host);

/* Whether `host` is long enough to hold `field`: for a function added to
 * the end of the table after the plugin's API version. */
#define RWP_HOST_HAS(host, field) \
    ((host)->size >= offsetof(rwp_host, field) + sizeof(((const rwp_host*)0)->field))

#ifdef __cplusplus
}
#endif

#endif /* RIFTWII_PLUGIN_API_H */
