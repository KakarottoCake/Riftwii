// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The ctgpr plugin (docs/PLUGINS.md, "The ctgpr plugin"): the native CTGP-R
// launch's planning, loaded into the plugin arena in MEM2 only when CTGP-R
// starts. Its table's launch takes the DOL's CtgprLaunchRequest
// (riftwii/plugin_ctgpr.hpp), builds the plan into it and returns
// kCtgprPlanReady; the DOL then shuts the plugin down and places the plan
// itself. On failure it returns RWP_LAUNCH_FAILED or RWP_LAUNCH_NO_MEMORY
// with the reason in the request.
#include <zlib.h>

#include "ctgprprepare.hpp"
#include "riftwii/ctgparena.hpp"
#include "riftwii/plugin_ctgpr.hpp"
#include "rwpruntime.hpp"

namespace {

// Its own version, for logs; the build stamp is what has to match.
constexpr std::uint32_t kCtgprVersion = 1;

// The launch inside escape()'s window (rwpruntime.hpp). Kept out of line:
// __builtin_setjmp's caller and this must be different functions.
__attribute__((noinline)) int32_t run_launch(riftwii::CtgprLaunchRequest& request) {
    return riftwii::plugin::ctgpr_prepare(request) ? riftwii::kCtgprPlanReady : RWP_LAUNCH_FAILED;
}

int32_t ctgpr_launch(const void* raw, uint32_t bytes) {
    std::string why;
    if (!riftwii::ctgpr_request_check(raw, bytes, why)) {
        riftwii::plugin::host().log("refused the request: %s", why.c_str());
        return RWP_LAUNCH_FAILED;
    }
    // The DOL's request, which this launch fills (the gamecube plugin writes
    // its answers through the request's pointers the same way).
    auto& request = *static_cast<riftwii::CtgprLaunchRequest*>(const_cast<void*>(raw));
    // The big buffers go to the DOL's MEM2 block: this image's copy of
    // g_ctgp_arena points at the DOL's arena for the length of the launch.
    riftwii::g_ctgp_arena = request.arena;
    if (__builtin_setjmp(riftwii::plugin::escape_buffer()) != 0) {
        // Back from escape(): what would have thrown, as a result. What the
        // launch had allocated stays allocated; the DOL gives the MEM2 block
        // back whole (wii/launch/ctgpr.cpp).
        riftwii::g_ctgp_arena = nullptr;
        const riftwii::plugin::Escape taken = riftwii::plugin::escape_taken();
        request.error = taken.why ? taken.why : "the launch stopped";
        riftwii::plugin::host().log("the plan stopped: %s", taken.why ? taken.why : "no reason given");
        return taken.result;
    }
    riftwii::plugin::arm_escape();
    const int32_t result = run_launch(request);
    riftwii::plugin::disarm_escape();
    riftwii::g_ctgp_arena = nullptr;
    return result;
}

}  // namespace

extern "C" {

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);

// extern: a C++ const at namespace scope would otherwise be local to this
// file, and make_plugin.py looks the table up by this name.
extern const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, kCtgprVersion, 0, nullptr, rwp_shutdown, ctgpr_launch,
};

int32_t rwp_init(const rwp_host* host) {
    // A table shorter than this plugin's (an older loader) is refused
    // before any of its functions is called: the cup layouts' unpacker needs
    // the loader's zlib.
    if (!riftwii::plugin::start_runtime(host) || !RWP_HOST_HAS(host, zlib_inflate_end)) return 1;
    host->log("version %u ready, its code at 0x%08x, for RiftWii %s (build %s)", kCtgprVersion,
              static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&rwp_init)), host->version(),
              host->build_stamp());
    return 0;
}

void rwp_shutdown(void) {}

// zlib's inflater (ctgpstage2.cpp's MAIN.DOL unpacking), on the loader's own
// copy (rwp_host's zlib_*).
int inflateInit2_(z_streamp stream, int window_bits, const char* version, int stream_size) {
    return riftwii::plugin::host().zlib_inflate_init2(stream, window_bits, version, stream_size);
}

int inflate(z_streamp stream, int flush) { return riftwii::plugin::host().zlib_inflate(stream, flush); }

int inflateEnd(z_streamp stream) { return riftwii::plugin::host().zlib_inflate_end(stream); }

}  // extern "C"
