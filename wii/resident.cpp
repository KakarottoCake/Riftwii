// SPDX-License-Identifier: GPL-3.0-or-later
#include "resident.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_rt_bin.h"
#include "rt_hook.h"
#include "rtfs.h"

namespace riftwii::wii {
namespace {

// The MEM1 arena end the SDK's OSInit takes, in this order of preference
// (Kirby's Epic Yarn's OSInit, section 23): 0x80003110 when it is set,
// else 0x80000034, else the FST address. The apploader has just set 0x34
// and the FST fields; 0x3110 (wiibrew memory map: "MEM1 Arena End") is
// whatever the previous program left and the loader overwrites it at the
// handover, so it is not an input here. The BI2 sits right below the FST
// when the apploader put it there, 0x2000 bytes at the pointer in 0xF4.
constexpr std::uint32_t kMem1ArenaHiField = 0x80000034;
constexpr std::uint32_t kFstAddressField = 0x80000038;
constexpr std::uint32_t kFstSizeField = 0x8000003C;
constexpr std::uint32_t kBi2Field = 0x800000F4;
constexpr std::uint32_t kBi2Bytes = 0x2000;
constexpr std::uint32_t kMem2ArenaLoField = 0x80003124;   // written by IOS at reload (Dolphin IOS.cpp, wiibrew)
constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr unsigned kContinueScratchRegister = 12;         // see rt_entry.S
constexpr std::uint32_t kNop = 0x60000000;

void store_words(std::uint32_t address, const std::uint32_t* words, std::size_t count) {
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(address);
    for (std::size_t i = 0; i < count; ++i) p[i] = words[i];
}

void sync_code(std::uint32_t address, std::size_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), static_cast<u32>(bytes));
    ICInvalidateRange(reinterpret_cast<void*>(address), static_cast<u32>(bytes));
}

const char* ipc_entry_name(std::uint32_t entry) {
    static const char* const names[RT_IPC_ENTRIES] = {
        "IOS_OpenAsync", "IOS_CloseAsync", "IOS_ReadAsync", "IOS_WriteAsync", "IOS_SeekAsync", "IOS_IoctlAsync",
        "IOS_IoctlvAsync", "IOS_Open", "IOS_Close", "IOS_Read", "IOS_Write", "IOS_Seek", "IOS_Ioctl", "IOS_Ioctlv"};
    return entry < RT_IPC_ENTRIES ? names[entry] : "?";
}

}  // namespace

std::uint32_t game_arena1_hi() {
    std::uint32_t arena1_hi = read32(kMem1ArenaHiField);
    if (arena1_hi == 0) arena1_hi = read32(kFstAddressField);
    const std::uint32_t bi2 = read32(kBi2Field);
    if (bi2 != 0 && bi2 < arena1_hi && arena1_hi - bi2 <= kBi2Bytes) arena1_hi = bi2;  // keep it whole
    return arena1_hi;
}

bool install_resident(const DolHeader& dol, const ResidentOptions& options, ResidentInstall& out,
                      std::string& error) {
    ResidentBlob blob;
    if (!parse_resident_blob(riftwii_rt_bin, riftwii_rt_bin_size, blob, error)) return false;

    // 1. Find the SDK's IPC entry points in the text the apploader loaded.
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (!s.used()) continue;
        text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    IpcSymbols symbols;
    if (!find_ipc_symbols(text, symbols, error)) return false;
    logf("Resident: IOS_IoctlAsync at 0x%08x (%u DI commands agree), IOS_IoctlvAsync at 0x%08x\n",
         symbols.ioctl_async, symbols.ioctl_async_commands, symbols.ioctlv_async);
    // The whole API: every function found gets its trampoline. Without
    // the API (an SDK the search does not know) only IOS_IoctlAsync does.
    std::uint32_t entry_address[RT_IPC_ENTRIES] = {};
    entry_address[RT_IPC_ASYNC_IOCTL] = symbols.ioctl_async;
    IpcApi api;
    std::string api_error;
    const bool have_api = find_ipc_api(text, symbols, api, api_error);
    if (have_api) {
        for (std::uint32_t cmd = 1; cmd <= RT_IPC_COMMANDS; ++cmd) {
            entry_address[RT_IPC_ASYNC(cmd)] = api.async[cmd];
            entry_address[RT_IPC_SYNC(cmd)] = api.sync[cmd];
        }
        logf("Resident: IPC API found (async/sync): open %08x/%08x close %08x/%08x read %08x/%08x write %08x/%08x seek %08x/%08x ioctl %08x/%08x ioctlv %08x/%08x\n",
             api.async[1], api.sync[1], api.async[2], api.sync[2], api.async[3], api.sync[3], api.async[4],
             api.sync[4], api.async[5], api.sync[5], api.async[6], api.sync[6], api.async[7], api.sync[7]);
    } else {
        logf("Resident: IPC API not found (%s); only IOS_IoctlAsync is hooked\n", api_error.c_str());
    }

    // 2. The instruction each hook displaces must be safe to replay.
    //    IOS_IoctlAsync's must be; another function's that is not leaves it
    //    unhooked (its calls go to IOS untouched).
    //    Synchronous IOS_Open is hooked on its second instruction when its
    //    first is the usual `stwu r1,-N(r1)`: code that runs that `stwu`
    //    itself and enters at +4 (Pulsar's "OpenFix", which is how it
    //    looks for Riivolution's "file" device) then meets the hook too.
    //    Its replay slot holds both instructions and resumes at +8.
    std::uint32_t displaced[RT_IPC_ENTRIES][4] = {};
    std::uint32_t hook_site[RT_IPC_ENTRIES] = {};
    bool hooked[RT_IPC_ENTRIES] = {};
    unsigned hooked_count = 0;
    bool open_at_second = false;
    for (std::uint32_t e = 0; e < RT_IPC_ENTRIES; ++e) {
        if (entry_address[e] == 0) continue;
        hook_site[e] = entry_address[e];
        if (e == RT_IPC_SYNC(1)) {
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(entry_address[e]);
            const std::uint32_t second = *reinterpret_cast<const std::uint32_t*>(entry_address[e] + 4);
            std::string why;
            if ((first & 0xFFFF8000u) == 0x94218000u && displaceable(second, kContinueScratchRegister, why)) {
                displaced[e][0] = first;
                displaced[e][1] = second;
                displaced[e][2] = kNop;
                displaced[e][3] = kNop;
                hook_site[e] = entry_address[e] + 4;
                open_at_second = true;
                hooked[e] = true;
                ++hooked_count;
                continue;
            }
        }
        bool ok = true;
        for (unsigned i = 1; i < 4; ++i) displaced[e][i] = kNop;  // the replay slot's padding
        for (unsigned i = 0; i < kHookStubBytes / 4 && ok; ++i) {
            displaced[e][i] = *reinterpret_cast<const std::uint32_t*>(entry_address[e] + i * 4);
            std::string why;
            if (!displaceable(displaced[e][i], kContinueScratchRegister, why)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "instruction %u of %s (0x%08x) cannot be displaced: ", i,
                              ipc_entry_name(e), displaced[e][i]);
                if (e == RT_IPC_ASYNC_IOCTL) {
                    error = buf + why;
                    return false;
                }
                logf("Resident: %s%s; not hooked\n", buf, why.c_str());
                ok = false;
            }
        }
        hooked[e] = ok;
        if (ok) ++hooked_count;
    }

    // 3. Reserve the top of the MEM1 arena for the blob and the bottom of
    //    the MEM2 arena for the redirect payload, the bounce buffers and
    //    the savegame state (riftwii/hook.hpp: the top stays the game's).
    //    The data is built at the staging area with its final addresses;
    //    the loader copies it down just before the jump.
    const bool has_table = !options.pieces.empty();
    const bool has_sd = options.pieces.needs_sd();
    if (has_sd && symbols.ioctlv_async == 0) {
        error = "SD-backed replacements need the game's IOS_IoctlvAsync, which was not found";
        return false;
    }
    if (has_sd && options.sdio_fd < 0) {
        error = "SD-backed replacements need an open SD card";
        return false;
    }
    const bool has_usb = options.pieces.needs_usb();
    if (has_usb && (symbols.ioctlv_async == 0 || options.usb_fd < 0)) {
        error = symbols.ioctlv_async == 0 ? "files on the USB drive need the game's IOS_IoctlvAsync, which was not found"
                                          : "files on the USB drive need d2x's USB device open";
        return false;
    }
    bool has_fs = options.savegame.enabled;
    bool file_device = options.savegame.file_device;
    if (file_device && !has_fs) {
        std::string why;
        if (!have_api) {
            why = "the game's IPC API was not found (" + api_error + ")";
        } else if (options.sdio_fd < 0) {
            why = "no SD card";
        } else if (api.sync[7] == 0 || api.async[7] == 0 || api.async[6] == 0) {
            why = "the game lacks IOS_Ioctlv, IOS_IoctlvAsync or IOS_IoctlAsync";
        } else if (options.savegame.volume.root_cluster < 2) {
            why = "the card's root is unknown";
        } else {
            for (std::uint32_t e = 0; e < RT_IPC_ENTRIES && why.empty(); ++e) {
                if (entry_address[e] != 0 && !hooked[e]) why = std::string(ipc_entry_name(e)) + " cannot be hooked";
            }
        }
        if (why.empty()) {
            has_fs = true;
        } else {
            logf("Resident: Riivolution's \"file\" device is off: %s\n", why.c_str());
            file_device = false;
        }
    }
    if (options.savegame.enabled) {
        if (!have_api) {
            error = "savegame redirection needs the game's IPC API: " + api_error;
            return false;
        }
        if (options.sdio_fd < 0) {
            error = "savegame redirection needs an open SD card";
            return false;
        }
        if (api.sync[7] == 0 || api.async[7] == 0 || api.async[6] == 0) {
            error = "savegame redirection needs the game's IOS_Ioctlv, IOS_IoctlvAsync and IOS_IoctlAsync";
            return false;
        }
        for (std::uint32_t e = 0; e < RT_IPC_ENTRIES; ++e) {
            if (entry_address[e] != 0 && !hooked[e]) {
                error = std::string("savegame redirection needs every IPC API function hooked; ") +
                        ipc_entry_name(e) + " cannot be";
                return false;
            }
        }
        if (options.savegame.prefix.empty() || options.savegame.prefix.size() >= RTFS_PATH_BYTES ||
            options.savegame.volume.dir_cluster < 2) {
            error = "savegame redirection: bad prefix or folder";
            return false;
        }
    }
    const std::uint32_t fs_bytes = has_fs ? static_cast<std::uint32_t>(sizeof(rt_fs_state)) + 32 : 0;
    const std::uint32_t sdio_fd = options.sdio_fd < 0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(options.sdio_fd);
    // A replacement the game already holds in memory for its whole run is
    // served from there: the rewritten FST the apploader loaded, which the
    // SDK keeps above its arena at the address in 0x38. Every byte kept
    // out of the MEM2 arena is one the game's heaps keep.
    PayloadPieces pieces = options.pieces;
    const std::uint32_t fst_address = read32(kFstAddressField);
    const std::uint32_t fst_bytes = read32(kFstSizeField);
    std::uint32_t in_place_bytes = 0;
    for (MemReplacement& r : pieces.mem) {
        if (fst_address >= 0x80000000u && fst_address < 0x81800000u && !r.bytes.empty() &&
            r.bytes.size() <= fst_bytes &&
            std::memcmp(reinterpret_cast<const void*>(fst_address), r.bytes.data(), r.bytes.size()) == 0) {
            r.in_place = fst_address;
            in_place_bytes += static_cast<std::uint32_t>(r.bytes.size());
        }
    }
    std::vector<std::uint8_t> payload;
    if (has_table && !build_payload(pieces, 0, options.table_tag, sdio_fd, payload, error)) return false;
    // Bounce buffers whenever a run may be fetched (SD, USB or DISC):
    // anything beyond plain MEM replacements.
    const bool has_disc = !options.pieces.disc.empty() || !options.pieces.entries.empty();
    const std::uint32_t bounce_bytes = has_sd || has_usb || has_disc ? RT_MAX_PENDING * RT_BOUNCE_BYTES : 0;
    const std::uint32_t arena1_hi = game_arena1_hi();
    const std::uint32_t arena2_lo = read32(kMem2ArenaLoField);
    const std::uint32_t arena2_end = read32(kMem2ArenaEndField);
    ResidentPlacement place;
    if (!plan_resident_placement(arena1_hi, options.mem1_floor, arena2_lo, arena2_end, blob.size,
                                 static_cast<std::uint32_t>(payload.size()) + bounce_bytes + fs_bytes, place, error)) {
        return false;
    }
    const std::uint32_t payload_address = place.data_base;
    if (has_table && !build_payload(pieces, payload_address, options.table_tag, sdio_fd, payload, error)) {
        return false;
    }
    const std::uint32_t bounce_address = payload_address + static_cast<std::uint32_t>(payload.size());
    const std::uint32_t fs_state_address = (bounce_address + bounce_bytes + 31) & ~31u;
    // Where each final address is written now.
    const auto staged = [&](std::uint32_t address) { return address - place.data_base + place.stage_base; };

    // 4. Copy the blob and the payload, fill in the context and the
    //    loader-patched slots: each hooked function's displaced words and
    //    the jump back into it.
    std::memcpy(reinterpret_cast<void*>(place.code_base), riftwii_rt_bin, blob.size);
    if (!payload.empty()) {
        std::memcpy(reinterpret_cast<void*>(staged(payload_address)), payload.data(), payload.size());
    }
    rt_context* ctx = reinterpret_cast<rt_context*>(place.code_base + blob.context_offset);
    ctx->flags = options.gecko ? RT_FLAG_GECKO : 0;
    ctx->gecko_channel = 1;
    ctx->original_ioctl_async = symbols.ioctl_async;
    ctx->table = payload.empty() ? 0 : payload_address;
    ctx->complete_entry = place.code_base + blob.complete_di_offset;
    ctx->virtual_start_words = payload.empty() ? 0 : options.virtual_start_words;
    ctx->sdio_fd = sdio_fd;
    ctx->sdio_sdhc = options.sdio_d2x ? RT_SD_D2X : options.sdio_sdhc ? 1 : 0;
    ctx->usb_fd = options.usb_fd < 0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(options.usb_fd);
    // What the runtime calls when it needs an SDK function itself: the
    // replay slot of a hooked one (its displaced words, then the jump
    // back), the function itself when it is not hooked, 0 when absent.
    const auto original = [&](std::uint32_t e) -> std::uint32_t {
        if (entry_address[e] == 0) return 0;
        return hooked[e] ? place.code_base + blob.replay_offsets[e] : entry_address[e];
    };
    ctx->ioctlv_async = original(RT_IPC_ASYNC(7));
    ctx->di_read_entry = original(RT_IPC_ASYNC_IOCTL);  // the unhooked IOS_IoctlAsync
    for (std::uint32_t i = 0; i < RT_MAX_PENDING; ++i) {
        ctx->pending[i].bounce = bounce_bytes != 0 ? bounce_address + i * RT_BOUNCE_BYTES : 0;
    }
    for (std::uint32_t e = 0; e < RT_IPC_ENTRIES; ++e) {
        if (!hooked[e]) continue;
        store_words(place.code_base + blob.replay_offsets[e], displaced[e], 4);
        const auto resume = encode_absolute_jump(kContinueScratchRegister, hook_site[e] + kHookStubBytes);
        store_words(place.code_base + blob.continue_offsets[e], resume.data(), 4);
    }
    if (open_at_second && hooked[RT_IPC_SYNC(1)]) {
        const std::uint32_t undo = 0x80210000u;  // lwz r1,0(r1): the function's own frame, undone
        store_words(place.code_base + blob.open_undo_offsets[0], &undo, 1);
        store_words(place.code_base + blob.open_undo_offsets[1], &undo, 1);
    }
    // The savegame state: the engine's context (volume, prefix), the
    // completion entry and the two originals the sync path calls.
    if (has_fs) {
        // Position-independent: rtfs_init stores no pointer into itself.
        rt_fs_state* st = reinterpret_cast<rt_fs_state*>(staged(fs_state_address));
        std::memset(st, 0, sizeof(*st));
        const char* prefix = options.savegame.enabled ? options.savegame.prefix.c_str() : "";
        if (rtfs_init(&st->fs, &options.savegame.volume, prefix, -1) != RTFAT_OK) {
            error = "savegame redirection: rtfs_init refused the prefix '" + options.savegame.prefix + "'";
            return false;
        }
        if (file_device && rtfs_enable_file_device(&st->fs) != RTFAT_OK) {
            logf("Resident: Riivolution's \"file\" device is off: root cluster %u is not on the card\n",
                 options.savegame.volume.root_cluster);
            file_device = false;
        }
        st->complete_fs = place.code_base + blob.complete_fs_offset;
        st->clone_pending = options.savegame.enabled && options.savegame.clone ? 1u : 0u;
        st->open_sync = original(RT_IPC_SYNC(1));
        st->close_sync = original(RT_IPC_SYNC(2));
        st->read_sync = original(RT_IPC_SYNC(3));
        st->ioctl_sync = original(RT_IPC_SYNC(6));
        st->ioctlv_sync = original(RT_IPC_SYNC(7));
        st->open_async = original(RT_IPC_ASYNC(1));
        st->close_async = original(RT_IPC_ASYNC(2));
        st->read_async = original(RT_IPC_ASYNC(3));
        ctx->fs_state = fs_state_address;
        ctx->flags |= RT_FLAG_FS;
    }
    sync_code(place.code_base, blob.size);

    // 5. Divert the game's functions to their trampolines.
    for (std::uint32_t e = 0; e < RT_IPC_ENTRIES; ++e) {
        if (!hooked[e]) continue;
        std::uint32_t branch = 0;
        if (!encode_branch(hook_site[e], place.code_base + blob.hook_offsets[e], branch)) {
            // Out of b's reach (not in MEM1): the function stays unhooked.
            hooked[e] = false;
            --hooked_count;
            continue;
        }
        store_words(hook_site[e], &branch, 1);
        sync_code(hook_site[e], kHookStubBytes);
    }

    out.code_base = place.code_base;
    out.code_bytes = place.code_bytes;
    out.old_arena1_hi = arena1_hi;
    out.new_arena1_hi = place.new_arena1_hi;
    out.data_base = place.data_base;
    out.data_bytes = place.data_bytes;
    out.stage_base = place.stage_base;
    out.old_arena2_lo = arena2_lo;
    out.new_arena2_lo = place.new_arena2_lo;
    out.ioctl_async = symbols.ioctl_async;
    out.ioctlv_async = symbols.ioctlv_async;
    out.ioctl_async_original = original(RT_IPC_ASYNC_IOCTL);
    out.ioctlv_async_original = original(RT_IPC_ASYNC(7)) != 0 ? original(RT_IPC_ASYNC(7)) : symbols.ioctlv_async;
    out.table = ctx->table;
    out.payload_bytes = static_cast<std::uint32_t>(payload.size());
    out.bounce_bytes = bounce_bytes;
    out.hooked = hooked_count;
    out.hook_site_count = 0;
    for (std::uint32_t e = 0; e < RT_IPC_ENTRIES; ++e) {
        if (!hooked[e]) continue;
        out.hook_sites[out.hook_site_count++] = hook_site[e];
    }
    out.fs_state = has_fs ? fs_state_address : 0;
    logf("Resident: %u bytes at 0x%08x, MEM1 arena top 0x%08x -> 0x%08x, gecko %s, %u IPC function(s) hooked\n",
         blob.size, place.code_base, arena1_hi, place.new_arena1_hi, options.gecko ? "on" : "off", hooked_count);
    if (place.data_bytes != 0) {
        logf("Resident: %u bytes of data at 0x%08x (staged at 0x%08x), MEM2 arena start 0x%08x -> 0x%08x, end 0x%08x kept\n",
             place.data_bytes, place.data_base, place.stage_base, arena2_lo, place.new_arena2_lo, arena2_end);
    }
    if (in_place_bytes != 0) {
        logf("Resident: %u bytes served from the game's own FST at 0x%08x, not copied\n", in_place_bytes, fst_address);
    }
    if (!payload.empty()) {
        logf("Resident: redirect table at 0x%08x, %u MEM + %u SD + %u DISC replacement(s) + %u entries, payload %u bytes, virtual window from word 0x%08x\n",
             ctx->table, static_cast<unsigned>(options.pieces.mem.size()),
             static_cast<unsigned>(options.pieces.sd.size()), static_cast<unsigned>(options.pieces.disc.size()),
             static_cast<unsigned>(options.pieces.entries.size()), static_cast<unsigned>(payload.size()),
             ctx->virtual_start_words);
    }
    if (has_sd) {
        logf("Resident: SD fd %d (%s), bounce buffers at 0x%08x, IOS_IoctlvAsync at 0x%08x\n", options.sdio_fd,
             options.sdio_sdhc ? "SDHC" : "SDSC", bounce_address, symbols.ioctlv_async);
    }
    if (has_usb) {
        logf("Resident: USB fd %d (d2x's /dev/usb2), bounce buffers at 0x%08x\n", options.usb_fd, bounce_address);
    }
    if (file_device) {
        logf("Resident: Riivolution's \"file\" device served from the card's root (cluster %u)%s\n",
             options.savegame.volume.root_cluster, open_at_second ? ", IOS_Open hooked at +4" : "");
    }
    if (has_fs && !options.savegame.enabled) {
        logf("Resident: card state %u bytes at 0x%08x, SD fd %d (%s)\n", static_cast<unsigned>(sizeof(rt_fs_state)),
             fs_state_address, options.sdio_fd, options.sdio_sdhc ? "SDHC" : "SDSC");
    }
    if (options.savegame.enabled) {
        logf("Resident: savegame %s served from folder cluster %u, state %u bytes at 0x%08x, SD fd %d (%s)%s\n",
             options.savegame.prefix.c_str(), options.savegame.volume.dir_cluster,
             static_cast<unsigned>(sizeof(rt_fs_state)), fs_state_address, options.sdio_fd,
             options.sdio_sdhc ? "SDHC" : "SDSC", options.savegame.clone ? ", NAND save cloned in first" : "");
    }
    error.clear();
    return true;
}

}  // namespace riftwii::wii
