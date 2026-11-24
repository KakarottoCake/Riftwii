/* SPDX-FileCopyrightText: 2026 RiftWii contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_PLUGIN_APPSTORE_H
#define RIFTWII_PLUGIN_APPSTORE_H

#include <stdint.h>

/* The appstore plugin's request (docs/APPSTORE.md): the menu opens the
 * store with it, and the plugin's launch returns when the player leaves,
 * RWP_LAUNCH_DONE, or RWP_LAUNCH_FAILED with the reason in `error`. */
typedef struct rwp_appstore_request {
    uint32_t size;      /* sizeof(rwp_appstore_request) */
    uint32_t changed;   /* out: apps were installed or removed (Home's Homebrew view reads the card again) */
    char error[200];    /* out: why the store could not open */
} rwp_appstore_request;

#endif
