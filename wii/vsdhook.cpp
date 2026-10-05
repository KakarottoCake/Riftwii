// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vsdhook.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_vsd_bin.h"
#include "umsdev.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kImagePath = "sd:/riftwii/sd.raw";
constexpr const char* kUsbImagePath = "usb:/riftwii/sd.raw";
constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr unsigned kScratchRegister = 12;  // vsd_entry.S's jump back and the veneers
constexpr std::uint32_t kNop = 0x60000000;
constexpr std::uint32_t kMinSectors = 0x4000;  // 8 MiB: anything smaller is not a card image

std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

const vsd_header& header() { return *reinterpret_cast<const vsd_header*>(riftwii_vsd_bin); }

void store_words(std::uint32_t address, const std::uint32_t* words, std::size_t count) {
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(address);
    for (std::size_t i = 0; i < count; ++i) p[i] = words[i];
}

void sync_code(std::uint32_t address, std::uint32_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), bytes);
    ICInvalidateRange(reinterpret_cast<void*>(address), bytes);
}

bool is_plain_branch(std::uint32_t word, std::uint32_t at, std::uint32_t& target) {
    if ((word & 0xFC000003u) != 0x48000000u) return false;
    std::int32_t delta = static_cast<std::int32_t>(word & 0x03FFFFFCu);
    if (delta & 0x02000000) delta -= 0x04000000;
    target = at + static_cast<std::uint32_t>(delta);
    return true;
}

bool overlaps(const MemoryPatch& p, std::uint32_t start, std::uint32_t bytes) {
    if (!p.has_offset || p.value.empty()) return false;
    return p.offset < static_cast<std::uint64_t>(start) + bytes && p.offset + p.value.size() > start;
}

const char* entry_name(unsigned e) {
    static const char* const names[VSD_ENTRIES] = {"IOS_OpenAsync", "IOS_CloseAsync", "IOS_IoctlAsync",
                                                   "IOS_IoctlvAsync", "IOS_Open",      "IOS_Close",
                                                   "IOS_Ioctl",     "IOS_Ioctlv"};
    return e < VSD_ENTRIES ? names[e] : "?";
}

// Where an address of the blob's final code or state is written now.
std::uint32_t now(const VsdHook& h, std::uint32_t address) {
    if (address >= h.data_base && address < h.data_base + h.data_bytes) return address - h.data_base + h.stage_base;
    return address;
}

}  // namespace

bool find_vsd_image(VsdImage& out, std::string& why) {
    out = VsdImage{};
    std::uint64_t size = 0;
    std::vector<Fragment> pieces;
    struct stat st;
    if (stat(kImagePath, &st) == 0) {
        out.path = kImagePath;
        if (!sd_file_pieces(kImagePath, size, pieces, why)) return false;
    } else {
        // On the USB drive, through d2x's device: open for a game on it.
        const ImageVolume* volume = nullptr;
        VolumeFile file;
        if (ums::Fd() < 0) {
            why = "sd.raw is not on the SD card, and the USB drive is read in-game only for a game on the USB drive";
            return false;
        }
        if (ums::SectorBytes() != RTVSD_SECTOR_BYTES) {
            why = "the USB drive does not have 512-byte sectors";
            return false;
        }
        if (!ums::Volume(volume, why) || !volume->lookup(std::string(kUsbImagePath).substr(4), file, why)) {
            why = "sd.raw is on neither the SD card nor the USB drive (" + why + ")";
            return false;
        }
        if (file.entry.is_directory || !file.inline_bytes.empty()) {
            why = "usb:/riftwii/sd.raw is not an image file";
            return false;
        }
        out.path = kUsbImagePath;
        out.on_usb = true;
        size = file.entry.size;
        pieces = file.fragments;
    }
    if (size % RTVSD_SECTOR_BYTES != 0 || size / RTVSD_SECTOR_BYTES < kMinSectors ||
        size / RTVSD_SECTOR_BYTES > 0xFFFFFFFFull) {
        why = "its size (" + std::to_string(size) + " bytes) is not a card's";
        return false;
    }
    std::uint64_t file_sector = 0;
    for (const Fragment& f : pieces) {
        if (f.sector + f.sector_count > 0xFFFFFFFFull) {
            why = "it lies beyond the drive's first 2 TiB";
            return false;
        }
        out.extents.push_back(rtvsd_extent{static_cast<std::uint32_t>(file_sector), static_cast<std::uint32_t>(f.sector),
                                           static_cast<std::uint32_t>(f.sector_count)});
        file_sector += f.sector_count;
    }
    if (out.extents.size() > RTVSD_MAX_EXTENTS) {
        why = "it is in " + std::to_string(out.extents.size()) + " pieces (at most " +
              std::to_string(RTVSD_MAX_EXTENTS) + "); copy it to a freshly formatted drive";
        return false;
    }
    out.sectors = static_cast<std::uint32_t>(size / RTVSD_SECTOR_BYTES);
    if (file_sector < out.sectors) {
        why = "its pieces on the drive are shorter than the file";
        return false;
    }
    out.enabled = true;
    return true;
}

bool plan_vsd_hook(const DolHeader& dol, const VsdImage& image, const VsdDevice& device, std::uint32_t arena1_hi,
                   std::uint32_t mem1_floor, std::uint32_t arena2_lo, std::uint32_t mem1_veneers,
                   const std::vector<MemoryPatch>& patches, VsdHook& out, std::string& why) {
    out = VsdHook{};
    const vsd_header& h = header();
    if (riftwii_vsd_bin_size < sizeof(vsd_header) || h.magic != VSD_MAGIC || h.version != VSD_VERSION ||
        h.size > riftwii_vsd_bin_size || h.context_offset + VSD_CONTEXT_BYTES > h.size) {
        why = "the embedded virtual SD card blob is damaged";
        return false;
    }
    if (!image.enabled || device.fd < 0) {
        why = "no image or no device";
        return false;
    }

    // 1. The game's IPC functions.
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    IpcSymbols symbols;
    IpcApi api;
    if (!find_ipc_symbols(text, symbols, why) || !find_ipc_api(text, symbols, api, why)) {
        why = "the game's IPC functions were not found: " + why;
        return false;
    }
    out.functions[VSD_OPEN_ASYNC] = api.async[kIpcOpen];
    out.functions[VSD_CLOSE_ASYNC] = api.async[kIpcClose];
    out.functions[VSD_IOCTL_ASYNC] = api.async[kIpcIoctlCmd];
    out.functions[VSD_IOCTLV_ASYNC] = api.async[kIpcIoctlvCmd];
    out.functions[VSD_OPEN] = api.sync[kIpcOpen];
    out.functions[VSD_CLOSE] = api.sync[kIpcClose];
    out.functions[VSD_IOCTL] = api.sync[kIpcIoctlCmd];
    out.functions[VSD_IOCTLV] = api.sync[kIpcIoctlvCmd];
    if (out.functions[VSD_OPEN] == 0 && out.functions[VSD_OPEN_ASYNC] == 0) {
        why = "the game has no IOS_Open";
        return false;
    }
    if (out.functions[VSD_IOCTLV] == 0 && out.functions[VSD_IOCTLV_ASYNC] == 0) {
        why = "the game has no IOS_Ioctlv";
        return false;
    }
    logf("Virtual SD card: IPC open %08x/%08x close %08x/%08x ioctl %08x/%08x ioctlv %08x/%08x (async/sync)\n",
         out.functions[VSD_OPEN_ASYNC], out.functions[VSD_OPEN], out.functions[VSD_CLOSE_ASYNC],
         out.functions[VSD_CLOSE], out.functions[VSD_IOCTL_ASYNC], out.functions[VSD_IOCTL],
         out.functions[VSD_IOCTLV_ASYNC], out.functions[VSD_IOCTLV]);

    // 2. Memory: code and state at the top of the MEM2 arena, its end
    //    lowered (the code below the MEM1 arena's top instead when there
    //    is no veneer room), built in place, above this loader's own
    //    memory. Not at the bottom: the arena's start stays where it is,
    //    as Project M 3.6's codes read fixed addresses in Brawl's MEM2
    //    heaps (0x90e60f10, its sound loader) and crashed when they moved.
    out.code_bytes = align_up(h.size);
    out.state_bytes = align_up(sizeof(vsd_state));
    out.code_in_mem2 = mem1_veneers != 0;
    out.veneers = mem1_veneers;
    out.data_bytes = out.state_bytes + (out.code_in_mem2 ? out.code_bytes : 0);
    const std::uint32_t arena2_end = *reinterpret_cast<volatile std::uint32_t*>(kMem2ArenaEndField);
    const std::uint32_t loader_top = reinterpret_cast<std::uint32_t>(SYS_GetArena2Hi());
    out.data_base = (arena2_end - out.data_bytes) & ~31u;
    if (out.code_in_mem2) {
        out.code_base = out.data_base;
        out.state_base = out.data_base + out.code_bytes;
        out.new_arena1_hi = arena1_hi;
    } else {
        out.code_base = (arena1_hi - out.code_bytes) & ~31u;
        out.state_base = out.data_base;
        out.new_arena1_hi = out.code_base;
        if (arena1_hi < out.code_bytes || out.code_base < mem1_floor) {
            why = "no room below the MEM1 arena's top";
            return false;
        }
    }
    out.new_arena2_lo = arena2_lo;
    out.new_arena2_hi = out.data_base;
    out.stage_base = out.data_base;
    if (arena2_end < out.data_bytes || out.data_base < loader_top || out.data_base < align_up(arena2_lo)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "no room in the MEM2 arena (0x%08x-0x%08x, this loader's memory ends at 0x%08x)",
                      arena2_lo, arena2_end, loader_top);
        why = buf;
        return false;
    }
    for (const MemoryPatch& p : patches) {
        bool hit = overlaps(p, out.data_base, out.data_bytes) || overlaps(p, out.code_base, out.code_bytes);
        for (std::uint32_t f : out.functions) hit = hit || (f != 0 && overlaps(p, f, 4));
        if (hit) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            return false;
        }
    }

    // 3. The blob, its context and its state, built where they are staged.
    const std::uint32_t code_now = now(out, out.code_base);
    std::memset(reinterpret_cast<void*>(code_now), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(code_now), riftwii_vsd_bin, h.size);
    vsd_context* ctx = reinterpret_cast<vsd_context*>(code_now + h.context_offset);
    if (ctx->magic != VSD_CONTEXT_MAGIC) {
        why = "the blob's context is not where its header says";
        return false;
    }
    ctx->state = out.state_base;
    vsd_state* st = reinterpret_cast<vsd_state*>(now(out, out.state_base));
    std::memset(st, 0, sizeof(vsd_state));
    st->magic = VSD_STATE_MAGIC;
    st->complete = out.code_base + h.complete;
    for (unsigned e = 0; e < VSD_ENTRIES; ++e) {
        st->original[e] = out.functions[e] != 0 ? out.code_base + h.replay[e] : 0;
    }
    st->backend = device.backend;
    st->fd = device.fd;
    st->sdhc = device.sdhc ? 1 : 0;
    st->rca = device.rca;
    st->card.sectors = image.sectors;
    st->card.extent_count = static_cast<std::uint32_t>(image.extents.size());
    for (std::size_t i = 0; i < image.extents.size(); ++i) st->card.extents[i] = image.extents[i];
    if (rtvsd_reset(&st->card) != 0) {
        why = "the image's pieces do not cover it";
        return false;
    }
    DCFlushRange(reinterpret_cast<void*>(out.stage_base), out.data_bytes);
    out.active = true;
    logf("Virtual SD card: %s, %u MiB as an %s card in %u piece(s); blob %u bytes at 0x%08x, state %u bytes at "
         "0x%08x%s\n",
         image.path.c_str(), image.sectors / 2048, st->card.sdhc ? "SDHC" : "SD", st->card.extent_count, h.size,
         out.code_base, out.state_bytes, out.state_base,
         out.code_in_mem2 ? " (both in MEM2, built at the arena's top)" : "");
    return true;
}

bool install_vsd_hook(VsdHook& hook, std::string& why) {
    if (!hook.active) return false;
    const vsd_header& h = header();
    // The replays: the displaced first instruction and the jump back, or,
    // where another hook's branch already sits, a jump to that hook.
    for (unsigned e = 0; e < VSD_ENTRIES; ++e) {
        const std::uint32_t f = hook.functions[e];
        if (f == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(f);
        std::uint32_t target = 0;
        std::string reason;
        if (is_plain_branch(first, f, target)) {
            const auto jump = encode_absolute_jump(kScratchRegister, target);
            store_words(now(hook, hook.code_base + h.replay[e]), jump.data(), 4);
            continue;
        }
        if (!displaceable(first, kScratchRegister, reason)) {
            logf("Virtual SD card: %s's first instruction (0x%08x) cannot be moved (%s); not hooked\n", entry_name(e),
                 first, reason.c_str());
            hook.functions[e] = 0;
            continue;
        }
        const std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        store_words(now(hook, hook.code_base + h.replay[e]), replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, f + 4);
        store_words(now(hook, hook.code_base + h.resume[e]), resume.data(), 4);
    }
    // A function left unhooked has no original to call through either.
    vsd_state* st = reinterpret_cast<vsd_state*>(now(hook, hook.state_base));
    for (unsigned e = 0; e < VSD_ENTRIES; ++e) {
        if (hook.functions[e] == 0) st->original[e] = 0;
    }
    if ((hook.functions[VSD_OPEN] == 0 && hook.functions[VSD_OPEN_ASYNC] == 0) ||
        (hook.functions[VSD_IOCTLV] == 0 && hook.functions[VSD_IOCTLV_ASYNC] == 0)) {
        why = "IOS_Open or IOS_Ioctlv could not be hooked";
        hook.active = false;
        return false;
    }
    DCFlushRange(reinterpret_cast<void*>(hook.stage_base), hook.data_bytes);
    if (!hook.code_in_mem2) sync_code(hook.code_base, hook.code_bytes);
    // The game's functions to the trampolines, through a veneer each when
    // the code is in MEM2 (out of a branch's reach).
    unsigned hooked = 0, veneer = 0;
    for (unsigned e = 0; e < VSD_ENTRIES; ++e) {
        const std::uint32_t f = hook.functions[e];
        if (f == 0) continue;
        std::uint32_t target = hook.code_base + h.hook[e];
        if (hook.code_in_mem2) {
            const std::uint32_t at = hook.veneers + veneer++ * 16;
            const auto jump = encode_absolute_jump(kScratchRegister, target);
            store_words(at, jump.data(), 4);
            sync_code(at, 16);
            target = at;
        }
        std::uint32_t branch = 0;
        if (!encode_branch(f, target, branch)) {
            logf("Virtual SD card: %s is out of a branch's reach; not hooked\n", entry_name(e));
            continue;
        }
        store_words(f, &branch, 1);
        sync_code(f & ~31u, 32);
        ++hooked;
    }
    if (hooked == 0) {
        why = "nothing could be hooked";
        hook.active = false;
        return false;
    }
    logf("Virtual SD card: hooked %u function(s)%s\n", hooked,
         hook.code_in_mem2 ? ", through veneers in the code handler's list room" : "");
    return true;
}

void place_vsd_hook(const VsdHook& hook) {
    if (!hook.active) return;
    if (hook.data_base != hook.stage_base)
        std::memmove(reinterpret_cast<void*>(hook.data_base), reinterpret_cast<const void*>(hook.stage_base),
                     hook.data_bytes);
    DCFlushRange(reinterpret_cast<void*>(hook.data_base), hook.data_bytes);
    ICInvalidateRange(reinterpret_cast<void*>(hook.data_base), hook.data_bytes);
}

}  // namespace riftwii::wii
