// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiimenucontent.hpp"

#include <cstdio>
#include <cstring>
#include <malloc.h>

#include "log.hpp"
#include "riftwii/checkedalloc.hpp"

namespace riftwii::wii {
namespace {

constexpr u64 kSystemMenu = 0x0000000100000002ull;

}  // namespace

MenuContentBuffer AllocateMenuContentBuffer(std::size_t size, std::string& why) {
    MenuContentBuffer buffer(nullptr, &std::free);
    if (size == 0 || size > kMaxMenuContentSize) {
        why = "the System Menu content is empty or exceeds the memory budget";
        return buffer;
    }
    const std::size_t aligned_size = (size + 31) & ~std::size_t(31);
    if (can_allocate(aligned_size)) buffer.reset(static_cast<u8*>(memalign(32, aligned_size)));
    if (!buffer) why = "no room for " + std::to_string(size) + " bytes";
    return buffer;
}

bool WiiMenuContentReader::Failed(const char* call, s32 code, std::string& why) const {
    why = std::string(call) + " failed (" + std::to_string(code) + ")";
    logf("%s: %s\n", log_name_, why.c_str());
    return false;
}

bool WiiMenuContentReader::Open(std::string& why) {
    view_count_ = 0;
    size_logged_.fill(false);
    metadata_ = {};
    u32 size = 0;
    s32 result = ES_GetStoredTMDSize(kSystemMenu, &size);
    if (result < 0) return Failed("ES_GetStoredTMDSize", result, why);
    if (size == 0 || size > 64 * 1024) {
        why = "the System Menu title metadata is empty or exceeds 64 KiB";
        return false;
    }
    MenuContentBuffer bytes = AllocateMenuContentBuffer(size, why);
    if (!bytes) return false;
    result = ES_GetStoredTMD(kSystemMenu, reinterpret_cast<signed_blob*>(bytes.get()), size);
    if (result < 0) return Failed("ES_GetStoredTMD", result, why);
    if (!parse_tmd_contents(bytes.get(), size, metadata_, why)) return false;
    if (metadata_.title_id != kSystemMenu || metadata_.contents.size() > 64) {
        why = "the System Menu title metadata is invalid";
        return false;
    }
    u32 count = 0;
    result = ES_GetNumTicketViews(kSystemMenu, &count);
    if (result < 0) return Failed("ES_GetNumTicketViews", result, why);
    if (count == 0 || count > 16) {
        why = "the System Menu has no ticket views or more than 16";
        return false;
    }
    result = ES_GetTicketViews(kSystemMenu, views_, count);
    if (result < 0) return Failed("ES_GetTicketViews", result, why);
    view_count_ = count;
    return true;
}

const TmdContent* WiiMenuContentReader::FindHash(const std::uint8_t* hash) const {
    for (const TmdContent& content : metadata_.contents) {
        if (std::memcmp(content.sha1.data(), hash, content.sha1.size()) == 0) return &content;
    }
    return nullptr;
}

void WiiMenuContentReader::LogContentSize(const TmdContent& content, std::size_t size) const {
    if (size == content.size) return;
    for (std::size_t i = 0; i < metadata_.contents.size(); ++i) {
        if (metadata_.contents[i].index != content.index) continue;
        if (size_logged_[i]) return;
        size_logged_[i] = true;
        break;
    }
    logf("%s: content %08x is %u bytes, not the %llu its title metadata gives "
         "(a Wii Menu theme?); it is read at its own size\n", log_name_,
         static_cast<unsigned>(content.id), static_cast<unsigned>(size),
         static_cast<unsigned long long>(content.size));
}

bool WiiMenuContentReader::ReadEsContent(s32 fd, const MenuContentAllocator& allocate,
                                         std::size_t& size, std::string& why) const {
    const s32 length = ES_SeekContent(fd, 0, SEEK_END);
    if (length < 0) return Failed("ES_SeekContent", length, why);
    if (length == 0 || static_cast<std::size_t>(length) > kMaxMenuContentSize) {
        why = "the System Menu content is empty or exceeds the memory budget";
        return false;
    }
    const s32 position = ES_SeekContent(fd, 0, SEEK_SET);
    if (position != 0) return Failed("ES_SeekContent", position, why);

    size = static_cast<std::size_t>(length);
    u8* buffer = allocate(size, why);
    if (!buffer) return false;
    const s32 got = ES_ReadContent(fd, buffer, static_cast<u32>(size));
    if (got != length) return Failed("ES_ReadContent", got, why);
    return true;
}

bool WiiMenuContentReader::Read(const TmdContent& content, const MenuContentAllocator& allocate,
                                std::size_t& size, std::string& why) const {
    size = 0;
    if (view_count_ == 0 || !allocate) {
        why = "the System Menu content reader or its allocator is unavailable";
        return false;
    }
    s32 fd = -1;
    for (u32 i = 0; i < view_count_; ++i) {
        // tikview is 216 bytes: later entries in the array are not aligned
        // for IOS. Each open gets its own aligned copy of the chosen view.
        alignas(32) tikview view = views_[i];
        fd = ES_OpenTitleContent(kSystemMenu, &view, content.index);
        if (fd >= 0) break;
        Failed("ES_OpenTitleContent", fd, why);
    }
    if (fd < 0) {
        // `why` keeps ES's refusal when the NAND fails too.
        std::string nand_why;
        if (ReadMenuContentFromNand(content, allocate, size, log_name_, nand_why, &permissions_)) {
            LogContentSize(content, size);
            logf("%s: read from the NAND, as ES refused it\n", log_name_);
            return true;
        }
        logf("%s: the NAND's copy could not be read either (%s)\n", log_name_, nand_why.c_str());
        return false;
    }
    const bool ok = ReadEsContent(fd, allocate, size, why);
    const s32 closed = ES_CloseContent(fd);
    if (closed < 0) return Failed("ES_CloseContent", closed, why);
    if (ok) LogContentSize(content, size);
    return ok;
}

}  // namespace riftwii::wii
