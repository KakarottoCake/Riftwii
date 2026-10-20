// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcrequest.hpp"

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
    const bool has_packs = request->pack_fst != nullptr;
    if (has_packs) {
        const bool header_whole = request->pack_header != nullptr && request->pack_header_bytes == kPlanHeaderBytes;
        if (request->pack_fst_bytes == 0 || !header_whole ||
            !sized(request->pack_entries, request->pack_entry_count)) {
            why = "the packs' plan in the launch request is not whole";
            return false;
        }
    } else if (request->pack_fst_bytes != 0 || request->pack_header != nullptr || request->pack_entry_count != 0) {
        why = "the launch request has parts of a packs' plan without its FST";
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
    if (request->way_back_kind > RWP_GC_WAY_BACK_HBC ||
        (request->way_back_kind != RWP_GC_WAY_BACK_NONE && request->way_back_view == nullptr)) {
        why = "the launch request's way back is not whole";
        return false;
    }
    return true;
}

}  // namespace riftwii
