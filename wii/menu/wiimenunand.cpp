// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiimenucontent.hpp"

#include <gccore.h>

#include <cstdio>

#include "nandpermissions.hpp"
#include "nanduse.hpp"
#include "riftwii/sysfont.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxContentMap = 64 * 1024;  // 28 bytes a shared content

// The System Menu's contents are IOS's own files: an app is refused them
// (-102) until IOS's NAND permission check is opened (nandpermissions.hpp),
// which needs hardware access. Opened only when refused, and closed again
// once the file is read, whichever way this returns.
class NandFile {
public:
    NandFile(const char* path, const char* who) : who_(who) {
        fd_ = ISFS_Open(path, ISFS_OPEN_READ);
        if (fd_ == -102 && open_nand_permissions(who)) {
            opened_permissions_ = true;
            fd_ = ISFS_Open(path, ISFS_OPEN_READ);
        }
    }
    ~NandFile() {
        if (fd_ >= 0) ISFS_Close(fd_);
        if (opened_permissions_) close_nand_permissions(who_);
    }
    NandFile(const NandFile&) = delete;
    NandFile& operator=(const NandFile&) = delete;

    s32 fd() const { return fd_; }

    // The file's length, or 0 when IOS cannot say.
    std::size_t Length() const {
        static fstats stats ATTRIBUTE_ALIGN(32);
        return ISFS_GetFileStats(fd_, &stats) < 0 ? 0 : stats.file_length;
    }

private:
    const char* who_;
    s32 fd_ = -1;
    bool opened_permissions_ = false;
};

// Where the NAND keeps `content`: a shared one under the name content.map
// gives its hash, any other under the System Menu's own title.
bool ContentPath(const TmdContent& content, char* path, std::size_t room, const char* who, std::string& why) {
    constexpr std::uint16_t kShared = 0x8000;
    if ((content.type & kShared) == 0) {
        std::snprintf(path, room, "/title/00000001/00000002/content/%08x.app", static_cast<unsigned>(content.id));
        return true;
    }
    NandFile map("/shared1/content.map", who);
    if (map.fd() < 0) {
        why = "/shared1/content.map could not be opened (" + std::to_string(map.fd()) + ")";
        return false;
    }
    const std::size_t size = map.Length();
    if (size == 0 || size > kMaxContentMap) {
        why = "/shared1/content.map has no size we can read";
        return false;
    }
    MenuContentBuffer bytes = AllocateMenuContentBuffer(size, why);
    if (!bytes) return false;
    const s32 got = ISFS_Read(map.fd(), bytes.get(), static_cast<u32>(size));
    if (got != static_cast<s32>(size)) {
        why = "/shared1/content.map read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    const std::string name = shared_content_name(bytes.get(), size, content.sha1.data());
    if (name.empty()) {
        why = "content.map does not list the content";
        return false;
    }
    std::snprintf(path, room, "/shared1/%s.app", name.c_str());
    return true;
}

}  // namespace

bool ReadMenuContentFromNand(const TmdContent& content, void* buffer, const char* who, std::string& why) {
    if (NandOpen() < 0) {
        why = "the NAND could not be opened";
        return false;
    }
    struct NandUse {
        ~NandUse() { NandClose(); }
    } nand_use;
    char path[64];
    if (!ContentPath(content, path, sizeof path, who, why)) return false;
    NandFile file(path, who);
    if (file.fd() < 0) {
        why = std::string(path) + " could not be opened (" + std::to_string(file.fd()) + ")";
        return false;
    }
    if (file.Length() != content.size) {
        why = std::string(path) + " is not the size its title metadata gives";
        return false;
    }
    const s32 got = ISFS_Read(file.fd(), buffer, static_cast<u32>(content.size));
    if (got != static_cast<s32>(content.size)) {
        why = std::string(path) + " read " + std::to_string(got) + " of " + std::to_string(content.size) + " bytes";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii
