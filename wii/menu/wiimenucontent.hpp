// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <memory>
#include <ogc/es.h>

#include "riftwii/nandchannel.hpp"
#include "nandpermissions.hpp"

namespace riftwii::wii {

using MenuContentBuffer = std::unique_ptr<u8, decltype(&std::free)>;
MenuContentBuffer AllocateMenuContentBuffer(std::size_t size, std::string& why);

// ES resolves both private and shared contents by their TMD index. No IOS
// permission patch, title identity change or direct NAND handle is needed,
// where ES allows it: when it refuses the content (-1026 for every view on
// a Wii and a vWii started from the RiftWii channel, 2610-810), Read takes
// the NAND's own copy (ReadMenuContentFromNand).
class WiiMenuContentReader {
public:
    explicit WiiMenuContentReader(const char* log_name) : log_name_(log_name), permissions_(log_name, false) {}
    bool Open(std::string& why);
    const TmdContents& Metadata() const { return metadata_; }
    const TmdContent* FindHash(const std::uint8_t* hash) const;
    // The caller supplies a 32-byte aligned buffer of content.size bytes.
    bool Read(const TmdContent& content, void* buffer, std::size_t limit, std::string& why) const;

private:
    bool Failed(const char* call, s32 code, std::string& why) const;
    const char* log_name_;
    mutable NandPermissionSession permissions_;
    TmdContents metadata_;
    alignas(32) tikview views_[16]{};
    u32 view_count_ = 0;
};

// `content` (content.size bytes into a 32-byte aligned buffer) read from
// its file on the NAND: a shared one by the name content.map gives its
// hash, any other from the System Menu's title folder. Opens IOS's NAND
// permission check before the first file, which needs hardware access.
// A reader supplies its session to keep it open across all its contents.
bool ReadMenuContentFromNand(const TmdContent& content, void* buffer, const char* who, std::string& why,
                             NandPermissionSession* permissions = nullptr);

}  // namespace riftwii::wii
