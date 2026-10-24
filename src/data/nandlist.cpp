// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/nandlist.hpp"

#include "riftwii/checkedalloc.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace riftwii {

bool nand_list_bytes(std::uint32_t count, std::size_t& bytes) {
    // Zero names need no buffer; more than the whole file system is damage.
    if (count == 0 || count > kNandMaxEntries) return false;
    const std::size_t need = static_cast<std::size_t>(count) * kNandNameBytes;
    // ISFS buffers are 32-byte aligned and sized.
    bytes = (need + 31) & ~static_cast<std::size_t>(31);
    return true;
}

bool parse_nand_list(const char* list, std::size_t list_bytes, std::uint32_t asked, std::uint32_t got,
                     std::vector<std::string>& names) {
    names.clear();
    // The second read must not report more than the buffer was sized for.
    if (got > asked) return false;
    if (got == 0) return true;
    if (list == nullptr) return false;
    std::size_t at = 0;
    for (std::uint32_t i = 0; i < got; ++i) {
        // Each entry ends with a NUL within kNandNameMax characters.
        std::size_t len = 0;
        for (;;) {
            if (at + len >= list_bytes) {
                names.clear();
                return false;
            }
            if (list[at + len] == '\0') break;
            ++len;
            if (len > kNandNameMax) {
                names.clear();
                return false;
            }
        }
        if (len != 0) {
            if (!try_push_back(names, std::string(list + at, len))) {
                names.clear();
                return false;
            }
        }
        at += len + 1;
    }
    return true;
}

NandList list_nand_with(NandReadDir read_dir, void* ctx, NandAlloc alloc, NandFree release,
                        std::vector<std::string>& names, std::uint32_t& count) {
    names.clear();
    count = 0;
    if (read_dir(ctx, nullptr, &count) < 0) {
        count = 0;
        return NandList::NotThere;
    }
    const std::uint32_t asked = count;
    if (asked == 0) {
        count = 0;
        return NandList::Ok;
    }
    std::size_t bytes = 0;
    if (!nand_list_bytes(asked, bytes)) {
        count = asked;
        return NandList::Damaged;
    }
    void* block = alloc(bytes);
    if (block == nullptr) {
        count = asked;
        return NandList::NoRoom;
    }
    char* list = static_cast<char*>(block);
    std::memset(list, 0, bytes);
    std::uint32_t got = asked;
    if (read_dir(ctx, list, &got) < 0) {
        release(block);
        count = asked;
        return NandList::Damaged;
    }
    if (!parse_nand_list(list, bytes, asked, got, names)) {
        release(block);
        count = asked;
        return NandList::Damaged;
    }
    release(block);
    count = asked;
    return NandList::Ok;
}

}  // namespace riftwii
