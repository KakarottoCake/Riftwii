/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_PLUGIN_PADS_H
#define RIFTWII_PLUGIN_PADS_H

/* The pads plugin's request (docs/PLUGINS.md, "The pads plugin"): the
 * in-game pad code (runtime/pad) is twelve builds, carried by the plugin
 * instead of the DOL. At a Wii game's launch the DOL asks for the one
 * the game takes (wii/hooks/padhook.cpp picks it); the plugin copies it
 * into the DOL's buffer and is shut down before the game starts. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The builds, in padhook.cpp's BlobKind order: the full one, adapter
 * only, other USB pads only, then the same three without the HOME Menu
 * by the controller, then the full and adapter-only ones with the Wii
 * controller stand-ins (#587), for a game whose setting is on: as Classic
 * Controllers, then as a Wii Remote + Nunchuk, then as a sideways Wii
 * Remote, then as a pointing Wii Remote. */
#define RWP_PAD_BUILDS 14u

#define RWP_PADS_ERROR_BYTES 160u

typedef struct rwp_pads_request {
    uint32_t size;      /* sizeof(rwp_pads_request) */
    uint32_t build;     /* which build, below RWP_PAD_BUILDS */
    uint8_t* out;       /* the DOL's buffer, 32-byte aligned */
    uint32_t capacity;  /* its bytes */
    uint32_t written;   /* out: the build's bytes copied */
    char error[RWP_PADS_ERROR_BYTES]; /* out: why, when it fails */
} rwp_pads_request;

/* launch's result when the build is copied (its failures are
 * RWP_LAUNCH_FAILED, with the reason in error). */
#define RWP_PADS_COPIED 0

#ifdef __cplusplus
}
#endif

#endif /* RIFTWII_PLUGIN_PADS_H */
