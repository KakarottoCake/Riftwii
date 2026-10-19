// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcoverlay.hpp"

#include <algorithm>

#include "riftwii/fst.hpp"
#include "rtable.h"

namespace riftwii {

namespace {

constexpr std::uint64_t kAlign = 0x8000;  // window slots start on a 32 KiB boundary, like the disc's own files

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void poke32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24);
    p[1] = std::uint8_t(v >> 16);
    p[2] = std::uint8_t(v >> 8);
    p[3] = std::uint8_t(v);
}
std::uint64_t round_up(std::uint64_t v, std::uint64_t a) { return (v + a - 1) / a * a; }

std::uint64_t pieces_size(const std::vector<GcPiece>& pieces) {
    std::uint64_t total = 0;
    for (const GcPiece& p : pieces) total += p.length;
    return total;
}

// Lays `pieces` out from `at`, then zeros up to a 32-byte multiple so a
// read the DVD driver rounds up never falls through to the image.
bool place(std::vector<GcOverlayPlan::Entry>& entries, std::uint64_t at, const std::vector<GcPiece>& pieces,
           std::string& error) {
    std::uint64_t cursor = at;
    for (const GcPiece& p : pieces) {
        if (p.length == 0) continue;
        if ((p.kind == GcPiece::Kind::Device || p.kind == GcPiece::Kind::UsbDevice) && p.skip >= RT_SECTOR_BYTES) {
            error = "a device piece starts past its sector";
            return false;
        }
        GcOverlayPlan::Entry e;
        e.vstart = cursor;
        e.length = p.length;
        e.piece = p;
        entries.push_back(e);
        cursor += p.length;
    }
    const std::uint64_t padded = round_up(cursor - at, 32) + at;
    if (padded > cursor) {
        GcOverlayPlan::Entry e;
        e.vstart = cursor;
        e.length = padded - cursor;
        entries.push_back(e);  // a Zero piece
    }
    return true;
}

}  // namespace

bool plan_gc_overlay(const std::vector<std::uint8_t>& boot, const std::vector<std::uint8_t>& fst_image,
                     std::uint64_t image_size, const std::vector<GcFile>& files, GcOverlayPlan& out,
                     std::string& error) {
    if (boot.size() < 0x440) {
        error = "the disc header is short";
        return false;
    }
    if (be32(boot.data() + 0x1C) != 0xC2339F3Du) {
        error = "not a GameCube disc";
        return false;
    }
    Fst fst;
    if (!Fst::parse(fst_image.data(), fst_image.size(), false, fst, error)) {
        error = "the disc's FST: " + error;
        return false;
    }

    // The window starts past everything the image holds.
    std::uint64_t end = image_size;
    const std::uint64_t fst_end = std::uint64_t(be32(boot.data() + 0x424)) + be32(boot.data() + 0x428);
    end = std::max(end, fst_end);
    for (const FstEntry& e : fst.entries())
        if (!e.is_directory) end = std::max(end, e.offset + e.size);
    std::uint64_t cursor = round_up(end, kAlign);

    GcOverlayPlan plan;

    // Give every file its slot; created files first go into the table so
    // the indices of existing ones are looked up afterwards.
    std::vector<std::uint64_t> slots(files.size());
    std::vector<FstNewFile> created;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const GcFile& f = files[i];
        const std::uint64_t size = pieces_size(f.pieces);
        if (size > 0xFFFFFFFFull) {
            error = "'" + f.disc_path + "' is too large for the FST";
            return false;
        }
        const std::uint32_t index = fst.find(f.disc_path, true);
        if (f.create && index != Fst::npos) {
            error = "'" + f.disc_path + "' already exists";
            return false;
        }
        if (!f.create && (index == Fst::npos || fst.entries()[index].is_directory)) {
            error = "no such disc file '" + f.disc_path + "'";
            return false;
        }
        slots[i] = cursor;
        if (!place(plan.entries, cursor, f.pieces, error)) return false;
        cursor = round_up(cursor + size, kAlign);
        if (cursor > kGcDiscLimit) {
            error = "the mod's files do not fit below 4 GiB";
            return false;
        }
        if (f.create) created.push_back(FstNewFile{f.disc_path, slots[i], std::uint32_t(size)});
    }
    if (!created.empty()) {
        std::vector<std::uint32_t> indices;
        if (!fst.create_files(created, indices, error)) return false;
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (files[i].create) continue;
        const std::uint32_t index = fst.find(files[i].disc_path, true);
        if (!fst.set_file_extent(index, slots[i], std::uint32_t(pieces_size(files[i].pieces)), error)) return false;
    }

    if (!fst.serialize(plan.fst, error)) return false;
    plan.fst.resize(round_up(plan.fst.size(), 32), 0);
    plan.fst_offset = cursor;
    if (plan.fst_offset + plan.fst.size() > kGcDiscLimit) {
        error = "the new FST does not fit below 4 GiB";
        return false;
    }

    // The header points at the new FST. Its maximum size is what the
    // apploader reserves (the largest of a game's discs), so it only grows.
    plan.header.assign(boot.begin() + 0x420, boot.begin() + 0x440);
    poke32(plan.header.data() + 4, std::uint32_t(plan.fst_offset));
    poke32(plan.header.data() + 8, std::uint32_t(plan.fst.size()));
    if (be32(plan.header.data() + 12) < plan.fst.size()) poke32(plan.header.data() + 12, std::uint32_t(plan.fst.size()));

    out = std::move(plan);
    error.clear();
    return true;
}

namespace {

// gc_disc_runs and gc_disc_runs_rvz: `image` is the image file's fragments
// on the card (or the USB drive, `image_kind` Usb), or null for an image
// read from its RVZ.
bool disc_runs(const std::vector<Fragment>* image, std::uint64_t image_size, const GcOverlayPlan* plan,
               std::vector<GcRun>& out, std::string& error, GcRun::Kind image_kind = GcRun::Kind::Sd) {
    out.clear();
    std::uint64_t at = 0;
    // Bytes of the image at `disc`: from its file's fragments, or by their
    // offset in the image.
    auto from_image = [&](std::uint64_t disc, std::uint64_t source, std::uint64_t length) {
        if (image == nullptr) {
            if (source > image_size || length > image_size - source) {
                error = "a piece lies past the image's end";
                return false;
            }
            GcRun r;
            r.kind = GcRun::Kind::Image;
            r.disc = disc;
            r.length = length;
            r.image_offset = source;
            out.push_back(r);
            return true;
        }
        std::vector<PlacedRun> placed;
        if (!place_on_fragments(*image, source, length, placed, error)) return false;
        for (const PlacedRun& p : placed) {
            GcRun r;
            r.kind = image_kind;
            r.disc = disc;
            r.length = p.length;
            r.sector = p.source;
            r.skip = p.skip;
            out.push_back(r);
            disc += p.length;
        }
        return true;
    };
    auto zeros_to = [&](std::uint64_t end) {
        if (end <= at) return;
        GcRun r;
        r.disc = at;
        r.length = end - at;
        out.push_back(r);
        at = end;
    };
    if (image_size != 0 && !from_image(0, 0, image_size)) return false;
    at = image_size;
    if (!plan) return true;

    // The window: each file's pieces, the gaps between files as zeros, and
    // the FST last (the plan puts it after every file).
    for (const GcOverlayPlan::Entry& e : plan->entries) {
        if (e.vstart < at) {
            error = "the mod's files overlap the image or each other";
            return false;
        }
        zeros_to(e.vstart);
        if (e.piece.kind == GcPiece::Kind::Image) {
            if (!from_image(e.vstart, e.piece.source, e.length)) return false;
        } else if (e.piece.kind == GcPiece::Kind::Device || e.piece.kind == GcPiece::Kind::UsbDevice) {
            GcRun r;
            r.kind = e.piece.kind == GcPiece::Kind::UsbDevice ? GcRun::Kind::Usb : GcRun::Kind::Sd;
            r.disc = e.vstart;
            r.length = e.length;
            r.sector = e.piece.source;
            r.skip = e.piece.skip;
            out.push_back(r);
        } else {
            GcRun r;
            r.disc = e.vstart;
            r.length = e.length;
            out.push_back(r);
        }
        at = e.vstart + e.length;
    }
    if (plan->fst_offset < at) {
        error = "the new FST overlaps the mod's files";
        return false;
    }
    zeros_to(plan->fst_offset);
    GcRun fst;
    fst.kind = GcRun::Kind::Fst;
    fst.disc = plan->fst_offset;
    fst.length = plan->fst.size();
    out.push_back(fst);
    if (plan->fst_offset + plan->fst.size() > kGcDiscLimit) {
        error = "the mod's files do not fit below 4 GiB";
        return false;
    }
    return true;
}

}  // namespace

bool gc_plan_uses_usb(const GcOverlayPlan& plan) {
    for (const GcOverlayPlan::Entry& e : plan.entries)
        if (e.piece.kind == GcPiece::Kind::UsbDevice) return true;
    return false;
}

bool gc_disc_runs(const std::vector<Fragment>& image, std::uint64_t image_size, const GcOverlayPlan* plan,
                  std::vector<GcRun>& out, std::string& error, bool image_on_usb) {
    return disc_runs(&image, image_size, plan, out, error, image_on_usb ? GcRun::Kind::Usb : GcRun::Kind::Sd);
}

bool gc_disc_runs_rvz(std::uint64_t image_size, const GcOverlayPlan* plan, std::vector<GcRun>& out,
                      std::string& error) {
    return disc_runs(nullptr, image_size, plan, out, error);
}

}  // namespace riftwii
