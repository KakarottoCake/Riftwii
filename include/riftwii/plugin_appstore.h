/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_PLUGIN_APPSTORE_H
#define RIFTWII_PLUGIN_APPSTORE_H

#include <stdint.h>

/* The appstore plugin's request (docs/APPSTORE.md): the menu opens the
 * store with it, and the plugin's launch returns when the player leaves,
 * RWP_LAUNCH_DONE, or RWP_LAUNCH_FAILED with the reason in `error`. */
/* rwp_appstore_request.console: which console the store runs on, so the
 * apps listed for another are marked. */
#define RWP_CONSOLE_WII 0u
#define RWP_CONSOLE_VWII 1u /* a Wii U in Wii mode */

typedef struct rwp_appstore_request {
    uint32_t size;      /* sizeof(rwp_appstore_request) */
    uint32_t changed;   /* out: apps were installed or removed (Home's Homebrew view reads the card again) */
    uint32_t console;   /* in: RWP_CONSOLE_* */
    char error[200];    /* out: why the store could not open */
    uint32_t check_update; /* out: RiftWii's own page asked for its update check (the menu runs it) */
    /* in: a game ID ("RMGE01") opens the store on the Riivolution mods for
     * that game (its Mods page's "Get more mods"); "" for the apps.
     * game_name is the game's name for the page's title. */
    char game_id[8];
    char game_name[64];
} rwp_appstore_request;

#endif
