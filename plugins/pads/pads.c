/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The pads plugin (docs/PLUGINS.md, "The pads plugin"): carries the six
 * builds of the in-game pad code (runtime/pad: the GameCube adapter, the
 * other USB pads and the HOME Menu by the controller), which the DOL used
 * to embed. Its launch copies the build the DOL asks for into the DOL's
 * buffer; the DOL checks it, shuts the plugin down and puts it into the
 * game itself (wii/hooks/padhook.cpp), as before. */
#include "riftwii/plugin_api.h"
#include "riftwii/plugin_pads.h"

#define PADS_VERSION 1u

typedef struct pads_build {
    const uint8_t* bytes;
    uint32_t size;
} pads_build;

/* padsblobs.S: the builds, in padhook.cpp's BlobKind order. */
extern const pads_build pads_builds[RWP_PAD_BUILDS];

static const rwp_host* g_host;

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);
static int32_t pads_launch(const void* request, uint32_t request_bytes);

const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, PADS_VERSION, 0, 0, rwp_shutdown, pads_launch,
};

int32_t rwp_init(const rwp_host* host) {
    /* move_bytes is API 2's: an older table is refused before it is used. */
    if (host->api_version != RWP_API_VERSION || !RWP_HOST_HAS(host, move_bytes)) return 1;
    g_host = host;
    return 0;
}

void rwp_shutdown(void) {}

static void say(rwp_pads_request* r, const char* text) {
    uint32_t i = 0;
    while (text[i] != '\0' && i + 1 < RWP_PADS_ERROR_BYTES) {
        r->error[i] = text[i];
        ++i;
    }
    r->error[i] = '\0';
}

static int32_t pads_launch(const void* request, uint32_t request_bytes) {
    rwp_pads_request* r = (rwp_pads_request*)request;
    const pads_build* b;
    if (request_bytes != sizeof(rwp_pads_request) || r == 0 || r->size != sizeof(rwp_pads_request))
        return RWP_LAUNCH_FAILED; /* no request of this plugin's to say why in */
    r->written = 0;
    r->error[0] = '\0';
    if (r->build >= RWP_PAD_BUILDS) {
        say(r, "no such pad build");
        return RWP_LAUNCH_FAILED;
    }
    b = &pads_builds[r->build];
    if (r->out == 0 || ((uint32_t)(uintptr_t)r->out & 31u) != 0 || r->capacity < b->size) {
        say(r, "the buffer for the pad build is too small or not aligned");
        return RWP_LAUNCH_FAILED;
    }
    g_host->move_bytes(r->out, b->bytes, b->size);
    g_host->flush_range(r->out, b->size);
    r->written = b->size;
    return RWP_PADS_COPIED;
}
