// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/disc.hpp"

#include <limits>

namespace riftwii {
namespace {

constexpr std::uint32_t kMaxPartitionsPerGroup = 32;
constexpr std::uint32_t kMaxPartitions = 64;
constexpr std::uint32_t kTmdFixedBytes = 0x1E4;   // header up to the content records
constexpr std::uint32_t kTmdContentBytes = 36;
constexpr std::uint32_t kMaxTmdBytes = 64 * 1024;
constexpr std::uint64_t kMaxApploaderBytes = 2 * 1024 * 1024;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint64_t be64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
}

std::uint64_t shifted(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(be32(p)) << 2;
}

std::string trimmed(const std::uint8_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
    std::string s;
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char c = p[i];
        s.push_back((c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c));
    }
    return s;
}

bool read_exact(const ByteSource& source, std::uint64_t offset, std::vector<std::uint8_t>& buffer,
                std::size_t length, const char* what, std::string& error) {
    if (offset > source.size() || length > source.size() - offset) {
        error = std::string(what) + " lies outside the source";
        return false;
    }
    try {
        buffer.assign(length, 0);
    } catch (...) {
        error = "allocation failure";
        return false;
    }
    if (!source.read(offset, buffer.data(), length)) {
        error = std::string("cannot read ") + what;
        return false;
    }
    return true;
}

bool is_id_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

}  // namespace

DiscIdentity DiscHeader::identity() const {
    DiscIdentity id;
    id.id = game_id;
    id.revision = version;
    id.number = disc_number;
    return id;
}

std::uint32_t Tmd::required_ios() const {
    if ((sys_version >> 32) != 1u) return 0;
    return static_cast<std::uint32_t>(sys_version & 0xFFFFFFFFu);
}

bool parse_disc_header(const std::uint8_t* data, std::size_t size, DiscHeader& out, std::string& error) {
    if (data == nullptr || size < kDiscHeaderBytes) {
        error = "disc header too small";
        return false;
    }
    DiscHeader h;
    h.game_id.assign(reinterpret_cast<const char*>(data), 6);
    for (unsigned char c : h.game_id) {
        if (!is_id_char(c)) {
            error = "disc header has an invalid game id";
            return false;
        }
    }
    h.disc_number = data[6];
    h.version = data[7];
    h.wii_magic = be32(data + 0x18) == kWiiMagic;
    h.gamecube_magic = be32(data + 0x1C) == kGameCubeMagic;
    if (!h.wii_magic && !h.gamecube_magic) {
        error = "disc header has no Wii or GameCube magic";
        return false;
    }
    h.title = trimmed(data + 0x20, 64);
    out = h;
    error.clear();
    return true;
}

bool read_disc_header(const ByteSource& disc, DiscHeader& out, std::string& error) {
    std::vector<std::uint8_t> buffer;
    if (!read_exact(disc, 0, buffer, static_cast<std::size_t>(kDiscHeaderBytes), "disc header", error)) return false;
    return parse_disc_header(buffer.data(), buffer.size(), out, error);
}

bool read_partition_table(const ByteSource& disc, std::vector<PartitionEntry>& out, std::string& error) {
    std::vector<std::uint8_t> groups;
    if (!read_exact(disc, kPartitionTableOffset, groups, 32, "partition table", error)) return false;
    std::vector<PartitionEntry> table;
    for (std::uint32_t g = 0; g < 4; ++g) {
        const std::uint32_t count = be32(groups.data() + g * 8);
        const std::uint64_t info_offset = shifted(groups.data() + g * 8 + 4);
        if (count == 0) continue;
        if (count > kMaxPartitionsPerGroup) {
            error = "partition group " + std::to_string(g) + " lists too many partitions";
            return false;
        }
        std::vector<std::uint8_t> entries;
        if (!read_exact(disc, info_offset, entries, static_cast<std::size_t>(count) * 8, "partition info table", error)) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            PartitionEntry e;
            e.offset = shifted(entries.data() + i * 8);
            e.type = be32(entries.data() + i * 8 + 4);
            e.group = g;
            if (e.offset == 0 || e.offset >= disc.size()) {
                error = "partition " + std::to_string(i) + " in group " + std::to_string(g) + " lies outside the disc";
                return false;
            }
            table.push_back(e);
            if (table.size() > kMaxPartitions) {
                error = "too many partitions";
                return false;
            }
        }
    }
    if (table.empty()) {
        error = "disc has no partitions";
        return false;
    }
    out = std::move(table);
    error.clear();
    return true;
}

bool find_game_partition(const std::vector<PartitionEntry>& table, PartitionEntry& out) {
    for (const auto& e : table) {
        if (e.type == 0) {
            out = e;
            return true;
        }
    }
    return false;
}

bool read_partition_header(const ByteSource& disc, std::uint64_t partition_offset,
                           PartitionHeader& out, std::string& error) {
    std::vector<std::uint8_t> h;
    if (!read_exact(disc, partition_offset, h, static_cast<std::size_t>(kPartitionHeaderBytes), "partition header", error)) return false;
    PartitionHeader p;
    p.offset = partition_offset;
    p.tmd_size = be32(h.data() + 0x2A4);
    p.cert_size = be32(h.data() + 0x2AC);
    const std::uint64_t tmd_rel = shifted(h.data() + 0x2A8);
    const std::uint64_t cert_rel = shifted(h.data() + 0x2B0);
    const std::uint64_t h3_rel = shifted(h.data() + 0x2B4);
    const std::uint64_t data_rel = shifted(h.data() + 0x2B8);
    p.data_size = shifted(h.data() + 0x2BC);
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - partition_offset;
    if (tmd_rel > limit || cert_rel > limit || h3_rel > limit || data_rel > limit) {
        error = "partition header offsets overflow";
        return false;
    }
    p.tmd_offset = partition_offset + tmd_rel;
    p.cert_offset = partition_offset + cert_rel;
    p.h3_offset = partition_offset + h3_rel;
    p.data_offset = partition_offset + data_rel;
    if (p.tmd_size < kTmdFixedBytes || p.tmd_size > kMaxTmdBytes) {
        error = "partition TMD size is implausible";
        return false;
    }
    if (p.tmd_offset >= disc.size() || p.tmd_size > disc.size() - p.tmd_offset) {
        error = "partition TMD lies outside the disc";
        return false;
    }
    if (data_rel == 0 || p.data_offset >= disc.size()) {
        error = "partition data lies outside the disc";
        return false;
    }
    if (p.data_size > disc.size() - p.data_offset) {
        error = "partition data size exceeds the disc";
        return false;
    }
    out = p;
    error.clear();
    return true;
}

bool parse_tmd(const std::uint8_t* data, std::size_t size, Tmd& out, std::string& error) {
    if (data == nullptr || size < kTmdFixedBytes) {
        error = "TMD too small";
        return false;
    }
    Tmd t;
    t.sys_version = be64(data + 0x184);
    t.title_id = be64(data + 0x18C);
    t.title_version = be16(data + 0x1DC);
    t.content_count = be16(data + 0x1DE);
    const std::uint64_t needed = kTmdFixedBytes + static_cast<std::uint64_t>(t.content_count) * kTmdContentBytes;
    if (needed > size) {
        error = "TMD content count exceeds its size";
        return false;
    }
    out = t;
    error.clear();
    return true;
}

bool read_tmd(const ByteSource& disc, const PartitionHeader& partition, Tmd& out, std::string& error) {
    std::vector<std::uint8_t> buffer;
    if (!read_exact(disc, partition.tmd_offset, buffer, partition.tmd_size, "TMD", error)) return false;
    return parse_tmd(buffer.data(), buffer.size(), out, error);
}

bool read_partition_data_header(const ByteSource& data, PartitionDataHeader& out, std::string& error) {
    std::vector<std::uint8_t> h;
    if (!read_exact(data, 0, h, static_cast<std::size_t>(kPartitionDataHeaderBytes), "partition data header", error)) return false;
    DiscHeader disc_header;
    if (!parse_disc_header(h.data(), h.size(), disc_header, error)) return false;
    PartitionDataHeader d;
    d.dol_offset = shifted(h.data() + 0x420);
    d.fst_offset = shifted(h.data() + 0x424);
    d.fst_size = shifted(h.data() + 0x428);
    d.fst_max_size = shifted(h.data() + 0x42C);
    if (d.dol_offset == 0 || d.fst_offset == 0 || d.fst_size == 0) {
        error = "partition data header has empty DOL/FST fields";
        return false;
    }
    if (d.fst_size > d.fst_max_size) {
        error = "FST size exceeds its maximum";
        return false;
    }
    if (d.fst_offset >= data.size() || d.fst_size > data.size() - d.fst_offset) {
        error = "FST lies outside the partition data";
        return false;
    }
    if (d.dol_offset >= data.size()) {
        error = "DOL lies outside the partition data";
        return false;
    }
    out = d;
    error.clear();
    return true;
}

bool encode_partition_data_fields(const PartitionDataHeader& header, std::uint8_t* out, std::string& error) {
    const std::uint64_t values[4] = {header.dol_offset, header.fst_offset, header.fst_size, header.fst_max_size};
    if (header.fst_size > header.fst_max_size) {
        error = "FST size exceeds its maximum";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if ((values[i] & 3u) != 0 || (values[i] >> 2) > 0xFFFFFFFFull) {
            error = "partition data header field cannot be encoded";
            return false;
        }
        const std::uint32_t words = static_cast<std::uint32_t>(values[i] >> 2);
        out[i * 4 + 0] = static_cast<std::uint8_t>(words >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(words >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(words >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(words);
    }
    error.clear();
    return true;
}

bool read_apploader_header(const ByteSource& data, ApploaderHeader& out, std::string& error) {
    std::vector<std::uint8_t> h;
    if (!read_exact(data, kApploaderOffset, h, static_cast<std::size_t>(kApploaderHeaderBytes), "apploader header", error)) return false;
    ApploaderHeader a;
    a.date = trimmed(h.data(), 16);
    a.entry = be32(h.data() + 0x10);
    a.size = be32(h.data() + 0x14);
    a.trailer_size = be32(h.data() + 0x18);
    a.code_offset = kApploaderOffset + kApploaderHeaderBytes;
    if (a.size == 0 || a.total_size() > kMaxApploaderBytes) {
        error = "apploader size is implausible";
        return false;
    }
    if (a.code_offset >= data.size() || a.total_size() > data.size() - a.code_offset) {
        error = "apploader lies outside the partition data";
        return false;
    }
    // The apploader is loaded to 0x81200000; an entry outside the cached
    // MEM1 window cannot be right.
    if (a.entry < 0x80000000u || a.entry >= 0x81800000u) {
        error = "apploader entry point is not in MEM1";
        return false;
    }
    out = a;
    error.clear();
    return true;
}

}  // namespace riftwii
