// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/bnr.hpp"

#include <cstring>

#include "riftwii/disc.hpp"
#include "riftwii/fst.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool same_path(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

// "/./arc//x" and "arc/x" both as "/arc/x".
std::string normal_path(const std::string& p) {
    std::string out;
    std::size_t i = 0;
    while (i <= p.size()) {
        const std::size_t j = p.find('/', i);
        const std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!part.empty() && part != ".") out += "/" + part;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out.empty() ? "/" : out;
}

void append_utf8(std::string& s, std::uint32_t c) {
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Big-endian UTF-16, up to `chars` or a zero.
std::string utf16be(const std::uint8_t* p, std::size_t chars) {
    std::string s;
    for (std::size_t i = 0; i < chars; ++i) {
        std::uint32_t c = (static_cast<std::uint32_t>(p[2 * i]) << 8) | p[2 * i + 1];
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < chars) {
            const std::uint32_t lo = (static_cast<std::uint32_t>(p[2 * i + 2]) << 8) | p[2 * i + 3];
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        append_utf8(s, c);
    }
    return s;
}

}  // namespace

bool lz77_decompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::string& error,
                     std::size_t max_out) {
    if (!data || size < 8 || std::memcmp(data, "LZ77", 4) != 0) {
        error = "not LZ77 data";
        return false;
    }
    const std::uint32_t word = static_cast<std::uint32_t>(data[4]) | (static_cast<std::uint32_t>(data[5]) << 8) |
                               (static_cast<std::uint32_t>(data[6]) << 16) | (static_cast<std::uint32_t>(data[7]) << 24);
    if ((word & 0xFF) != 0x10) {
        error = "LZ77 type " + std::to_string(word & 0xFF) + " is not supported";
        return false;
    }
    const std::size_t want = word >> 8;
    if (want > max_out) {
        error = "LZ77 data unpacks too large";
        return false;
    }
    std::vector<std::uint8_t> o;
    o.reserve(want);
    std::size_t i = 8;
    while (o.size() < want) {
        if (i >= size) {
            error = "LZ77 data ends early";
            return false;
        }
        const std::uint8_t flags = data[i++];
        for (int bit = 7; bit >= 0 && o.size() < want; --bit) {
            if (!(flags & (1u << bit))) {
                if (i >= size) {
                    error = "LZ77 data ends early";
                    return false;
                }
                o.push_back(data[i++]);
                continue;
            }
            if (i + 1 >= size) {
                error = "LZ77 data ends early";
                return false;
            }
            const std::size_t length = (data[i] >> 4) + 3;
            const std::size_t distance = (((data[i] & 0x0F) << 8) | data[i + 1]) + 1;
            i += 2;
            if (distance > o.size()) {
                error = "LZ77 reference before the start";
                return false;
            }
            for (std::size_t k = 0; k < length && o.size() < want; ++k) o.push_back(o[o.size() - distance]);
        }
    }
    out.swap(o);
    return true;
}

bool U8Archive::parse(const std::uint8_t* data, std::size_t size, U8Archive& out, std::string& error) {
    if (!data || size < 0x20 || be32(data) != 0x55AA382Du) {
        error = "not a U8 archive";
        return false;
    }
    const std::uint32_t root = be32(data + 4), header = be32(data + 8);
    if (root < 0x20 || root > size || header > size - root || header < 12) {
        error = "U8 header points outside the archive";
        return false;
    }
    const std::uint8_t* nodes = data + root;
    const std::uint32_t count = be32(nodes + 8);
    if (count == 0 || count > header / 12) {
        error = "U8 node count is wrong";
        return false;
    }
    const std::uint8_t* strings = nodes + static_cast<std::size_t>(count) * 12;
    const std::size_t strings_size = header - static_cast<std::size_t>(count) * 12;
    const auto name_of = [&](std::uint32_t off, std::string& name) {
        if (off >= strings_size) return false;
        const void* end = std::memchr(strings + off, 0, strings_size - off);
        if (!end) return false;
        name.assign(reinterpret_cast<const char*>(strings + off),
                    static_cast<const std::uint8_t*>(end) - (strings + off));
        return true;
    };
    U8Archive a;
    a.base_ = data;
    // The open directories: where each ends and its path.
    struct Dir {
        std::uint32_t end;
        std::string path;
    };
    std::vector<Dir> dirs = {{count, ""}};
    for (std::uint32_t n = 1; n < count; ++n) {
        while (dirs.size() > 1 && n >= dirs.back().end) dirs.pop_back();
        const std::uint8_t* e = nodes + static_cast<std::size_t>(n) * 12;
        std::string name;
        if (!name_of(be32(e) & 0x00FFFFFFu, name)) {
            error = "U8 name points outside the string table";
            return false;
        }
        const std::uint32_t off = be32(e + 4), len = be32(e + 8);
        if (e[0] == 1) {
            if (len <= n || len > count) {
                error = "U8 directory ends in the wrong place";
                return false;
            }
            dirs.push_back({len, dirs.back().path + "/" + name});
        } else {
            if (off > size || len > size - off) {
                error = "U8 file lies outside the archive";
                return false;
            }
            a.files_.push_back({normal_path(dirs.back().path + "/" + name), off, len});
        }
    }
    out = std::move(a);
    return true;
}

bool U8Archive::find(const std::string& path, const std::uint8_t*& data, std::size_t& size) const {
    const std::string want = normal_path(path);
    for (const File& f : files_) {
        if (!same_path(f.path, want)) continue;
        data = base_ + f.offset;
        size = f.size;
        return true;
    }
    return false;
}

std::vector<std::string> U8Archive::files() const {
    std::vector<std::string> out;
    for (const File& f : files_) out.push_back(f.path);
    return out;
}

std::vector<std::string> U8Archive::files_in(const std::string& dir, const std::string& suffix) const {
    std::string prefix = normal_path(dir);
    if (prefix != "/") prefix += "/";
    std::vector<std::string> out;
    for (const File& f : files_) {
        if (f.path.size() < prefix.size() + suffix.size()) continue;
        if (!same_path(f.path.substr(0, prefix.size()), prefix)) continue;
        if (!same_path(f.path.substr(f.path.size() - suffix.size()), suffix)) continue;
        out.push_back(f.path);
    }
    return out;
}

bool unpack_banner_bin(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::string& error) {
    if (data && size >= 0x20 && std::memcmp(data, "IMD5", 4) == 0) {
        data += 0x20;
        size -= 0x20;
    }
    if (size >= 4 && std::memcmp(data, "LZ77", 4) == 0) return lz77_decompress(data, size, out, error);
    if (size >= 4 && be32(data) == 0x55AA382Du) {
        out.assign(data, data + size);
        return true;
    }
    error = "the banner holds no U8 archive";
    return false;
}

bool parse_opening_bnr(const std::uint8_t* data, std::size_t size, OpeningBanner& out, std::string& error,
                       bool want_banner) {
    // On a disc IMET is at 0x40; in a channel's first content at 0x80.
    std::size_t imet = 0;
    if (size >= 0x44 && std::memcmp(data + 0x40, "IMET", 4) == 0) imet = 0x40;
    else if (size >= 0x84 && std::memcmp(data + 0x80, "IMET", 4) == 0) imet = 0x80;
    else {
        error = "no IMET header";
        return false;
    }
    const std::size_t archive = imet + 0x5C0;  // the header's own 0x600 bytes, counted from the file start
    if (size < archive + 0x20) {
        error = "opening.bnr is too short";
        return false;
    }
    OpeningBanner b;
    for (int l = 0; l < kBnrLanguages; ++l) {
        // Two lines of 21 characters each.
        const std::uint8_t* name = data + imet + 0x1C + static_cast<std::size_t>(l) * 84;
        const std::string first = utf16be(name, 21), second = utf16be(name + 42, 21);
        b.names[l] = second.empty() ? first : first + "\n" + second;
    }
    U8Archive u8;
    if (!U8Archive::parse(data + archive, size - archive, u8, error)) return false;
    const std::uint8_t* part = nullptr;
    std::size_t part_size = 0;
    if (!u8.find("/meta/icon.bin", part, part_size) || !unpack_banner_bin(part, part_size, b.icon, error)) {
        if (error.empty()) error = "no meta/icon.bin";
        error = "icon.bin: " + error;
        return false;
    }
    if (want_banner && u8.find("/meta/banner.bin", part, part_size) &&
        !unpack_banner_bin(part, part_size, b.banner, error)) {
        error = "banner.bin: " + error;
        return false;
    }
    if (u8.find("/meta/sound.bin", part, part_size)) b.sound.assign(part, part + part_size);
    out = std::move(b);
    return true;
}

bool read_partition_file(const ByteSource& data, const std::string& path, std::vector<std::uint8_t>& out,
                         std::string& error, std::size_t max_size) {
    PartitionDataHeader header;
    if (!read_partition_data_header(data, header, error)) return false;
    if (header.fst_size > (8u << 20)) {
        error = "FST too large";
        return false;
    }
    std::vector<std::uint8_t> fst_bytes(static_cast<std::size_t>(header.fst_size));
    if (!data.read(header.fst_offset, fst_bytes.data(), fst_bytes.size())) {
        error = "cannot read the FST";
        return false;
    }
    Fst fst;
    if (!Fst::parse(fst_bytes.data(), fst_bytes.size(), true, fst, error)) return false;
    const std::uint32_t index = fst.find(path, true);
    if (index == Fst::npos || fst.entries()[index].is_directory) {
        error = path + " is not on the disc";
        return false;
    }
    const FstEntry& e = fst.entries()[index];
    if (e.size > max_size) {
        error = path + " is too large";
        return false;
    }
    std::vector<std::uint8_t> bytes(e.size);
    if (e.size && !data.read(e.offset, bytes.data(), bytes.size())) {
        error = "cannot read " + path;
        return false;
    }
    out.swap(bytes);
    return true;
}

}  // namespace riftwii
