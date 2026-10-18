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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The version of both tables below. tools/make_plugin.py writes it into
 * every plugin's header; the loader refuses a plugin built for another. */
#define RWP_API_VERSION 1

/* The plugin arena in MEM2, taken from the top of the loader's MEM2 heap
 * the first time a plugin loads (wii/core/plugins.cpp). Every plugin's
 * image, .bss and state must fit in it; the build refuses one that does
 * not. */
#define RWP_ARENA_BYTES (256u * 1024u)

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

    /* Stage (b) adds the Menu, Launch and System groups here, at the end. */
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
    /* Stage (b) adds list, details and launch here. */
} rwp_plugin;

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
