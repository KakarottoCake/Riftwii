// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual std::uint64_t size() const = 0;
    virtual bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const = 0;
};

struct OverlayExtent {
    std::uint64_t destination = 0;
    std::uint64_t source_offset = 0;
    std::uint64_t length = 0;
    const ByteSource* source = nullptr;
};

class ReadOverlay final : public ByteSource {
public:
    ReadOverlay(const ByteSource& original, std::uint64_t virtual_size);
    bool append(OverlayExtent extent, std::string& error);
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;
    // Extents in append order; later ones take precedence when reading.
    const std::vector<OverlayExtent>& extents() const { return extents_; }
    const ByteSource& original() const { return original_; }

private:
    // read() without its scratch buffer: `destination` may be partly
    // written on failure. Layers below a ReadOverlay are read this way, so
    // a stack of patches on one file costs one buffer, not one per layer.
    bool read_into(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const;

    const ByteSource& original_;
    const ReadOverlay* below_ = nullptr;  // original_, when it is a ReadOverlay too
    std::uint64_t size_;
    std::vector<OverlayExtent> extents_;
};

}
