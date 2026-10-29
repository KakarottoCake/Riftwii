// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <memory>
#include <ogc/es.h>

#include "riftwii/nandchannel.hpp"

namespace riftwii::wii {

using MenuContentBuffer = std::unique_ptr<u8, decltype(&std::free)>;
MenuContentBuffer AllocateMenuContentBuffer(std::size_t size, std::string& why);

// ES resolves both private and shared contents by their TMD index. No IOS
// permission patch, title identity change or direct NAND handle is needed.
class WiiMenuContentReader {
public:
    explicit WiiMenuContentReader(const char* log_name) : log_name_(log_name) {}
    bool Open(std::string& why);
    const TmdContents& Metadata() const { return metadata_; }
    const TmdContent* FindHash(const std::uint8_t* hash) const;
    // The caller supplies a 32-byte aligned buffer of content.size bytes.
    bool Read(const TmdContent& content, void* buffer, std::size_t limit, std::string& why) const;

private:
    bool Failed(const char* call, s32 code, std::string& why) const;
    const char* log_name_;
    TmdContents metadata_;
    alignas(32) tikview views_[16]{};
    u32 view_count_ = 0;
};

}  // namespace riftwii::wii
