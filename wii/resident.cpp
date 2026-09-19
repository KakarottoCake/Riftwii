// SPDX-License-Identifier: GPL-3.0-or-later
#include "resident.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>

#include <cstring>
#include <vector>

#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_rt_bin.h"
#include "rt_hook.h"

namespace riftwii::wii {
namespace {

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

    // 3. Reserve the top of the MEM2 arena: blob, the redirect payload,
    //    then the SD bounce buffers.
    const bool has_table = !options.replacements.empty() || !options.sd_replacements.empty();
    const bool has_sd = !options.sd_replacements.empty();
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
    if (has_table && !build_payload(options.replacements, options.sd_replacements, 0, options.table_tag, sdio_fd,
                                    payload, error)) {
        return false;
    }
    const std::uint32_t bounce_bytes = has_sd ? RT_MAX_PENDING * RT_BOUNCE_BYTES : 0;
    const std::uint32_t arena_end = read32(kMem2ArenaEndField);
    ResidentPlacement place;
    if (!plan_resident_placement(arena_end, blob.size, static_cast<std::uint32_t>(payload.size()) + bounce_bytes,
                                 place, error)) {
        return false;
    }
    const std::uint32_t payload_address = place.base + blob.size;  // blob sizes are multiples of 32
    if (has_table && !build_payload(options.replacements, options.sd_replacements, payload_address,
                                    options.table_tag, sdio_fd, payload, error)) {
        return false;
    }
    const std::uint32_t bounce_address = payload_address + static_cast<std::uint32_t>(payload.size());

    // 4. Copy the blob and the payload, fill in the context and the two
    //    loader-patched slots.
    std::memcpy(reinterpret_cast<void*>(place.base), riftwii_rt_bin, blob.size);
    if (!payload.empty()) {
        std::memcpy(reinterpret_cast<void*>(payload_address), payload.data(), payload.size());
        DCFlushRange(reinterpret_cast<void*>(payload_address), static_cast<u32>(payload.size()));
    }
    rt_context* ctx = reinterpret_cast<rt_context*>(place.base + blob.context_offset);
    ctx->flags = options.gecko ? RT_FLAG_GECKO : 0;
    ctx->gecko_channel = 1;
    ctx->original_ioctl_async = symbols.ioctl_async;
    ctx->table = payload.empty() ? 0 : payload_address;
    ctx->complete_entry = place.base + blob.complete_di_offset;
    ctx->virtual_start_words = payload.empty() ? 0 : options.virtual_start_words;
    ctx->sdio_fd = sdio_fd;
    ctx->sdio_sdhc = options.sdio_sdhc ? 1 : 0;
    ctx->ioctlv_async = symbols.ioctlv_async;
    for (std::uint32_t i = 0; i < RT_MAX_PENDING; ++i) {
        ctx->pending[i].bounce = has_sd ? bounce_address + i * RT_BOUNCE_BYTES : 0;
    }
    store_words(place.base + blob.replay_ioctl_async_offset, displaced, 4);
    const auto resume = encode_absolute_jump(kContinueScratchRegister, symbols.ioctl_async + kHookStubBytes);
    store_words(place.base + blob.continue_ioctl_async_offset, resume.data(), 4);
    sync_code(place.base, blob.size);

    // 5. Divert the game's IOS_IoctlAsync to the trampoline.
    const auto stub = encode_absolute_jump(kStubScratchRegister, place.base + blob.hook_ioctl_async_offset);
    store_words(symbols.ioctl_async, stub.data(), 4);
    sync_code(symbols.ioctl_async, kHookStubBytes);

    out.base = place.base;
    out.reserved_bytes = place.reserved_bytes;
    out.old_arena_end = arena_end;
    out.new_arena_end = place.new_arena_end;
    out.ioctl_async = symbols.ioctl_async;
    out.ioctlv_async = symbols.ioctlv_async;
    out.table = ctx->table;
    out.payload_bytes = static_cast<std::uint32_t>(payload.size());
    out.bounce_bytes = bounce_bytes;
    logf("Resident: %u bytes at 0x%08x, MEM2 arena end 0x%08x -> 0x%08x, gecko %s\n", blob.size, place.base,
         arena_end, place.new_arena_end, options.gecko ? "on" : "off");
    if (!payload.empty()) {
        logf("Resident: redirect table at 0x%08x, %u MEM + %u SD replacement(s), payload %u bytes, virtual window from word 0x%08x\n",
             ctx->table, static_cast<unsigned>(options.replacements.size()),
             static_cast<unsigned>(options.sd_replacements.size()), static_cast<unsigned>(payload.size()),
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
