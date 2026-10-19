// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The gamecube plugin (docs/PLUGINS.md, "Stage (b)"): RiftWii's GameCube
// loader, loaded the first time a GameCube game starts. Its table's
// launch takes the DOL's rwp_gc_launch, checks it, turns it back into the
// loader's C++ (GcImage, GcOverlayPlan, GcLaunchExtras) and starts the
// game; it returns only when the game could not be started.
#include <cstdio>

#include "gc_crash.h"
#include "gchost.hpp"
#include "riftwii/gcrequest.hpp"
#include "riftwii/plugin_gamecube.h"

namespace {

// Its own version, for logs; the build stamp is what has to match.
constexpr std::uint32_t kGamecubeVersion = 1;

void copy_reason(const rwp_gc_launch& request, const char* why) {
    std::snprintf(request.error, request.error_bytes, "%s", why);
}

riftwii::wii::GcImage image_of(const rwp_gc_launch& request) {
    riftwii::wii::GcImage image;
    image.path = request.path;
    image.id = request.id;
    image.rvz = request.is_rvz != 0;
    image.shared_card = request.shared_card != 0;
    image.drive_timing = request.drive_timing != 0;
    image.progressive = request.progressive != 0;
    image.widescreen = request.widescreen != 0;
    image.language = request.language;
    image.sharp = request.sharp != 0;
    image.sixty_hz = request.sixty_hz != 0;
    image.vi_width = request.vi_width;
    image.usb_fd = request.usb_fd;
    if (request.usb_fd >= 0) {
        for (std::uint32_t i = 0; i < request.usb_fragment_count; ++i) {
            const rwp_fragment& f = request.usb_fragments[i];
            image.usb_fragments.push_back(riftwii::Fragment{f.sector, f.sector_count});
        }
        image.usb_bytes = request.usb_file_bytes;
    }
    return image;
}

riftwii::wii::GcLaunchExtras extras_of(const rwp_gc_launch& request) {
    riftwii::wii::GcLaunchExtras extras;
    extras.rumble_off = request.rumble_off != 0;
    extras.in_dolphin = request.in_dolphin != 0;
    if (request.cheat_gct != nullptr)
        extras.cheat_gct.assign(request.cheat_gct, request.cheat_gct + request.cheat_gct_bytes);
    extras.cheat_count = request.cheat_count;
    extras.code_build_codes = request.code_build_codes != 0;
    extras.code_handler = request.code_handler;
    extras.code_handler_bytes = request.code_handler_bytes;
    extras.crash_screen = request.crash_screen;
    extras.crash_screen_bytes = request.crash_screen_bytes;
    extras.crash_screen_way_back = request.crash_screen_way_back != 0;
    extras.adapter_fd = request.adapter_fd;
    extras.adapter_version = request.adapter_version;
    extras.adapter_dev = request.adapter_dev;
    extras.adapter_rumble = request.adapter_rumble != 0;
    if (request.way_back_kind != RWP_GC_WAY_BACK_NONE) {
        extras.way_back_title = request.way_back_title;
        extras.way_back_view = request.way_back_view;
        extras.way_back_view_bytes = request.way_back_view_bytes;
    }
    return extras;
}

// The launch inside escape()'s window (rwpruntime.hpp). Kept out of line:
// __builtin_setjmp's caller and this must be different functions.
__attribute__((noinline)) int32_t run_launch(const rwp_gc_launch& request) {
    std::string error;
    riftwii::GcOverlayPlan plan;
    const bool has_packs = request.pack_fst != nullptr;
    if (has_packs && !riftwii::gc_plan_from_request(request, plan, error)) {
        copy_reason(request, error.c_str());
        return RWP_LAUNCH_FAILED;
    }
    const riftwii::wii::GcImage image = image_of(request);
    const riftwii::wii::GcLaunchExtras extras = extras_of(request);
    riftwii::wii::LaunchGcNative(image, has_packs ? &plan : nullptr, extras, error);
    copy_reason(request, error.empty() ? "the game did not start" : error.c_str());
    return RWP_LAUNCH_FAILED;
}

int32_t gamecube_launch(const void* raw, uint32_t bytes) {
    const auto* request = static_cast<const rwp_gc_launch*>(raw);
    std::string why;
    if (!riftwii::gc_request_check(request, bytes, GC_CRASH_SCREEN_BYTES, why)) {
        riftwii::plugin::host().log("refused the launch: %s", why.c_str());
        if (request != nullptr && request->error != nullptr && request->error_bytes != 0)
            std::snprintf(request->error, request->error_bytes, "%s", why.c_str());
        return RWP_LAUNCH_FAILED;
    }
    if (__builtin_setjmp(riftwii::plugin::escape_buffer()) != 0) {
        // Back from escape(): what would have thrown, as a result.
        const riftwii::plugin::Escape taken = riftwii::plugin::escape_taken();
        copy_reason(*request, taken.why ? taken.why : "the launch stopped");
        riftwii::plugin::host().log("the launch stopped: %s", taken.why ? taken.why : "no reason given");
        return taken.result;
    }
    riftwii::plugin::arm_escape();
    const int32_t result = run_launch(*request);
    riftwii::plugin::disarm_escape();
    return result;
}

}  // namespace

extern "C" {

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);

// extern: a C++ const at namespace scope would otherwise be local to this
// file, and make_plugin.py looks the table up by this name.
extern const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, kGamecubeVersion, 0, nullptr, rwp_shutdown, gamecube_launch,
};

int32_t rwp_init(const rwp_host* host) {
    // A table shorter than this plugin's (an older loader) is refused
    // before any of its functions is called.
    if (!riftwii::plugin::start_runtime(host)) return 1;
    host->log("version %u ready, its code at 0x%08x, for RiftWii %s (build %s)", kGamecubeVersion,
              static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&rwp_init)), host->version(),
              host->build_stamp());
    return 0;
}

void rwp_shutdown(void) {}

}  // extern "C"
