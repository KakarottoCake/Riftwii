// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgptables.hpp"

#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// CTGP-R's package-disk crypto constants are four of its own PPC instruction
// words (store-word and blr opcodes), not keys anyone chose: the channel
// assembles them in place and uses them as the AES inputs. blob.bin's key is
// all four; its chunk IV reuses two of them around the chunk's first sector;
// jelly.bin's per-file key reuses the key's 2nd and 4th words around the file's
// timestamps. Kept as the opcode words so it is clear what they are.
constexpr std::uint32_t kBlobKeyWords[4] = {0x90830004u, 0x90a30008u, 0x90c3000cu, 0x4e800020u};
constexpr std::uint32_t kBlobIvWord0 = 0x80630004u;
constexpr std::uint32_t kBlobIvWord1 = 0x90830004u;
constexpr std::uint32_t kBlobIvWord3 = 0x4e800020u;

}  // namespace

std::array<std::uint8_t, 16> ctgp_blob_key() {
    std::array<std::uint8_t, 16> key{};
    for (int w = 0; w < 4; ++w) put_be32(key.data() + 4 * w, kBlobKeyWords[w]);
    return key;
}

CtgpChunkIv ctgp_blob_chunk_iv() {
    CtgpChunkIv iv;
    iv.w0 = kBlobIvWord0;
    iv.w1 = kBlobIvWord1;
    iv.w3 = kBlobIvWord3;
    return iv;
}

CtgpEfbKeyWords ctgp_jelly_key_words() {
    CtgpEfbKeyWords words;
    words.w1 = kBlobKeyWords[1];  // blob key word 1
    words.w3 = kBlobKeyWords[3];  // blob key word 3 (blr)
    return words;
}

bool ctgp_course_from_crs(const std::uint8_t* data, std::size_t size,
                          std::array<std::uint32_t, kCtgpCourseWords>& out, std::string& error) {
    // "CRS\0" then three header words; the entries start at 0x10.
    constexpr std::size_t kHeader = 0x10;
    if (data == nullptr || size < kHeader || std::memcmp(data, "CRS\0", 4) != 0) {
        error = "CTGP.CRS on the package disk is not a course table (it should start with CRS)";
        return false;
    }
    std::array<std::uint32_t, kCtgpCourseWords> course{};
    std::size_t at = kHeader;
    for (std::size_t i = 0; i < kCtgpCourseWords; ++i) {
        // Each entry: an index byte, the course-ID byte, a NUL-terminated name.
        if (at + 2 > size) {
            error = "CTGP.CRS ends before its 32 base tracks";
            return false;
        }
        const std::uint8_t course_id = data[at + 1];
        std::size_t name = at + 2;
        while (name < size && data[name] != 0) ++name;
        if (name >= size) {
            error = "CTGP.CRS has a course name that is not terminated";
            return false;
        }
        course[i] = course_id;
        at = name + 1;
    }
    out = course;
    return true;
}

bool ctgp_layout_from_cup(const std::uint8_t* data, std::size_t size, CtgpLayout& out, std::string& error) {
    // "CUP2", the group count, then count * 4 course-ID bytes.
    constexpr std::size_t kHeader = 8;
    if (data == nullptr || size < kHeader || std::memcmp(data, "CUP2", 4) != 0) {
        error = "a .cup file on the package disk is not a cup layout (it should start with CUP2)";
        return false;
    }
    const std::uint32_t groups = be32(data + 4);
    if (groups > kCtgpLayoutMaxGroups) {
        error = "a .cup file names more course groups than a layout holds";
        return false;
    }
    if (size < kHeader + std::size_t(groups) * 4) {
        error = "a .cup file ends before its course list";
        return false;
    }
    CtgpLayout layout;
    layout.groups = groups;
    std::memcpy(layout.tracks.data(), data + kHeader, std::size_t(groups) * 4);
    out = layout;
    return true;
}

}  // namespace riftwii
