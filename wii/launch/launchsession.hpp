// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "boot.hpp"
#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {

// After a launch with d2x's IOS reload block failed: without clearing it,
// every later IOS reload (the menu's own) would land in the cIOS instead.
void ClearIosReloadBlock(std::string& error);

// After a launch that turned d2x's NAND emulation on came back failed:
// d2x's FAT module lets go of the SD card and libfat and boot.log have it
// again (release_d2x_saves), so the failure reaches boot.log and the menu
// gets its card. Nothing when the emulation was not on.
void TakeCardBackFromD2x();

// A compile's warnings and notes for the log.
void LogCompileNotes(const CompiledMod& mod);

// Shared state for one session: the probe and, lazily, the layout.
struct Session {
    LaunchSource source;
    DiscProbe probe;
    bool probed = false;
    OpenedPartition partition;
    bool laid_out = false;
    void* frag_storage = nullptr;
    std::size_t frag_storage_bytes = 0;
    const char* log_path = nullptr;
    // A pack is on the USB drive: d2x's USB device is opened before the
    // probe opens the game partition, after which d2x refuses to open it.
    bool usb_packs = false;
    // The disc's launch moved to a d2x cIOS for those packs: keep it.
    bool on_cios = false;
    // A game image is kept as the disc for a program that reloads IOS
    // itself (a Homebrew app stand-in): d2x blocks those reloads.
    bool block_ios_reload = false;

    // Set when the session hands a Homebrew Channel app the d2x setup and
    // its reload block, which must outlive this session.
    bool keep_reload_block = false;

    explicit Session(const LaunchSource& launch_source = LaunchSource(), const char* active_log = nullptr)
        : source(launch_source), log_path(active_log) {}

    // A launch that comes back failed: d2x's IOS reload block (set for
    // every USB/SD game) would send the menu's later IOS reloads, and the
    // next game's, into this cIOS.
    ~Session() {
        if (d2x_saves) TakeCardBackFromD2x();
        if (keep_reload_block) return;
        std::string ignored;
        ClearIosReloadBlock(ignored);
    }

    // Saves on the game's page (RiftWii's own folder, not a pack's): the
    // folder, whether the Wii's save is copied in, and whether d2x's NAND
    // emulation keeps them (set by ensure_probe).
    std::string own_saves_dir;
    bool own_saves_clone = false;
    bool own_saves_clone_empty = false;
    bool d2x_saves = false;
    // The game reads the SD card as it runs (packs, saves): the cIOS
    // choice keeps to the bases that reach it (image_cios_order).
    bool sd_in_use = false;
    // RiftWii itself still needs the SD card after the probe, while the
    // game would be loading and running: packs on the card (compiled after
    // the probe, read by the runtime in the game) or a code build. d2x's
    // NAND emulation then stays off and RiftWii's own save code keeps the
    // save, so the card never has two file systems writing to it.
    bool sd_after_probe = false;

    // Opens the game image (or the disc) and probes it; false with `error`.
    bool ensure_probe(std::string& error);
    // The probe, then the partition's file table.
    bool ensure_layout(std::string& error);
    void open_usb_for_packs();
};

}  // namespace riftwii::wii
