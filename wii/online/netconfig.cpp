// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netconfig.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/isfs.h>

#include <cstdlib>

#include "nanduse.hpp"
#include "riftwii/dnsquery.hpp"

namespace riftwii::wii {

bool ReadSavedNetConfig(std::vector<std::uint8_t>& config, std::string& why) {
    config.clear();
    if (NandOpen() < 0) {
        why = "the NAND could not be opened";
        return false;
    }
    struct NandUse {
        ~NandUse() { NandClose(); }
    } nand_use;

    const s32 file = ISFS_Open(kNetConfigPath, ISFS_OPEN_READ);
    if (file < 0) {
        why = std::string(kNetConfigPath) + " could not be opened (" + std::to_string(file) + ")";
        return false;
    }
    // IOS writes into this buffer, so it needs the 32-byte alignment and
    // its own length rounded up to one (a stack or static buffer would be
    // shared between threads, and this runs from the network's jobs).
    constexpr std::size_t kAlignedSize = (kNetConfigSize + 31) & ~std::size_t(31);
    void* buffer = memalign(32, kAlignedSize);
    if (buffer == nullptr) {
        ISFS_Close(file);
        why = "no memory to read the network settings";
        return false;
    }
    const s32 got = ISFS_Read(file, buffer, static_cast<u32>(kNetConfigSize));
    ISFS_Close(file);
    if (got != static_cast<s32>(kNetConfigSize)) {
        free(buffer);
        why = std::string(kNetConfigPath) + " gave " + std::to_string(got) + " of " +
              std::to_string(kNetConfigSize) + " bytes";
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    config.assign(bytes, bytes + kNetConfigSize);
    free(buffer);
    return true;
}

}  // namespace riftwii::wii
