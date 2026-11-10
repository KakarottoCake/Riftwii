// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgparena.hpp"
#include "riftwii/ctgplaunch.hpp"
#include "riftwii/fat32.hpp"

// The ctgpr plugin's request (docs/PLUGINS.md, "The ctgpr plugin"): what the
// DOL gives the plugin's launch so it can build the native CTGP-R launch's
// plan, and where the plugin puts it.
//
// Unlike the gamecube plugin's plain-C request, this one carries RiftWii's
// own C++ types: the plan the plugin fills is the DOL's CtgpLaunchPlan, which
// the DOL then checks and writes into MEM1 itself (ctgp_check_layout,
// ctgp_place, ctgp_merge_files). That is sound only because a plugin is
// loaded by the one DOL whose build stamp it carries (the same compiler, the
// same headers), and because both sides allocate from the same places: the
// plugin's operator new is the loader's heap (rwp_host's mem1_alloc,
// memalign and free, as the DOL's own), and its big buffers come from the
// DOL's CtgpArena in MEM2, passed here by address. layout guards the rest: a
// plugin whose types disagree with the DOL's in size refuses the request.
namespace riftwii {

// Why the plugin stopped, for the words the player gets (the DOL's
// translated texts, wii/launch/ctgpr.cpp); the technical detail is in
// CtgprLaunchRequest::error.
enum class CtgprFailure : std::int32_t {
    Other = 0,         // damaged or incomplete files: "install CTGP-R again"
    NotMarioKart = 1,  // not the Mario Kart Wii disc
    Region = 2,        // not a US, Japanese or European disc
    NotInstalled = 3,  // blob.bin or jelly.bin missing
    Version = 4,       // a CTGP-R the launch is not made for
};

// The DOL's raw reads of the SD card (wii/core/sdfile.hpp), which the plugin
// uses to read the package disks without a file system in the way.
struct CtgprSdAccess {
    bool (*resolve)(const std::string& sd_path, Fat32File& out, std::string& error) = nullptr;
    bool (*read)(const Fat32File& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) = nullptr;
    bool (*boot_sector)(std::uint64_t& volume_lba, std::vector<std::uint8_t>& sector, std::string& error) = nullptr;
};

// The sizes of the shared types, as one number both sides work out the same
// way from their own headers.
constexpr std::uint32_t ctgpr_request_layout();

struct CtgprLaunchRequest {
    std::uint32_t size = sizeof(CtgprLaunchRequest);
    std::uint32_t layout = 0;  // ctgpr_request_layout() of the DOL

    // In.
    std::string game_id;       // the disc's ID ("RMCP01")
    std::string channel_stub;  // CtgpInputs::channel_stub: CTGP's way back to RiftWii, or empty
    CtgpArena* arena = nullptr;  // the launch's MEM2 block, or null (the heap then)
    CtgprSdAccess sd;

    // Out. `plan` is the DOL's, empty when it comes in; on success the
    // plugin's launch returns RWP_CTGPR_PLAN_READY with it filled.
    CtgpLaunchPlan* plan = nullptr;
    CtgprFailure failure = CtgprFailure::Other;
    std::string error;  // technical, for boot.log
};

constexpr std::uint32_t ctgpr_request_layout() {
    return static_cast<std::uint32_t>(sizeof(CtgprLaunchRequest) + 0x100u * sizeof(CtgpLaunchPlan) +
                                      0x10000u * sizeof(CtgpGameOp) + 0x1000000u * sizeof(CtgpMergedFile)) ^
           static_cast<std::uint32_t>(sizeof(std::string) << 4) ^ static_cast<std::uint32_t>(sizeof(Fat32File) << 12) ^
           static_cast<std::uint32_t>(sizeof(CtgpMmu) << 20) ^ static_cast<std::uint32_t>(sizeof(CtgpArena) << 26);
}

// The plugin's launch returns this when the plan is built (and the DOL starts
// the game); RWP_LAUNCH_FAILED or RWP_LAUNCH_NO_MEMORY when it is not.
constexpr std::int32_t kCtgprPlanReady = 0;

// Whether `raw` (`bytes` long) is a request this side can use: the right
// size and layout, every function and the plan there. False with `why`.
inline bool ctgpr_request_check(const void* raw, std::uint32_t bytes, std::string& why) {
    const auto* request = static_cast<const CtgprLaunchRequest*>(raw);
    if (request == nullptr || bytes != sizeof(CtgprLaunchRequest) || request->size != sizeof(CtgprLaunchRequest)) {
        why = "the request is not the size this plugin was built for";
        return false;
    }
    if (request->layout != ctgpr_request_layout()) {
        why = "the request's types are laid out differently from this plugin's";
        return false;
    }
    if (request->plan == nullptr || request->sd.resolve == nullptr || request->sd.read == nullptr ||
        request->sd.boot_sector == nullptr) {
        why = "the request has no plan or no way to read the SD card";
        return false;
    }
    return true;
}

}  // namespace riftwii
