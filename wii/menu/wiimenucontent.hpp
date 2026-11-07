// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdlib>
#include <functional>
#include <memory>
#include <ogc/es.h>

#include "riftwii/nandchannel.hpp"
#include "nandpermissions.hpp"

namespace riftwii::wii {

using MenuContentBuffer = std::unique_ptr<u8, decltype(&std::free)>;
MenuContentBuffer AllocateMenuContentBuffer(std::size_t size, std::string& why);
constexpr std::size_t kMaxMenuContentSize = 16 * 1024 * 1024;
using MenuContentAllocator = std::function<u8*(std::size_t size, std::string& why)>;

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
    // Measures the open content and refuses an empty file or one over 16 MiB
    // before calling allocate once. The allocator owns the buffer (including
    // on a failed read) and must return 32-byte aligned memory. size receives
    // the accepted length before allocation, or zero if none was accepted.
    bool Read(const TmdContent& content, const MenuContentAllocator& allocate,
              std::size_t& size, std::string& why) const;

private:
    bool Failed(const char* call, s32 code, std::string& why) const;
    bool ReadEsContent(s32 fd, const MenuContentAllocator& allocate,
                       std::size_t& size, std::string& why) const;
    void LogContentSize(const TmdContent& content, std::size_t size) const;
    const char* log_name_;
    mutable NandPermissionSession permissions_;
    TmdContents metadata_;
    alignas(32) tikview views_[16]{};
    u32 view_count_ = 0;
    mutable std::array<bool, 64> size_logged_{};
};

// Reads the content at its file's own length, using the same allocator and
// size rules as Read above: a shared one by the name content.map gives its
// hash, any other from the System Menu's title folder. Opens IOS's NAND
// permission check before the first file, which needs hardware access.
// A reader supplies its session to keep it open across all its contents.
bool ReadMenuContentFromNand(const TmdContent& content, const MenuContentAllocator& allocate,
                             std::size_t& size, const char* who, std::string& why,
                             NandPermissionSession* permissions = nullptr);

}  // namespace riftwii::wii
