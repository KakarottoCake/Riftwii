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
#define RWP_ARENA_BYTES (384u * 1024u)

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
 * signal (libogc's TVNtsc480IntDf and TVPal528IntDf, and RWP_TV_EURGB60
 * below). */
#define RWP_TV_NTSC 0u
#define RWP_TV_PAL 1u
/* Test switches only (the GameCube plugin's sd:/riftwii/gc/test/handover480p
 * and avekeep), for colours a Wii shows swapped in 480p: NTSC 480p
 * (TVNtsc480Prog), so the encoder is set while the VI is already
 * progressive; and the encoder left as the menu's own mode set it. */
#define RWP_TV_NTSC_480P 2u
#define RWP_TV_KEEP 3u
/* PAL 60 (libogc's TVEurgb60Hz480IntDf): the VI as NTSC, the encoder in
 * PAL colour with its EuRGB60 filter (riftwii/launch.hpp's GcTvSignal). */
#define RWP_TV_EURGB60 4u

/* ---- The screen group's structures (screen_open, screen_frame). ---- */

/* The view a plugin's screen draws in (screen_open), in menu units, 480
 * high. 0 to 640 across is the 4:3 picture; a 16:9 menu reaches further
 * out on both sides, from `left` (negative there) to `left + width`.
 * Keep text and buttons in the safe area, [safe_x, safe_x + safe_w)
 * across and 24 to 456 down: a TV's overscan hides the rest. */
typedef struct rwp_screen_info {
    int32_t left;
    int32_t width;
    int32_t safe_x;
    int32_t safe_w;
    uint32_t widescreen; /* 1 on a 16:9 menu */
    uint32_t glyph;      /* the theme's icon colour, 0xRRGGBBAA, for pictures the plugin paints */
} rwp_screen_info;

/* The buttons of a frame's input, from any controller (Wii Remote,
 * Classic Controller, GameCube controller, Wii U GamePad, USB pads): the
 * menu maps them as it does for its own pages. */
#define RWP_KEY_A 0x0001u
#define RWP_KEY_B 0x0002u
#define RWP_KEY_PLUS 0x0004u
#define RWP_KEY_MINUS 0x0008u
#define RWP_KEY_ONE 0x0010u
#define RWP_KEY_TWO 0x0020u
#define RWP_KEY_HOME 0x0040u
#define RWP_KEY_UP 0x0100u
#define RWP_KEY_DOWN 0x0200u
#define RWP_KEY_LEFT 0x0400u
#define RWP_KEY_RIGHT 0x0800u

typedef struct rwp_input {
    int32_t x, y;      /* where the Wii Remote points, in the view */
    uint32_t pointer;  /* 1 when it points at the screen */
    uint32_t pressed;  /* RWP_KEY_* pressed this frame */
    uint32_t held;     /* RWP_KEY_* held down */
    uint32_t frame;    /* frames since screen_open */
} rwp_input;

/* Draw commands, in the order they are drawn (later ones on top). */
#define RWP_DRAW_CARD 1     /* the theme's card, x y w h; flags RWP_LIT: lit (focus ring) */
#define RWP_DRAW_TEXT 2     /* text at x y: size, color, align; w > 0 cuts it with "..." at that
                             * width, and with h > 0 wraps it into h lines */
#define RWP_DRAW_TEXTURE 3  /* the texture `arg` scaled to x y w h */
#define RWP_DRAW_RECT 4     /* a filled rectangle, x y w h in `color`; `arg` its corners' radius */
#define RWP_DRAW_SKIN 5     /* the theme's picture `arg` (RWP_SKIN_*) at x y, w h where it stretches */
#define RWP_DRAW_PROGRESS 6 /* a progress bar, x y w h, `arg` 0 to 1000 */
#define RWP_DRAW_QR 7       /* a QR code of `text`, x y and w its side */
#define RWP_DRAW_BUTTON 8   /* the face of button `arg` (RWP_KEY_*) for the controller in use, x y, h its size */
#define RWP_DRAW_BACKGROUND 9 /* the theme's background, the whole screen */
#define RWP_DRAW_PROMPT 10    /* one of the page's prompts on its bottom band, as the menu's pages
                             * show them: `arg` the button (RWP_KEY_*), `text` what it does. The
                             * first named is the rightmost. Pointed at and pressed, it is that button. */

#define RWP_LIT 1u
#define RWP_PRIMARY 2u   /* RWP_DRAW_CARD: the tinted card of a page's main action */
#define RWP_ALIGN_LEFT 0u
#define RWP_ALIGN_CENTER 1u
#define RWP_ALIGN_RIGHT 2u

/* The theme's pictures for RWP_DRAW_SKIN. */
#define RWP_SKIN_BAR 1          /* the curved bottom bar, the full width */
#define RWP_SKIN_ROUND 2        /* the round bar button (w = h its size) */
#define RWP_SKIN_ROUND_LIT 3
#define RWP_SKIN_PILL 4         /* the pill button, stretched to w x h */
#define RWP_SKIN_PILL_LIT 5
#define RWP_SKIN_ARROW_LEFT 6   /* the page arrows */
#define RWP_SKIN_ARROW_RIGHT 7
#define RWP_SKIN_TITLE_BAND 8   /* a page's title band, the full width, h high */
#define RWP_SKIN_FOOTER 9       /* a page's bottom band, from y 410 down, where the prompts go */
#define RWP_SKIN_PICTURE 10     /* the theme's own PNG named by `text` (docs/THEMES.md lists them,
                                 * e.g. "store_games") at x y, w h its size; without one, the
                                 * plugin's own texture `color` (painted in rwp_screen_info's glyph).
                                 * "control_<type>" (wiimote, nunchuk, classiccontroller,
                                 * gamecube): the controller picture a game's page shows */

/* The theme's colours for RWP_DRAW_TEXT and RWP_DRAW_RECT, as `color`
 * values; any other value is RGBA itself (0xRRGGBBAA). */
#define RWP_COLOR_INK 1u        /* body text */
#define RWP_COLOR_INK_SOFT 2u   /* secondary text, still readable on composite */
#define RWP_COLOR_ACCENT 3u     /* the theme's accent */
#define RWP_COLOR_ON_BAND 4u    /* text on a title band */
#define RWP_COLOR_CARD 5u       /* a card's fill */
#define RWP_COLOR_WARN 6u       /* the theme's warning colour (an update waiting, a caution) */
#define RWP_COLOR_FIELD 7u      /* a recessed field, as an option chip that is off */

/* The menu's sounds for sound(). */
#define RWP_SOUND_HOVER 1u
#define RWP_SOUND_CLICK 2u
#define RWP_SOUND_BACK 3u

typedef struct rwp_draw {
    uint8_t kind;   /* RWP_DRAW_* */
    uint8_t align;  /* RWP_ALIGN_* (text) */
    uint8_t flags;  /* RWP_LIT */
    uint8_t size;   /* text size, in pixels of the 480-high view */
    int16_t x, y, w, h;
    uint32_t color; /* RWP_COLOR_* or 0xRRGGBBAA */
    uint32_t arg;
    uint8_t alpha;  /* 0 to 255 */
    uint8_t pad[3];
    const char* text;
} rwp_draw;

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

    /* Added after set_sram_flash_id_a (check RWP_HOST_HAS). Free bytes in
     * the loader's heap, including the arenas it can still grow into.
     * This is total space, not the largest single allocation available. */
    uint32_t (*heap_free_bytes)(void);

    /* Added after heap_free_bytes (check RWP_HOST_HAS). As
     * set_sram_flash_id_a, for slot B. */
    int32_t (*set_sram_flash_id_b)(const uint8_t flash_id[12]);

    /* Added after set_sram_flash_id_b (check RWP_HOST_HAS). zlib's
     * streaming inflater, the loader's own copy (inflateInit2_, inflate
     * and inflateEnd), on a z_stream the plugin owns: zlib.h's layout,
     * with the plugin's own zalloc and zfree. Each returns zlib's code
     * (Z_OK, Z_STREAM_END, Z_DATA_ERROR...). */
    int32_t (*zlib_inflate_init2)(void* stream, int32_t window_bits, const char* version, int32_t stream_size);
    int32_t (*zlib_inflate)(void* stream, int32_t flush);
    int32_t (*zlib_inflate_end)(void* stream);

    /* Added after zlib_inflate_end (check RWP_HOST_HAS). The launch
     * screen's progress line: `stage`, a few English words ("..." is
     * added), and the bar at `percent` (0 to 100; it never goes back).
     * Only before the SD card is handed over (sd_hand_over). */
    void (*progress)(const char* stage, int32_t percent);

    /* ---- Added after progress (check RWP_HOST_HAS with screen_frame and
     * so on): a screen of the plugin's own, drawn in the menu's look
     * (the appstore plugin, docs/APPSTORE.md). Immediate mode: each frame
     * the plugin hands RiftWii a list of rwp_draw commands and gets that
     * frame's input back. Menu thread only, between screen_open and
     * screen_close. ---- */

    /* Takes the screen over from the menu (the menu's own page is hidden
     * meanwhile). `out` says how wide the view is: 640, or more on a 16:9
     * menu, where the safe area is wider too. 0, or -1 when it cannot. */
    int32_t (*screen_open)(rwp_screen_info* out);
    /* Draws `count` commands from the next frame on (the texts are copied,
     * so they may be the plugin's own buffers), waits for that frame and
     * fills `input`. 0, or -1 when the screen is not open. */
    int32_t (*screen_frame)(const rwp_draw* commands, uint32_t count, rwp_input* input);
    /* Gives the screen back to the menu and frees the textures left. */
    void (*screen_close)(void);
    /* A picture for RWP_DRAW_TEXTURE, from `rgba` (w x h, 8 bits a channel,
     * any size up to 640 x 480; RiftWii keeps its own copy). Its handle, or
     * 0 when there is no room. texture_free gives one back. */
    uint32_t (*texture_create)(const uint8_t* rgba, uint32_t width, uint32_t height);
    void (*texture_free)(uint32_t handle);
    /* How wide `text` is in the menu's font at `size` pixels. */
    int32_t (*text_width)(const char* text, uint32_t size);
    /* The menu's translation of `english` (wii/lang), or `english` itself.
     * The pointer stays valid while the plugin is loaded. */
    const char* (*translate)(const char* english);
    /* One of the menu's sounds (RWP_SOUND_*). */
    void (*sound)(uint32_t which);
    /* Home's search keyboard over the plugin's screen: `text` (`bytes`
     * long, NUL included) is its start and its answer. Each time the entry
     * changes, `typed(text, ...)` fills `line` (shown beside the entry:
     * "12 apps"), `matches` (names shown above the keys, one a line) and
     * `next` (the characters that still lead somewhere, the other keys
     * dimmed; empty for all). 1 when the player confirmed, 0 when they
     * backed out. */
    int32_t (*ask_search)(char* text, uint32_t bytes,
                          void (*typed)(const char* text, char* line, uint32_t line_bytes, char* matches,
                                        uint32_t matches_bytes, char* next, uint32_t next_bytes, void* arg),
                          void* arg);
    /* The menu's popup: 1 for `yes`, 0 for `no` (or B). `no` may be NULL
     * for a message with one button. */
    int32_t (*ask_choice)(const char* title, const char* text, const char* yes, const char* no);

    /* The network. Blocking: call them from a background job
     * (background_start), never the menu thread. net_ready brings the
     * network up (once a visit; it may take seconds): 1, or 0 with the
     * reason. http_get: an https or http GET into `buffer` (redirects
     * followed), 1 with `*length` the body's bytes, or 0 with the reason.
     * http_download: the body streamed into the file `path` (written
     * whole or removed), its SHA-256 into `sha256`, `progress(bytes so
     * far, arg)` on the way (may be NULL); refused past `max_bytes`. */
    int32_t (*net_ready)(char* error, uint32_t error_bytes);
    int32_t (*http_get)(const char* url, uint8_t* buffer, uint32_t capacity, uint32_t* length, char* error,
                        uint32_t error_bytes);
    int32_t (*http_download)(const char* url, const char* path, uint32_t max_bytes, uint8_t sha256[32],
                             void (*progress)(uint32_t done, void* arg), void* arg, char* error, uint32_t error_bytes);
    /* One job at a time on RiftWii's network thread: `job(arg)` runs there.
     * 0 started, -1 when one is still running. background_cancel makes
     * the running network call give up within a tenth of a second. */
    int32_t (*background_start)(void (*job)(void* arg), void* arg);
    uint32_t (*background_running)(void);
    void (*background_cancel)(void);

    /* More files. remove_path: a file, or a folder that is empty; 0 or -1.
     * rename_path: 0 or -1 (no overwrite). list_directory: `each(name,
     * is_directory, arg)` once per entry ("." and ".." left out); the
     * entries, or -1 when the folder cannot be read. */
    int32_t (*remove_path)(const char* path);
    int32_t (*rename_path)(const char* from, const char* to);
    int32_t (*list_directory)(const char* path, void (*each)(const char* name, uint32_t is_directory, void* arg),
                              void* arg);

    /* Added after list_directory (check RWP_HOST_HAS). The SD card's free
     * room in KiB (at most 0xFFFFFFFF) into *kib: 0, or -1 when it cannot be
     * read. It may walk the whole FAT of a big card: call it from a
     * background job, not from a frame. */
    int32_t (*sd_free_kib)(uint32_t* kib);
    /* Added after sd_free_kib (check RWP_HOST_HAS). The menu's popup for a
     * question whose second answer cannot be undone: `safe` is the first
     * button, lit when it opens, and A, B and HOME press it; `risky` needs
     * the pointer on it and A, or + (START on a GameCube controller), as
     * every such question in the menu does. 1 for `risky`, 0 for `safe`. */
    int32_t (*ask_risky)(const char* title, const char* text, const char* safe, const char* risky);
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

/* launch's failures, and the end of a plugin with a screen of its own
 * (the appstore plugin's launch returns when the player leaves it). */
#define RWP_LAUNCH_DONE 0
#define RWP_LAUNCH_FAILED 1     /* the reason is in the request's error */
#define RWP_LAUNCH_NO_MEMORY 2  /* the loader's heap had no room */

/* The text of the gamecube plugin's real-card SRAM failure, for its log and
 * the request's error text; the DOL acts on rwp_gc_launch's error_code. */
#define RWP_GC_REAL_CARD_SRAM_ERROR "The real memory card's ID could not be written to SRAM."

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
