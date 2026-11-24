// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The appstore plugin (docs/APPSTORE.md): the app store's pages, loaded
// into the plugin arena only while the store is open, so the DOL carries
// none of it. Its launch takes the menu's rwp_appstore_request
// (riftwii/plugin_appstore.h), runs the store on a screen of its own
// (rwp_host's screen group) and returns when the player leaves.
//
// It runs no escape window (plugins/common/rwpruntime.hpp): its downloads
// run on RiftWii's network thread, and an escape taken there would jump
// across threads. Its big buffers are taken on the menu thread before a
// job starts, and every count it allocates for is capped by the list's
// rules, so running out of memory is a crash the report names, not a
// half-made page.
#include <zlib.h>

#include <cstdio>

#include "riftwii/plugin_appstore.h"
#include "rwpruntime.hpp"
#include "storeui.hpp"

namespace {

constexpr std::uint32_t kAppStoreVersion = 1;

int32_t appstore_launch(const void* raw, uint32_t bytes) {
    if (raw == nullptr || bytes < sizeof(rwp_appstore_request) ||
        static_cast<const rwp_appstore_request*>(raw)->size != sizeof(rwp_appstore_request)) {
        riftwii::plugin::host().log("refused a request of %u bytes", static_cast<unsigned>(bytes));
        return RWP_LAUNCH_FAILED;
    }
    auto& request = *static_cast<rwp_appstore_request*>(const_cast<void*>(raw));
    request.changed = 0;
    request.error[0] = '\0';
    return riftwii::appstore::RunStore(request);
}

}  // namespace

extern "C" {

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);

extern const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, kAppStoreVersion, 0, nullptr, rwp_shutdown, appstore_launch,
};

int32_t rwp_init(const rwp_host* host) {
    // An older RiftWii without the screen, network and file groups is
    // refused before any of them is called.
    if (!riftwii::plugin::start_runtime(host) || !RWP_HOST_HAS(host, list_directory) ||
        !RWP_HOST_HAS(host, zlib_inflate_end))
        return 1;
    host->log("version %u ready, its code at 0x%08x, for RiftWii %s (build %s)", kAppStoreVersion,
              static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&rwp_init)), host->version(), host->build_stamp());
    return 0;
}

void rwp_shutdown(void) {}

// zlib's inflater (icons, zip files), on RiftWii's own copy.
int inflateInit_(z_streamp stream, const char* version, int stream_size) {
    return riftwii::plugin::host().zlib_inflate_init2(stream, MAX_WBITS, version, stream_size);
}

int inflateInit2_(z_streamp stream, int window_bits, const char* version, int stream_size) {
    return riftwii::plugin::host().zlib_inflate_init2(stream, window_bits, version, stream_size);
}

int inflate(z_streamp stream, int flush) { return riftwii::plugin::host().zlib_inflate(stream, flush); }

int inflateEnd(z_streamp stream) { return riftwii::plugin::host().zlib_inflate_end(stream); }

}  // extern "C"
