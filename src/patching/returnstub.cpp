// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/returnstub.hpp"

#include <algorithm>

#include "riftwii/dol.hpp"

namespace riftwii {
namespace {

std::uint32_t read_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

void write_be32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

bool overlaps_stub(std::uint32_t start, std::uint32_t size) {
    if (size == 0) return false;
    const std::uint64_t end = static_cast<std::uint64_t>(start) + size;
    return start < RTSTUB_LIMIT && end > RTSTUB_BASE;
}

}  // namespace

std::vector<std::uint8_t> fill_return_stub(const std::uint8_t* image, std::size_t size,
                                           const std::vector<ReturnStubTitle>& titles, std::string& error) {
    if (image == nullptr || size < RTSTUB_OFFSET_CODE + 4) {
        error = "the return stub is missing or too short";
        return {};
    }
    if (return_stub_kind(image) != ReturnStubKind::RiftWii || read_be32(image + RTSTUB_OFFSET_VERSION) != RTSTUB_VERSION) {
        error = "the return stub is not the one this RiftWii expects";
        return {};
    }
    // The size the stub was built with: a different one means the image
    // was cut short or came from another build.
    if (read_be32(image + RTSTUB_OFFSET_SIZE) != size || RTSTUB_BASE + size > RTSTUB_LIMIT) {
        error = "the return stub's size does not match its header";
        return {};
    }
    if (titles.empty() || titles.size() > RTSTUB_MAX_TITLES) {
        error = "the return stub takes 1 to " + std::to_string(RTSTUB_MAX_TITLES) + " titles";
        return {};
    }
    std::vector<std::uint8_t> filled(image, image + size);
    write_be32(filled.data() + RTSTUB_OFFSET_COUNT, static_cast<std::uint32_t>(titles.size()));
    write_be32(filled.data() + RTSTUB_OFFSET_RESULT, 0);
    for (std::size_t i = 0; i < RTSTUB_MAX_TITLES; ++i) {
        std::uint8_t* slot = filled.data() + RTSTUB_OFFSET_TITLES + i * RTSTUB_TITLE_BYTES;
        std::fill(slot, slot + RTSTUB_TITLE_BYTES, 0);
        if (i >= titles.size()) continue;
        write_be32(slot, static_cast<std::uint32_t>(titles[i].title >> 32));
        write_be32(slot + 4, static_cast<std::uint32_t>(titles[i].title));
        std::copy(titles[i].view.begin(), titles[i].view.end(), slot + RTSTUB_TITLE_VIEW);
    }
    return filled;
}

ReturnStubKind return_stub_kind(const std::uint8_t* first_bytes) {
    if (first_bytes == nullptr) return ReturnStubKind::None;
    if (read_be32(first_bytes + RTSTUB_OFFSET_HBC_MAGIC) != RTSTUB_HBC_MAGIC_STUB ||
        read_be32(first_bytes + RTSTUB_OFFSET_HBC_MAGIC + 4) != RTSTUB_HBC_MAGIC_HAXX) {
        return ReturnStubKind::None;
    }
    return read_be32(first_bytes + RTSTUB_OFFSET_MAGIC) == RTSTUB_MAGIC ? ReturnStubKind::RiftWii
                                                                        : ReturnStubKind::HomebrewChannel;
}

bool dol_clear_of_return_stub(const std::uint8_t* dol, std::size_t size) {
    DolHeader header;
    std::string error;
    if (dol == nullptr || !parse_dol_header(dol, size, header, error)) return false;
    for (const DolSection& section : header.sections) {
        if (overlaps_stub(section.address, section.size)) return false;
    }
    return !overlaps_stub(header.bss_address, header.bss_size);
}

}  // namespace riftwii
