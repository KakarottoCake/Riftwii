// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgpr.hpp"

#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "di.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "online.hpp"
#include "plugins.hpp"
#include "riftwii/ctgparena.hpp"
#include "riftwii/plugin_ctgpr.hpp"
#include "riftwii/pluginfile.hpp"
#include "riftwii/update.hpp"
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
        logf("CTGP-R: no MEM2 block for the launch's buffers (the launch cannot start without it)\n");
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
    if (stopped) {
        // The plugin left through its escape and gave back every heap block
        // it held, the arena's bookkeeping nodes among them: the arena is
        // started afresh in place (not cleared, which would free those nodes
        // again), and the block goes back whole.
        logf("CTGP-R: the stopped plan's MEM2 buffers go back whole\n");
        const std::size_t peak = g_launch_arena.peak();
        new (&g_launch_arena) CtgpArena();
        logf("CTGP-R: launch buffers: most in use at once %u KiB of MEM2\n", static_cast<unsigned>(peak / 1024));
        g_ctgp_arena = nullptr;
        std::free(g_launch_arena_block);
        g_launch_arena_block = nullptr;
        return;
    }
    if (g_launch_arena.live_blocks() != 0) return;
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

bool ctgpr_native_wanted() {
    // CTGP-R's own files are on the card: its installer put them there. The
    // launch needs nothing of its own, so their presence is what turns the
    // native launch on.
    return exists(kCtgprBlobPath) && exists(kCtgprJellyPath);
}

namespace {

constexpr const char* kReturnNotePath = "sd:/riftwii/ctgpr-return.txt";
constexpr const char* kReturnNoteTemp = "sd:/riftwii/ctgpr-return.tmp";
// The path this launch wrote into CTGP-R, for the note at the handover.
std::string g_return_path;

bool card_file(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}  // namespace

void leave_ctgpr_return_note() {
    const std::string text = ctgpr_return_note_text(g_return_path, RIFTWII_VERSION);
    g_return_path.clear();
    if (text.empty()) return;
    // Whole or not at all: a temporary file renamed over the note.
    FILE* f = std::fopen(kReturnNoteTemp, "wb");
    bool ok = f != nullptr && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f != nullptr && std::fclose(f) != 0) ok = false;
    std::remove(kReturnNotePath);
    if (!ok || std::rename(kReturnNoteTemp, kReturnNotePath) != 0) {
        std::remove(kReturnNoteTemp);
        logf("CTGP-R: could not leave the way back's path in %s: leaving for the channel restarts RiftWii from "
             "sd:/apps/riftwii if this copy is elsewhere\n", kReturnNotePath);
    }
}

std::string take_ctgpr_return_note() {
    FILE* f = std::fopen(kReturnNotePath, "rb");
    if (f == nullptr) return "";
    char text[512] = {};
    const std::size_t got = std::fread(text, 1, sizeof(text) - 1, f);
    std::fclose(f);
    std::remove(kReturnNotePath);
    return ctgpr_return_note_path(std::string(text, got), RIFTWII_VERSION, card_file);
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
        if (self.rfind("sd:/", 0) == 0 && self.size() + 1 < kCtgpChannelStubLength) {
            request.channel_stub = "sda:/" + self.substr(4);
            g_return_path = self;
        }
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
    // Held raw until the plugin is done: after an escape its containers'
    // blocks are already given back, and it must not be destroyed.
    CtgpLaunchPlan* const built = new CtgpLaunchPlan;
    request.plan = built;

    std::string message;
    const std::int32_t result = plugin::Launch(kPluginName, &request, sizeof(request), message);
    // The plan needs none of the plugin's code (its containers are the DOL's
    // own types on the DOL's heap and MEM2 block): the plugin's arena goes
    // back to the heap before the game is loaded.
    plugin::ShutdownAll("CTGP-R's plan is built");
    if (request.escaped != 0) {
        // Only the plan's own memory: what it held went with the plugin's
        // blocks (riftwii/plugin_ctgpr.hpp), so no destructor runs.
        ::operator delete(static_cast<void*>(built));
        request.plan = nullptr;
        release_launch_arena(true);
        if (result == RWP_LAUNCH_NO_MEMORY) {
            logf("CTGP-R: the ctgpr plugin ran out of memory (its own log line above says where)\n");
            error = mem::LaunchOutOfMemoryMessage();
            return false;
        }
        return stop(CtgprFailure::Other, request.error.empty() ? "the ctgpr plugin stopped" : request.error);
    }
    std::shared_ptr<CtgpLaunchPlan> plan(built, [](CtgpLaunchPlan* done) {
        delete done;
        release_launch_arena();
    });
    if (result == kCtgprPlanReady) {
        // The block itself goes back at the handover, after the last log line.
        logf("CTGP-R: launch buffers: most in use at once %u KiB of MEM2 (plan built)\n",
             static_cast<unsigned>(g_launch_arena.peak() / 1024));
        out = std::move(plan);
        return true;
    }
    request.plan = nullptr;
    plan.reset();  // its deleter gives the MEM2 block back once nothing is in it
    release_launch_arena();
    if (result == RWP_LAUNCH_NO_MEMORY) {
        // Out of memory before the plan started (no room to track its heap
        // blocks): nothing was taken.
        logf("CTGP-R: the ctgpr plugin ran out of memory (its own log line above says where)\n");
        error = mem::LaunchOutOfMemoryMessage();
        return false;
    }
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
