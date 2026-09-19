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
constexpr std::uint32_t kBi2Field = 0x800000F4;
constexpr std::uint32_t kBi2Bytes = 0x2000;
constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;  // written by IOS at reload (Dolphin IOS.cpp, wiibrew)
constexpr unsigned kContinueScratchRegister = 12;         // see rt_entry.S
constexpr unsigned kStubScratchRegister = 0;

void store_words(std::uint32_t address, const std::uint32_t* words, std::size_t count) {
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(address);
    for (std::size_t i = 0; i < count; ++i) p[i] = words[i];
}

void sync_code(std::uint32_t address, std::size_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), static_cast<u32>(bytes));
    ICInvalidateRange(reinterpret_cast<void*>(address), static_cast<u32>(bytes));
}

}  // namespace

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

    // 2. The four instructions the stub displaces must be safe to replay.
    std::uint32_t displaced[4];
    for (unsigned i = 0; i < 4; ++i) {
        displaced[i] = *reinterpret_cast<const std::uint32_t*>(symbols.ioctl_async + i * 4);
        std::string why;
        if (!displaceable(displaced[i], kContinueScratchRegister, why)) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "instruction %u of IOS_IoctlAsync (0x%08x) cannot be displaced: ", i,
                          displaced[i]);
            error = buf + why;
            return false;
        }
    }

    // 3. Reserve the top of the MEM1 arena for the blob and the top of
    //    the MEM2 arena for the redirect payload and the bounce buffers.
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
    const std::uint32_t sdio_fd = options.sdio_fd < 0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(options.sdio_fd);
    std::vector<std::uint8_t> payload;
    if (has_table && !build_payload(options.pieces, 0, options.table_tag, sdio_fd, payload, error)) return false;
    // Bounce buffers whenever a run may be fetched (SD or DISC): anything
    // beyond plain MEM replacements.
    const bool has_disc = !options.pieces.disc.empty() || !options.pieces.entries.empty();
    const std::uint32_t bounce_bytes = has_sd || has_disc ? RT_MAX_PENDING * RT_BOUNCE_BYTES : 0;
    std::uint32_t arena1_hi = read32(kMem1ArenaHiField);
    if (arena1_hi == 0) arena1_hi = read32(kFstAddressField);
    const std::uint32_t bi2 = read32(kBi2Field);
    if (bi2 != 0 && bi2 < arena1_hi && arena1_hi - bi2 <= kBi2Bytes) arena1_hi = bi2;  // keep it whole
    const std::uint32_t arena2_end = read32(kMem2ArenaEndField);
    ResidentPlacement place;
    if (!plan_resident_placement(arena1_hi, options.mem1_floor, arena2_end, blob.size,
                                 static_cast<std::uint32_t>(payload.size()) + bounce_bytes, place, error)) {
        return false;
    }
    const std::uint32_t payload_address = place.data_base;
    if (has_table && !build_payload(options.pieces, payload_address, options.table_tag, sdio_fd, payload, error)) {
        return false;
    }
    const std::uint32_t bounce_address = payload_address + static_cast<std::uint32_t>(payload.size());

    // 4. Copy the blob and the payload, fill in the context and the two
    //    loader-patched slots.
    std::memcpy(reinterpret_cast<void*>(place.code_base), riftwii_rt_bin, blob.size);
    if (!payload.empty()) {
        std::memcpy(reinterpret_cast<void*>(payload_address), payload.data(), payload.size());
        DCFlushRange(reinterpret_cast<void*>(payload_address), static_cast<u32>(payload.size()));
    }
    rt_context* ctx = reinterpret_cast<rt_context*>(place.code_base + blob.context_offset);
    ctx->flags = options.gecko ? RT_FLAG_GECKO : 0;
    ctx->gecko_channel = 1;
    ctx->original_ioctl_async = symbols.ioctl_async;
    ctx->table = payload.empty() ? 0 : payload_address;
    ctx->complete_entry = place.code_base + blob.complete_di_offset;
    ctx->virtual_start_words = payload.empty() ? 0 : options.virtual_start_words;
    ctx->sdio_fd = sdio_fd;
    ctx->sdio_sdhc = options.sdio_sdhc ? 1 : 0;
    ctx->ioctlv_async = symbols.ioctlv_async;
    ctx->di_read_entry = place.code_base + blob.replay_ioctl_async_offset;  // the unhooked IOS_IoctlAsync
    for (std::uint32_t i = 0; i < RT_MAX_PENDING; ++i) {
        ctx->pending[i].bounce = bounce_bytes != 0 ? bounce_address + i * RT_BOUNCE_BYTES : 0;
    }
    store_words(place.code_base + blob.replay_ioctl_async_offset, displaced, 4);
    const auto resume = encode_absolute_jump(kContinueScratchRegister, symbols.ioctl_async + kHookStubBytes);
    store_words(place.code_base + blob.continue_ioctl_async_offset, resume.data(), 4);
    sync_code(place.code_base, blob.size);

    // 5. Divert the game's IOS_IoctlAsync to the trampoline.
    const auto stub = encode_absolute_jump(kStubScratchRegister, place.code_base + blob.hook_ioctl_async_offset);
    store_words(symbols.ioctl_async, stub.data(), 4);
    sync_code(symbols.ioctl_async, kHookStubBytes);

    out.code_base = place.code_base;
    out.code_bytes = place.code_bytes;
    out.old_arena1_hi = arena1_hi;
    out.new_arena1_hi = place.new_arena1_hi;
    out.data_base = place.data_base;
    out.data_bytes = place.data_bytes;
    out.old_arena2_end = arena2_end;
    out.new_arena2_end = place.new_arena2_end;
    out.ioctl_async = symbols.ioctl_async;
    out.ioctlv_async = symbols.ioctlv_async;
    out.table = ctx->table;
    out.payload_bytes = static_cast<std::uint32_t>(payload.size());
    out.bounce_bytes = bounce_bytes;
    logf("Resident: %u bytes at 0x%08x, MEM1 arena top 0x%08x -> 0x%08x, gecko %s\n", blob.size, place.code_base,
         arena1_hi, place.new_arena1_hi, options.gecko ? "on" : "off");
    if (place.data_bytes != 0) {
        logf("Resident: %u bytes of data at 0x%08x, MEM2 arena end 0x%08x -> 0x%08x\n", place.data_bytes,
             place.data_base, arena2_end, place.new_arena2_end);
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
    error.clear();
    return true;
}

}  // namespace riftwii::wii
