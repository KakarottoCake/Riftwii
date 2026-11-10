// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpr.hpp"

#include <sys/stat.h>

#include <cstdlib>

#include "di.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "online.hpp"
#include "plugins.hpp"
#include "riftwii/ctgparena.hpp"
#include "riftwii/plugin_ctgpr.hpp"
#include "riftwii/pluginfile.hpp"
#include "sdfile.hpp"

namespace riftwii::wii {
namespace {

// The plugin that builds the plan (docs/PLUGINS.md, "The ctgpr plugin").
constexpr const char* kPluginName = "ctgpr";

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// The launch's big buffers (BOOT.BIN and BOAT.BIN as read and as linked, the
// page table, the package and hash lists) live in one MEM2 block, so they do
// not fill the MEM1 heap (CtgpBytes draws from it, riftwii/ctgparena.hpp).
// Peak use is about 3.5 MB (both copies of BOOT.BIN and BOAT.BIN, 1.6 MB each,
// before the as-read copies are freed); the block is 6 MB, and a buffer that
// finds no room falls back to the heap. It is released when the plan is
// destroyed, or at once when the launch fails.
constexpr std::size_t kLaunchArenaBytes = 6u << 20;

CtgpArena g_launch_arena;
void* g_launch_arena_block = nullptr;

void reserve_launch_arena() {
    if (g_launch_arena_block != nullptr) return;
    std::size_t bytes = kLaunchArenaBytes;
    void* block = mem::TryMem2Alloc(bytes);
    if (block == nullptr) {
        logf("CTGP-R: no MEM2 block for the launch's buffers; they use the heap\n");
        return;
    }
    g_launch_arena.attach(block, bytes);
    g_launch_arena_block = block;
    g_ctgp_arena = &g_launch_arena;
    logf("CTGP-R: launch buffers in MEM2: %u KiB block at %p\n", static_cast<unsigned>(bytes / 1024), block);
}

// Gives the block back once nothing is left in it. `stopped`: the plugin's
// launch left through its escape (no memory), so blocks of its own locals
// may still be marked in use; nothing points into them any more, and the
// block goes back whole.
void release_launch_arena(bool stopped = false) {
    if (g_launch_arena_block == nullptr) return;
    if (g_launch_arena.live_blocks() != 0) {
        if (!stopped) return;
        logf("CTGP-R: %u buffers of the stopped plan were never freed; the MEM2 block goes back whole\n",
             static_cast<unsigned>(g_launch_arena.live_blocks()));
        g_launch_arena.attach(g_launch_arena_block, 0);
    }
    logf("CTGP-R: launch buffers: most in use at once %u KiB of MEM2\n",
         static_cast<unsigned>(g_launch_arena.peak() / 1024));
    g_ctgp_arena = nullptr;
    g_launch_arena.detach();
    std::free(g_launch_arena_block);
    g_launch_arena_block = nullptr;
}

// The words the player sees for a plugin's CtgprFailure (or the DOL's own
// checks). The technical detail goes to boot.log instead.
std::string failure_text(CtgprFailure why) {
    switch (why) {
    case CtgprFailure::NotMarioKart: return tr("CTGP-R only starts with the Mario Kart Wii game.");
    case CtgprFailure::Region:
        return tr("CTGP-R works with the US, Japanese and European versions of Mario Kart Wii only.");
    case CtgprFailure::NotInstalled:
        return tr("CTGP-R is not installed on the SD card. Install it with its own installer first.");
    case CtgprFailure::Version:
        return tr("This version of CTGP-R can't be started from RiftWii yet. Start CTGP-R from the Homebrew Channel instead.");
    case CtgprFailure::Other: break;
    }
    return tr("CTGP-R could not be started. Its files on the SD card may be damaged or incomplete: install CTGP-R again. The reason is in sd:/riftwii/boot.log.");
}

// The DOL's raw SD reads, as the plugin takes them (CtgprSdAccess).
bool resolve_for_plugin(const std::string& sd_path, Fat32File& out, std::string& error) {
    return resolve_sd_file(sd_path, out, error);
}

}  // namespace

namespace {

bool g_status_known = false;
CtgprNativeStatus g_status = CtgprNativeStatus::NotInstalled;

}  // namespace

CtgprNativeStatus ctgpr_native_status() {
    if (!g_status_known) {
        g_status = ctgpr_native_wanted() ? CtgprNativeStatus::Installed : CtgprNativeStatus::NotInstalled;
        g_status_known = true;
    }
    return g_status;
}

void ctgpr_native_status_forget() { g_status_known = false; }

bool ctgpr_native_wanted() {
    // CTGP-R's own files are on the card: its installer put them there. The
    // launch needs nothing of its own, so their presence is what turns the
    // native launch on.
    return exists(kCtgprBlobPath) && exists(kCtgprJellyPath);
}

bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error) {
    const auto stop = [&](CtgprFailure why, const std::string& technical) {
        logf("CTGP-R: the native launch stops: %s\n", technical.c_str());
        error = failure_text(why);
        return false;
    };
    // What RiftWii can tell without the plugin.
    if (di::has_partition_resolver()) {
        logf("CTGP-R: the native launch stops: RVZ needs RiftWii's resident reader, which conflicts with the "
             "native CTGP-R hooks and page table\n");
        error = tr("CTGP-R can't start from an RVZ image. Use a disc, ISO or WBFS instead.");
        return false;
    }
    if (game_id.compare(0, 3, "RMC") != 0)
        return stop(CtgprFailure::NotMarioKart, "CTGP-R only starts the Mario Kart Wii disc (its game ID starts \"RMC\")");
    if (!ctgpr_native_wanted())
        return stop(CtgprFailure::NotInstalled,
                    "CTGP-R is not installed on the SD card (sd:/ctgpr/blob.bin and jelly.bin are missing): install "
                    "it with its own installer first");

    CtgprLaunchRequest request;
    request.layout = ctgpr_request_layout();
    request.game_id = game_id;
    // Leaving for the channel (its menu buttons, a crash) restarts RiftWii.
    {
        const std::string self = SelfDolPath();
        if (self.rfind("sd:/", 0) == 0 && self.size() + 1 < kCtgpChannelStubLength)
            request.channel_stub = "sda:/" + self.substr(4);
        else if (self.empty())
            logf("CTGP-R: leaving for the CTGP-R Channel will not come back to RiftWii (it was not started from the "
                 "SD card, so there is no path on the card to return to)\n");
        else
            logf("CTGP-R: leaving for the CTGP-R Channel will not come back to RiftWii (its path '%s' does not fit)\n",
                 self.c_str());
    }
    request.sd.resolve = resolve_for_plugin;
    request.sd.read = read_sd_file;
    request.sd.boot_sector = read_sd_boot_sector;

    // The code files and everything built from them go to MEM2 (CtgpBytes).
    reserve_launch_arena();
    request.arena = g_ctgp_arena;
    std::shared_ptr<CtgpLaunchPlan> plan(new CtgpLaunchPlan, [](CtgpLaunchPlan* done) {
        delete done;
        release_launch_arena();
    });
    request.plan = plan.get();

    std::string message;
    const std::int32_t result = plugin::Launch(kPluginName, &request, sizeof(request), message);
    // The plan needs none of the plugin's code (its containers are the DOL's
    // own types on the DOL's heap and MEM2 block): the plugin's arena goes
    // back to the heap before the game is loaded.
    plugin::ShutdownAll("CTGP-R's plan is built");
    if (result == kCtgprPlanReady) {
        out = std::move(plan);
        return true;
    }
    request.plan = nullptr;
    plan.reset();  // its deleter gives the MEM2 block back once nothing is in it
    if (result == RWP_LAUNCH_NO_MEMORY) {
        release_launch_arena(true);
        logf("CTGP-R: the ctgpr plugin ran out of memory: %s\n", request.error.c_str());
        error = mem::LaunchOutOfMemoryMessage();
        return false;
    }
    release_launch_arena();
    if (result < 0) {
        // The plugin is missing, from another RiftWii, damaged or did not
        // start: its own sentence says which, and what to do.
        logf("CTGP-R: the native launch needs RiftWii's ctgpr plugin in %s (%s), and it could not be used: %s\n",
             plugin::Directory().c_str(), plugin_file_name(kPluginName, RIFTWII_VERSION).c_str(), message.c_str());
        error = message;
        return false;
    }
    return stop(request.failure, request.error.empty() ? "the ctgpr plugin gave no reason" : request.error);
}

}  // namespace riftwii::wii
