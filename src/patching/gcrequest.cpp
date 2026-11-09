// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcrequest.hpp"
#include "rthid.h"
#include "rtpadcal.h"

#include "riftwii/checkedalloc.hpp"
#include "riftwii/mempatch.hpp"

#include <cstring>

namespace riftwii {
namespace {

// The disc header bytes a plan replaces: 0x420 to 0x440 (gcoverlay.hpp).
constexpr std::uint32_t kPlanHeaderBytes = 0x20;

std::uint32_t piece_kind(GcPiece::Kind kind) {
    switch (kind) {
        case GcPiece::Kind::Image: return RWP_GC_PIECE_IMAGE;
        case GcPiece::Kind::Device: return RWP_GC_PIECE_DEVICE;
        case GcPiece::Kind::UsbDevice: return RWP_GC_PIECE_USB;
        case GcPiece::Kind::Zero: break;
    }
    return RWP_GC_PIECE_ZERO;
}

// A pointer and the size it is given with agree: both set, or both none.
bool sized(const void* pointer, std::uint32_t bytes) { return (pointer == nullptr) == (bytes == 0); }

// A memory patch's head in pack_memory (gc_memory_to_bytes): five words.
constexpr std::size_t kMemoryHeadWords = 5;
constexpr std::size_t kMemoryHeadBytes = kMemoryHeadWords * 4;
constexpr std::uint32_t kMemoryHasOffset = 1u << 0;
constexpr std::uint32_t kMemoryOcarina = 1u << 1;
constexpr std::uint32_t kMemorySearch = 1u << 2;

std::size_t padded_to_word(std::size_t bytes) { return (bytes + 3) & ~std::size_t(3); }

void append_word(std::vector<std::uint8_t>& out, std::uint32_t word) {
    std::uint8_t bytes[4];
    std::memcpy(bytes, &word, sizeof(bytes));
    out.insert(out.end(), bytes, bytes + sizeof(bytes));
}

void append_padded(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
    out.resize(out.size() + padded_to_word(bytes.size()) - bytes.size(), 0);
}

}  // namespace

void gc_plan_to_request(const GcOverlayPlan& plan, std::vector<rwp_gc_pack_entry>& entries, rwp_gc_launch& request) {
    entries.clear();
    entries.reserve(plan.entries.size());
    for (const GcOverlayPlan::Entry& entry : plan.entries) {
        rwp_gc_pack_entry flat{};
        flat.vstart = entry.vstart;
        flat.length = entry.length;
        flat.source = entry.piece.source;
        flat.kind = piece_kind(entry.piece.kind);
        flat.skip = entry.piece.skip;
        entries.push_back(flat);
    }
    request.pack_fst = plan.fst.data();
    request.pack_fst_bytes = static_cast<std::uint32_t>(plan.fst.size());
    request.pack_header = plan.header.data();
    request.pack_header_bytes = static_cast<std::uint32_t>(plan.header.size());
    request.pack_fst_offset = plan.fst_offset;
    request.pack_entries = entries.empty() ? nullptr : entries.data();
    request.pack_entry_count = static_cast<std::uint32_t>(entries.size());
}

bool gc_plan_from_request(const rwp_gc_launch& request, GcOverlayPlan& plan, std::string& why) {
    plan = GcOverlayPlan{};
    plan.fst.assign(request.pack_fst, request.pack_fst + request.pack_fst_bytes);
    plan.header.assign(request.pack_header, request.pack_header + request.pack_header_bytes);
    plan.fst_offset = request.pack_fst_offset;
    plan.entries.reserve(request.pack_entry_count);
    for (std::uint32_t i = 0; i < request.pack_entry_count; ++i) {
        const rwp_gc_pack_entry& flat = request.pack_entries[i];
        GcOverlayPlan::Entry entry;
        entry.vstart = flat.vstart;
        entry.length = flat.length;
        entry.piece.length = flat.length;
        entry.piece.source = flat.source;
        entry.piece.skip = flat.skip;
        if (flat.kind == RWP_GC_PIECE_IMAGE) {
            entry.piece.kind = GcPiece::Kind::Image;
        } else if (flat.kind == RWP_GC_PIECE_DEVICE) {
            entry.piece.kind = GcPiece::Kind::Device;
        } else if (flat.kind == RWP_GC_PIECE_USB) {
            entry.piece.kind = GcPiece::Kind::UsbDevice;
        } else if (flat.kind == RWP_GC_PIECE_ZERO) {
            entry.piece.kind = GcPiece::Kind::Zero;
        } else {
            why = "the packs' piece " + std::to_string(i) + " has an unknown kind " + std::to_string(flat.kind);
            return false;
        }
        plan.entries.push_back(entry);
    }
    return true;
}

bool gc_memory_to_bytes(const std::vector<MemoryPatch>& patches, std::vector<std::uint8_t>& out, std::string& why) {
    out.clear();
    for (std::size_t i = 0; i < patches.size(); ++i) {
        const MemoryPatch& patch = patches[i];
        if (!patch.valuefile.empty() && patch.value.empty()) {
            why = "memory patch " + std::to_string(i) + "'s file " + patch.valuefile + " was not read";
            return false;
        }
        if (patch.offset > 0xFFFFFFFFull || patch.align > 0xFFFFFFFFull) {
            why = "memory patch " + std::to_string(i) + " points past the GameCube's 32-bit addresses";
            return false;
        }
        std::uint32_t kind = 0;
        if (patch.has_offset) kind |= kMemoryHasOffset;
        if (patch.ocarina) kind |= kMemoryOcarina;
        if (patch.search) kind |= kMemorySearch;
        append_word(out, kind);
        append_word(out, static_cast<std::uint32_t>(patch.offset));
        append_word(out, static_cast<std::uint32_t>(patch.align));
        append_word(out, static_cast<std::uint32_t>(patch.value.size()));
        append_word(out, static_cast<std::uint32_t>(patch.original.size()));
        append_padded(out, patch.value);
        append_padded(out, patch.original);
    }
    return true;
}

bool gc_memory_from_request(const rwp_gc_launch& request, std::vector<MemoryPatch>& patches, std::string& why) {
    patches.clear();
    const std::uint8_t* bytes = request.pack_memory;
    const std::size_t total = request.pack_memory_bytes;
    std::size_t at = 0;
    const auto take = [&](std::size_t length, std::vector<std::uint8_t>& into) {
        const std::size_t padded = padded_to_word(length);
        if (padded < length || padded > total - at) return false;
        into.assign(bytes + at, bytes + at + length);
        at += padded;
        return true;
    };
    for (std::uint32_t i = 0; i < request.pack_memory_count; ++i) {
        if (total - at < kMemoryHeadBytes) {
            why = "the packs' memory patch " + std::to_string(i) + " is cut short";
            return false;
        }
        std::uint32_t head[kMemoryHeadWords];
        std::memcpy(head, bytes + at, kMemoryHeadBytes);
        at += kMemoryHeadBytes;
        const std::uint32_t kind = head[0];
        if ((kind & ~(kMemoryHasOffset | kMemoryOcarina | kMemorySearch)) != 0) {
            why = "the packs' memory patch " + std::to_string(i) + " has unknown kind bits";
            return false;
        }
        MemoryPatch patch;
        patch.has_offset = (kind & kMemoryHasOffset) != 0;
        patch.ocarina = (kind & kMemoryOcarina) != 0;
        patch.search = (kind & kMemorySearch) != 0;
        patch.offset = head[1];
        patch.align = head[2];
        if (!take(head[3], patch.value) || !take(head[4], patch.original)) {
            why = "the packs' memory patch " + std::to_string(i) + " runs past its bytes";
            return false;
        }
        patches.push_back(std::move(patch));
    }
    if (at != total) {
        why = "the packs' memory patches leave " + std::to_string(total - at) + " bytes over";
        return false;
    }
    return true;
}

bool gc_request_check(const rwp_gc_launch* request, std::uint32_t bytes, std::uint32_t crash_screen_bytes,
                      std::string& why) {
    if (request == nullptr || bytes < sizeof(rwp_gc_launch) || request->size != bytes) {
        why = "the launch request is not the size this plugin was built for (" + std::to_string(bytes) + " bytes, " +
              std::to_string(sizeof(rwp_gc_launch)) + " expected)";
        return false;
    }
    if (request->path == nullptr || request->id == nullptr || request->path[0] == '\0') {
        why = "the launch request names no game";
        return false;
    }
    if (request->error == nullptr || request->error_bytes == 0) {
        why = "the launch request has no room for a reason";
        return false;
    }
    // A real card as slot A leaves no room for an SD card there.
    const bool real_slot_a = request->real_card == RWP_GC_REAL_CARD_A || request->real_card == RWP_GC_REAL_CARD_B_AS_A;
    if (request->real_card > RWP_GC_REAL_CARD_B_AS_A || (real_slot_a && request->shared_card)) {
        why = "the launch request has conflicting memory card choices";
        return false;
    }
    if (request->volume_boost_db != 0 && request->volume_boost_db != 3 && request->volume_boost_db != 6) {
        why = "the launch request's volume boost of " + std::to_string(request->volume_boost_db) +
              " dB is not 0, 3 or 6";
        return false;
    }
    if (request->progressive && request->line240) {
        why = "the launch request asks for 480p and 240p at once";
        return false;
    }
    if (request->screen_offset_set && (request->screen_offset < -32 || request->screen_offset > 32)) {
        why = "the launch request's screen position " + std::to_string(request->screen_offset) +
              " is outside -32 to 32";
        return false;
    }
    if (request->external_card != nullptr) {
        const std::uint32_t bytes = request->external_card_bytes;
        if (real_slot_a || bytes < (512u << 10) || bytes > (16u << 20) || (bytes & (bytes - 1u)) != 0 ||
            request->card_fragment_count == 0 || request->card_fragment_count > 32 || !request->card_fragments ||
            (request->card_on_usb && request->usb_fd < 0)) {
            why = "the external memory card request is not whole";
            return false;
        }
        std::uint64_t sectors = 0;
        for (std::uint32_t i = 0; i < request->card_fragment_count; ++i) {
            const rwp_fragment& part = request->card_fragments[i];
            if (part.sector_count == 0 || part.sector > 0xFFFFFFFFull || part.sector_count > 0x100000000ull - part.sector) {
                why = "the external memory card has an unusable disk extent";
                return false;
            }
            sectors += part.sector_count;
        }
        if (sectors != bytes / 512u) {
            why = "the external memory card extents do not cover its bytes";
            return false;
        }
    } else if (request->external_card_bytes || request->card_fragment_count || request->card_on_usb) {
        why = "the external memory card request has no card";
        return false;
    }
    const bool has_packs = request->pack_fst != nullptr;
    if (has_packs) {
        const bool header_whole = request->pack_header != nullptr && request->pack_header_bytes == kPlanHeaderBytes;
        // The counts come from the DOL across the plugin line: bound them
        // before anything copies that many bytes or reads that many entries,
        // so a corrupt or mismatched DOL's huge counts are refused instead
        // of allocating gigabytes or reading past the request's buffers.
        // The entries are read as rwp_gc_pack_entry and kept as the plan's
        // larger GcOverlayPlan::Entry (gc_plan_from_request): the bound is
        // the larger of the two.
        constexpr std::size_t kEntryBytes = sizeof(GcOverlayPlan::Entry) > sizeof(rwp_gc_pack_entry)
                                                ? sizeof(GcOverlayPlan::Entry)
                                                : sizeof(rwp_gc_pack_entry);
        const bool sizes_bounded = within_checked_cap(request->pack_fst_bytes, 1) &&
                                   within_checked_cap(request->pack_entry_count, kEntryBytes);
        if (request->pack_fst_bytes == 0 || !header_whole || !sizes_bounded ||
            !sized(request->pack_entries, request->pack_entry_count)) {
            why = "the packs' plan in the launch request is not whole";
            return false;
        }
    } else if (request->pack_fst_bytes != 0 || request->pack_header != nullptr || request->pack_entry_count != 0) {
        why = "the launch request has parts of a packs' plan without its FST";
        return false;
    }
    // The memory patches' values are bounded as a launch's are
    // (kMaxPlanMemoryBytes), plus each patch's head and padding; every
    // patch takes at least its head, so the count is bounded by the bytes.
    const std::uint64_t memory_cap =
        std::uint64_t(kMaxPlanMemoryBytes) + std::uint64_t(request->pack_memory_count) * (kMemoryHeadBytes + 6);
    if (!sized(request->pack_memory, request->pack_memory_bytes) ||
        (request->pack_memory == nullptr) != (request->pack_memory_count == 0) ||
        request->pack_memory_bytes > memory_cap ||
        std::uint64_t(request->pack_memory_count) * kMemoryHeadBytes > request->pack_memory_bytes) {
        why = "the packs' memory patches in the launch request are not whole";
        return false;
    }
    if (!sized(request->hid_catalog, request->hid_catalog_bytes) ||
        (request->hid_catalog != nullptr && request->hid_catalog_bytes != sizeof(rthid_catalog))) {
        why = "the USB controller mappings have the wrong size";
        return false;
    }
    if (!sized(request->pad_calibration, request->pad_calibration_bytes) ||
        (request->pad_calibration != nullptr && request->pad_calibration_bytes != sizeof(rtpadcal_table))) {
        why = "the controllers' calibration has the wrong size";
        return false;
    }
    if (!sized(request->cheat_gct, request->cheat_gct_bytes) ||
        !sized(request->code_handler, request->code_handler_bytes)) {
        why = "the launch request's cheats or code handler have no bytes";
        return false;
    }
    if (request->crash_screen != nullptr && request->crash_screen_bytes != crash_screen_bytes) {
        why = "the launch request's crash screen is " + std::to_string(request->crash_screen_bytes) +
              " bytes, not " + std::to_string(crash_screen_bytes);
        return false;
    }
    // A program switch's arguments: whole strings, as many as said.
    if (request->exec_arg_count != 0) {
        std::uint32_t ends = 0;
        const bool sized_ok = request->exec_args != nullptr && request->exec_args_bytes <= 0x1000;
        for (std::uint32_t i = 0; sized_ok && i < request->exec_args_bytes; ++i)
            if (request->exec_args[i] == '\0') ++ends;
        if (!sized_ok || ends != request->exec_arg_count || request->exec_args[request->exec_args_bytes - 1] != '\0') {
            why = "the launch request's program arguments are not whole";
            return false;
        }
    }
    // A second disc: its pieces only with its path, and with as many as said.
    const bool has_disc2 = request->disc2_path != nullptr && request->disc2_path[0] != '\0';
    if (!sized(request->disc2_usb_fragments, request->disc2_usb_fragment_count) ||
        (!has_disc2 && (request->disc2_usb_fragment_count != 0 || request->disc2_usb_file_bytes != 0))) {
        why = "the launch request's second disc is not whole";
        return false;
    }
    if (request->way_back_kind > RWP_GC_WAY_BACK_HBC ||
        (request->way_back_kind != RWP_GC_WAY_BACK_NONE && request->way_back_view == nullptr)) {
        why = "the launch request's way back is not whole";
        return false;
    }
    return true;
}

}  // namespace riftwii
