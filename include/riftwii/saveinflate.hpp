// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <zlib.h>
#include "riftwii/zipread.hpp"

namespace riftwii {

// Streaming raw deflate for downloaded saves, shared with the host tests.
// Only a fixed output chunk and zlib's bounded state/window are allocated.
class SaveZipInflater final : public ZipInflateStream {
public:
    explicit SaveZipInflater(std::function<bool()> cancelled = {}) : cancelled_(cancelled) {}
    ~SaveZipInflater();
    bool begin() override;
    Step feed(const std::uint8_t* input, std::size_t bytes, const ZipSink& sink) override;
    void end() override;

private:
    std::function<bool()> cancelled_;
    z_stream state_{};
    bool active_ = false;
    std::uint8_t chunk_[8192]{};
};

}  // namespace riftwii
