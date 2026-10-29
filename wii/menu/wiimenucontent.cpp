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
constexpr std::size_t kMaxContent = 16 * 1024 * 1024;

}  // namespace

MenuContentBuffer AllocateMenuContentBuffer(std::size_t size, std::string& why) {
    MenuContentBuffer buffer(nullptr, &std::free);
    if (size == 0 || size > kMaxContent) {
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

bool WiiMenuContentReader::Read(const TmdContent& content, void* buffer, std::size_t limit,
                                std::string& why) const {
    if (view_count_ == 0 || !buffer || content.size == 0 || content.size > limit || content.size > kMaxContent) {
        why = "the System Menu content is empty, unavailable or exceeds the memory budget";
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
    if (fd < 0) return false;
    const s32 length = ES_SeekContent(fd, 0, SEEK_END);
    bool ok = false;
    if (length < 0) {
        Failed("ES_SeekContent", length, why);
    } else if (static_cast<std::uint64_t>(length) != content.size) {
        why = "the System Menu content size differs from its title metadata";
    } else {
        const s32 position = ES_SeekContent(fd, 0, SEEK_SET);
        if (position != 0) {
            Failed("ES_SeekContent", position, why);
        } else {
            const s32 got = ES_ReadContent(fd, buffer, static_cast<u32>(content.size));
            if (got != length) {
                Failed("ES_ReadContent", got, why);
            } else {
                ok = true;
            }
        }
    }
    const s32 closed = ES_CloseContent(fd);
    if (closed < 0) return Failed("ES_CloseContent", closed, why);
    return ok;
}

}  // namespace riftwii::wii
