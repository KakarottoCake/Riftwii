// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/rvz.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "riftwii/disc.hpp"
#include "riftwii/sha1.hpp"
#include "zstd.h"

namespace riftwii {
namespace {

constexpr std::uint32_t kSector = 0x8000;
constexpr std::uint32_t kSectorData = 0x7C00;
constexpr std::uint32_t kTwoMiB = 0x200000;
constexpr std::uint32_t kFileHead = 0x48;
constexpr std::uint32_t kDiscStruct = 0xDC;
constexpr std::uint32_t kPartEntry = 0x30;
constexpr std::uint32_t kRawEntry = 24;
constexpr std::uint32_t kRvzGroupEntry = 12;
// Most exceptions a list may hold when a group's size decoded is not
// recorded in its frame: Dolphin's own limit, enough for every hash and
// all padding in 2 MiB.
constexpr std::uint32_t kMaxExceptions = 52 * 64;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

bool digest_is(const std::uint8_t* data, std::size_t length, const std::uint8_t* expected) {
    const Sha1Digest d = sha1(data, length);
    return std::memcmp(d.data(), expected, d.size()) == 0;
}

std::string version_text(std::uint32_t v) {
    // AABBCCDD = A.BB, or A.BB.CC when CC is not zero.
    char buf[24];
    const unsigned a = v >> 24, b = (v >> 16) & 0xFF, c = (v >> 8) & 0xFF;
    if (c != 0) {
        std::snprintf(buf, sizeof buf, "%x.%02x.%02x", a, b, c);
    } else {
        std::snprintf(buf, sizeof buf, "%x.%02x", a, b);
    }
    return buf;
}

std::string size_text(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) {
        std::snprintf(buf, sizeof buf, "%llu MiB", static_cast<unsigned long long>(bytes / (1024 * 1024)));
    } else if (bytes % 1024 == 0) {
        std::snprintf(buf, sizeof buf, "%llu KiB", static_cast<unsigned long long>(bytes / 1024));
    } else {
        std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

bool chunk_size_valid(std::uint32_t chunk, bool wia) {
    if (chunk == 0) return false;
    if (chunk >= kTwoMiB || wia) return chunk % kTwoMiB == 0;
    return chunk >= kSector && (chunk & (chunk - 1)) == 0;
}

}  // namespace

const char* to_string(RvzMethod method) {
    switch (method) {
    case RvzMethod::None: return "no compression";
    case RvzMethod::Purge: return "Purge";
    case RvzMethod::Bzip2: return "bzip2";
    case RvzMethod::Lzma: return "LZMA";
    case RvzMethod::Lzma2: return "LZMA2";
    case RvzMethod::Zstd: return "Zstandard";
    }
    return "an unknown method";
}

bool read_rvz_head(const ByteSource& file, RvzHead& out, std::string& error) {
    out = RvzHead{};
    std::uint8_t head[kFileHead];
    if (file.size() < kFileHead + kDiscStruct || !file.read(0, head, sizeof head)) {
        error = "too short to be an RVZ file";
        return false;
    }
    if (std::memcmp(head, "RVZ\x01", 4) == 0) {
        out.wia = false;
    } else if (std::memcmp(head, "WIA\x01", 4) == 0) {
        out.wia = true;
    } else {
        error = "not an RVZ file";
        return false;
    }
    if (!digest_is(head, 0x34, head + 0x34)) {
        error = "the file header is damaged (its SHA-1 does not match)";
        return false;
    }
    out.version = be32(head + 4);
    out.version_compatible = be32(head + 8);
    const std::uint32_t disc_bytes = be32(head + 0x0C);
    out.iso_size = be64(head + 0x24);
    out.file_size = be64(head + 0x2C);
    if (disc_bytes < kDiscStruct || disc_bytes > 0x1000 || kFileHead + disc_bytes > file.size()) {
        error = "the disc header has an impossible size";
        return false;
    }
    std::vector<std::uint8_t> disc(disc_bytes);
    if (!file.read(kFileHead, disc.data(), disc.size())) {
        error = "cannot read the disc header";
        return false;
    }
    if (!digest_is(disc.data(), disc.size(), head + 0x10)) {
        error = "the disc header is damaged (its SHA-1 does not match)";
        return false;
    }
    const std::uint8_t* d = disc.data();
    out.disc_type = be32(d);
    out.method = be32(d + 4);
    out.level = static_cast<std::int32_t>(be32(d + 8));
    out.chunk_size = be32(d + 0x0C);
    std::memcpy(out.disc_header.data(), d + 0x10, out.disc_header.size());
    const std::uint32_t part_count = be32(d + 0x90);
    const std::uint32_t part_bytes = be32(d + 0x94);
    const std::uint64_t part_offset = be64(d + 0x98);
    out.raw_count = be32(d + 0xB4);
    out.raw_table_offset = be64(d + 0xB8);
    out.raw_table_bytes = be32(d + 0xC0);
    out.group_count = be32(d + 0xC4);
    out.group_table_offset = be64(d + 0xC8);
    out.group_table_bytes = be32(d + 0xD0);
    if (part_count > 64 || (part_count != 0 && (part_bytes == 0 || part_bytes > 0x1000))) {
        error = "the partition table has an impossible size";
        return false;
    }
    if (part_count != 0) {
        std::vector<std::uint8_t> parts(std::size_t(part_count) * part_bytes);
        if (part_offset > file.size() || parts.size() > file.size() - part_offset ||
            !file.read(part_offset, parts.data(), parts.size())) {
            error = "cannot read the partition table";
            return false;
        }
        if (!digest_is(parts.data(), parts.size(), d + 0xA0)) {
            error = "the partition table is damaged (its SHA-1 does not match)";
            return false;
        }
        for (std::uint32_t i = 0; i < part_count; ++i) {
            // A shorter entry reads as if padded with zeroes.
            std::uint8_t e[kPartEntry] = {};
            std::memcpy(e, parts.data() + std::size_t(i) * part_bytes, std::min(part_bytes, kPartEntry));
            RvzPartition p;
            std::memcpy(p.title_key.data(), e, 16);
            for (int k = 0; k < 2; ++k) {
                const std::uint8_t* q = e + 16 + 16 * k;
                p.data[k] = RvzPartData{be32(q), be32(q + 4), be32(q + 8), be32(q + 12)};
            }
            out.partitions.push_back(p);
        }
    }
    return true;
}

RvzVerdict check_rvz(const RvzHead& head, std::uint64_t file_bytes) {
    RvzVerdict v;
    std::vector<std::string> refuse, risk;
    if (head.wia) {
        refuse.push_back("This is a WIA file. RiftWii plays RVZ files only: convert it in Dolphin to RVZ with Zstandard.");
    } else {
        if (head.version_compatible > kRvzVersion) {
            refuse.push_back("It uses RVZ " + version_text(head.version_compatible) + ", newer than the " +
                             version_text(kRvzVersion) + " RiftWii can read.");
        } else if (head.version < kRvzOldestVersion) {
            refuse.push_back("It was made before RVZ " + version_text(kRvzOldestVersion) +
                             " was finished: convert it again with a current Dolphin.");
        } else if (head.version > kRvzVersion) {
            risk.push_back("It was made by a newer Dolphin (RVZ " + version_text(head.version) +
                           "); RiftWii was tested with " + version_text(kRvzVersion) + ".");
        }
        const auto method = static_cast<RvzMethod>(head.method);
        if (method == RvzMethod::Bzip2 || method == RvzMethod::Lzma || method == RvzMethod::Lzma2) {
            refuse.push_back(std::string("It is compressed with ") + to_string(method) +
                             ", too slow to decompress on a Wii: convert it again with Zstandard.");
        } else if (method != RvzMethod::Zstd && method != RvzMethod::None) {
            refuse.push_back("It uses compression method " + std::to_string(head.method) + ", which RVZ does not define.");
        } else if (method == RvzMethod::Zstd && head.level > kRvzMaxZstdLevel) {
            risk.push_back("Zstandard level " + std::to_string(head.level) + " is above the highest level (" +
                           std::to_string(kRvzMaxZstdLevel) + "): it was made by a tool RiftWii was not tested with.");
        }
        if (!chunk_size_valid(head.chunk_size, false)) {
            refuse.push_back("Its chunk size (" + std::to_string(head.chunk_size) + " bytes) is not one RVZ allows.");
        } else if (head.chunk_size > kRvzMaxChunk) {
            refuse.push_back("Its " + size_text(head.chunk_size) + " chunks are too large to read during play (at most " +
                             size_text(kRvzMaxChunk) + "): convert it again with " + size_text(kRvzTestedChunk) + ".");
        } else if (head.chunk_size > kRvzTestedChunk) {
            risk.push_back("Its " + size_text(head.chunk_size) + " chunks are larger than the tested " +
                           size_text(kRvzTestedChunk) + ": loading may be slower and may stutter.");
        }
    }
    if (head.disc_type == 1) {
        refuse.push_back("It is a GameCube game; RiftWii plays Wii games.");
    } else if (head.disc_type != 2) {
        refuse.push_back("It is not a Wii disc (disc type " + std::to_string(head.disc_type) + ").");
    } else if (head.partitions.empty()) {
        refuse.push_back("It holds no Wii partitions.");
    }
    if (file_bytes < head.file_size) {
        refuse.push_back("The file is incomplete: " + std::to_string(file_bytes) + " of " +
                         std::to_string(head.file_size) + " bytes.");
    } else if (file_bytes > head.file_size) {
        refuse.push_back("The file is larger than its header says (" + std::to_string(file_bytes) + " bytes, not " +
                         std::to_string(head.file_size) + ").");
    }
    if (!refuse.empty()) {
        v.support = RvzSupport::Unsupported;
        v.reasons = refuse;
    } else if (!risk.empty()) {
        v.support = RvzSupport::AtOwnRisk;
        v.reasons = risk;
    }
    return v;
}

std::string describe_rvz(const RvzHead& head) {
    std::string s = head.wia ? "WIA " : "RVZ ";
    s += version_text(head.version) + ", ";
    const auto method = static_cast<RvzMethod>(head.method);
    s += to_string(method);
    if (method != RvzMethod::None) s += " level " + std::to_string(head.level);
    s += ", " + size_text(head.chunk_size) + " chunks";
    return s;
}

RvzImage::~RvzImage() {
    if (dctx_ != nullptr) ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(dctx_));
}

bool RvzImage::fail(std::string message) const {
    error_ = std::move(message);
    return false;
}

bool RvzImage::open(std::shared_ptr<const ByteSource> file, std::unique_ptr<RvzImage>& out, std::string& error) {
    std::unique_ptr<RvzImage> image(new RvzImage());
    RvzHead& head = image->head_;
    if (!read_rvz_head(*file, head, error)) return false;
    if (head.wia) {
        error = "WIA files are not read, only RVZ";
        return false;
    }
    const auto method = static_cast<RvzMethod>(head.method);
    if (method != RvzMethod::None && method != RvzMethod::Zstd) {
        error = std::string("cannot decompress ") + to_string(method);
        return false;
    }
    if (!chunk_size_valid(head.chunk_size, false)) {
        error = "invalid chunk size " + std::to_string(head.chunk_size);
        return false;
    }
    image->file_ = std::move(file);
    if (method == RvzMethod::Zstd) {
        image->dctx_ = ZSTD_createDCtx();
        if (image->dctx_ == nullptr) {
            error = "out of memory for the Zstandard decoder";
            return false;
        }
    }
    const bool compressed = method == RvzMethod::Zstd;
    if (head.raw_count > 0x10000 || head.group_count > 0x1000000) {
        error = "the image's tables have impossible sizes";
        return false;
    }
    std::vector<std::uint8_t> table;
    if (!image->decode(head.raw_table_offset, head.raw_table_bytes, compressed, head.raw_count * kRawEntry, table) ||
        table.size() != std::size_t(head.raw_count) * kRawEntry) {
        error = "cannot read the raw data table: " + image->error_;
        return false;
    }
    for (std::uint32_t i = 0; i < head.raw_count; ++i) {
        const std::uint8_t* e = table.data() + std::size_t(i) * kRawEntry;
        const std::uint64_t offset = be64(e);
        const std::uint64_t size = be64(e + 8);
        Raw r;
        // The first entry starts at 0x80 but holds data from 0: offsets are
        // rounded down to a sector, keeping the end.
        r.start = offset - offset % kSector;
        r.end = offset + size;
        r.group_index = be32(e + 16);
        r.group_count = be32(e + 20);
        if (r.end < offset || r.group_index > head.group_count || r.group_count > head.group_count - r.group_index ||
            (r.end - r.start + head.chunk_size - 1) / head.chunk_size > r.group_count) {
            error = "raw data entry " + std::to_string(i) + " is out of range";
            return false;
        }
        image->raw_.push_back(r);
    }
    if (!image->decode(head.group_table_offset, head.group_table_bytes, compressed, head.group_count * kRvzGroupEntry,
                       table) ||
        table.size() != std::size_t(head.group_count) * kRvzGroupEntry) {
        error = "cannot read the group table: " + image->error_;
        return false;
    }
    for (std::uint32_t i = 0; i < head.group_count; ++i) {
        const std::uint8_t* e = table.data() + std::size_t(i) * kRvzGroupEntry;
        image->groups_.push_back(Group{be32(e), be32(e + 4), be32(e + 8)});
    }
    const std::uint32_t per_group = head.chunk_size / kSector;
    for (std::size_t p = 0; p < head.partitions.size(); ++p) {
        const RvzPartition& part = head.partitions[p];
        for (const RvzPartData& pd : part.data) {
            if (pd.sector_count == 0) continue;
            if (pd.first_sector < part.data[0].first_sector || pd.group_index > head.group_count ||
                pd.group_count > head.group_count - pd.group_index ||
                (pd.sector_count + per_group - 1) / per_group > pd.group_count) {
                error = "partition " + std::to_string(p) + "'s data is out of range";
                return false;
            }
        }
    }
    out = std::move(image);
    return true;
}

bool RvzImage::decode(std::uint64_t file_offset, std::uint32_t stored, bool compressed, std::uint32_t expected,
                      std::vector<std::uint8_t>& out) const {
    if (file_offset > file_->size() || stored > file_->size() - file_offset) {
        return fail("data at " + std::to_string(file_offset) + " runs past the end of the file");
    }
    if (!compressed) {
        out.resize(stored);
        if (stored != 0 && !file_->read(file_offset, out.data(), stored)) return fail("read error");
        return true;
    }
    stored_.resize(stored);
    if (stored != 0 && !file_->read(file_offset, stored_.data(), stored)) return fail("read error");
    std::size_t capacity = expected;
    const unsigned long long known = ZSTD_getFrameContentSize(stored_.data(), stored_.size());
    if (known == ZSTD_CONTENTSIZE_ERROR) return fail("not Zstandard data");
    if (known != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (known > std::size_t(expected) + 64 * 1024 * 1024) return fail("a group claims an impossible size");
        capacity = static_cast<std::size_t>(known);
    }
    out.resize(capacity);
    const std::size_t got =
        ZSTD_decompressDCtx(static_cast<ZSTD_DCtx*>(dctx_), out.data(), out.size(), stored_.data(), stored_.size());
    if (ZSTD_isError(got)) return fail(std::string("Zstandard: ") + ZSTD_getErrorName(got));
    out.resize(got);
    return true;
}

bool RvzImage::load_group(std::uint32_t index, std::uint32_t payload, std::uint64_t data_offset,
                          std::uint32_t lists) const {
    if (cached_ == index) return true;
    cached_ = UINT32_MAX;
    if (index >= groups_.size()) return fail("group " + std::to_string(index) + " does not exist");
    const Group& g = groups_[index];
    const std::uint32_t stored = g.size & 0x7FFFFFFFu;
    cache_.resize(payload);
    if (stored == 0) {
        std::fill(cache_.begin(), cache_.end(), 0);
        cached_ = index;
        return true;
    }
    const bool compressed = (g.size & 0x80000000u) != 0 && static_cast<RvzMethod>(head_.method) == RvzMethod::Zstd;
    const std::uint32_t body = g.packed != 0 ? g.packed : payload;
    const std::uint32_t expected = body + lists * (2 + 22 * kMaxExceptions);
    if (!decode(std::uint64_t(g.offset4) * 4, stored, compressed, expected, unpacked_)) {
        return fail("group " + std::to_string(index) + ": " + error_);
    }
    const std::uint32_t have = static_cast<std::uint32_t>(unpacked_.size());
    std::uint32_t at = 0;
    if (lists != 0 && rtrvz_exception_bytes(unpacked_.data(), have, lists, !compressed, &at) != RTRVZ_OK) {
        return fail("group " + std::to_string(index) + ": its hash exceptions are cut short");
    }
    if (have - at < body) return fail("group " + std::to_string(index) + " holds less data than it should");
    if (g.packed != 0) {
        if (rtrvz_unpack(unpacked_.data() + at, g.packed, data_offset, 0, cache_.data(), payload, &junk_) !=
            RTRVZ_OK) {
            return fail("group " + std::to_string(index) + ": its packed data is damaged");
        }
    } else {
        std::memcpy(cache_.data(), unpacked_.data() + at, payload);
    }
    cached_ = index;
    return true;
}

bool RvzImage::read_raw(std::uint64_t disc_offset, std::uint8_t* destination, std::size_t length) const {
    while (length > 0) {
        const Raw* raw = nullptr;
        for (const Raw& r : raw_) {
            if (disc_offset >= r.start && disc_offset < r.end) {
                raw = &r;
                break;
            }
        }
        if (raw == nullptr) {
            for (std::size_t p = 0; p < head_.partitions.size(); ++p) {
                const std::uint64_t start = partition_data_disc_offset(p);
                const std::uint64_t end = start + partition_data_size(p) / kSectorData * kSector;
                if (disc_offset >= start && disc_offset < end) {
                    return fail("disc offset " + std::to_string(disc_offset) + " is inside partition " +
                                std::to_string(p) + "'s encrypted data");
                }
            }
            return fail("disc offset " + std::to_string(disc_offset) + " is not stored in the image");
        }
        const std::uint64_t j = (disc_offset - raw->start) / head_.chunk_size;
        const std::uint64_t group_start = raw->start + j * head_.chunk_size;
        const auto payload = static_cast<std::uint32_t>(std::min<std::uint64_t>(head_.chunk_size, raw->end - group_start));
        if (!load_group(raw->group_index + static_cast<std::uint32_t>(j), payload, group_start, 0)) return false;
        const auto within = static_cast<std::size_t>(disc_offset - group_start);
        const std::size_t take = std::min<std::size_t>(length, payload - within);
        std::memcpy(destination, cache_.data() + within, take);
        // The first 0x80 bytes of the disc live in the disc header struct.
        for (std::size_t i = 0; i < take && disc_offset + i < head_.disc_header.size(); ++i) {
            destination[i] = head_.disc_header[static_cast<std::size_t>(disc_offset + i)];
        }
        destination += take;
        disc_offset += take;
        length -= take;
    }
    return true;
}

bool RvzImage::runtime_table(std::size_t index, RvzRuntimeTable& out, std::string& error) const {
    out = RvzRuntimeTable{};
    if (index >= head_.partitions.size()) {
        error = "no partition " + std::to_string(index);
        return false;
    }
    const RvzPartition& part = head_.partitions[index];
    const std::uint32_t per_group = head_.chunk_size / kSector;
    const bool zstd = static_cast<RvzMethod>(head_.method) == RvzMethod::Zstd;
    if (per_group == 0 || part.data[0].sector_count == 0) {
        error = "partition " + std::to_string(index) + " holds no data";
        return false;
    }
    if (part.data[1].sector_count != 0 &&
        part.data[1].first_sector != part.data[0].first_sector + part.data[0].sector_count) {
        error = "partition " + std::to_string(index) + "'s data is stored in two separate pieces";
        return false;
    }
    out.group_kib = per_group * (kSectorData / 1024);
    out.split_kib = part.data[0].sector_count * (kSectorData / 1024);
    out.data_kib = static_cast<std::uint32_t>(partition_data_size(index) / 1024);
    out.lists = head_.chunk_size >= kTwoMiB ? head_.chunk_size / kTwoMiB : 1;
    for (std::size_t s = 0; s < 2; ++s) {
        const RvzPartData& pd = part.data[s];
        if (pd.sector_count == 0) continue;
        const std::uint32_t needed = (pd.sector_count + per_group - 1) / per_group;
        if (pd.group_count < needed || std::uint64_t(pd.group_index) + needed > groups_.size()) {
            error = "partition " + std::to_string(index) + " has fewer groups than its data needs";
            return false;
        }
        if (s == 0) out.first_groups = needed;
        for (std::uint32_t j = 0; j < needed; ++j) {
            const Group& g = groups_[pd.group_index + j];
            const std::uint32_t stored = g.size & 0x7FFFFFFFu;
            const std::uint32_t sectors = std::min<std::uint32_t>(per_group, pd.sector_count - j * per_group);
            const std::uint32_t body = g.packed != 0 ? g.packed : sectors * kSectorData;
            if (stored > 0x3FFFFFFFu || g.packed > 0x3FFFFFFFu) {
                error = "group " + std::to_string(pd.group_index + j) + " is too large";
                return false;
            }
            std::uint32_t word1 = stored;
            if (stored != 0 && zstd && (g.size & 0x80000000u) != 0) word1 |= 0x80000000u;
            if (stored != 0 && g.packed != 0) word1 |= 0x40000000u;
            const std::uint32_t words[2] = {g.offset4, word1};
            for (std::uint32_t w : words) {
                for (int shift = 24; shift >= 0; shift -= 8) out.entries.push_back(static_cast<std::uint8_t>(w >> shift));
            }
            out.max_stored = std::max(out.max_stored, stored);
            if (stored != 0) out.max_body = std::max(out.max_body, body);
            ++out.group_count;
        }
    }
    error.clear();
    return true;
}

std::uint64_t RvzImage::partition_data_disc_offset(std::size_t index) const {
    if (index >= head_.partitions.size()) return 0;
    return std::uint64_t(head_.partitions[index].data[0].first_sector) * kSector;
}

std::uint64_t RvzImage::partition_data_size(std::size_t index) const {
    if (index >= head_.partitions.size()) return 0;
    const RvzPartition& part = head_.partitions[index];
    std::uint64_t end = 0;
    for (const RvzPartData& pd : part.data) {
        if (pd.sector_count == 0) continue;
        end = std::max<std::uint64_t>(end, std::uint64_t(pd.first_sector - part.data[0].first_sector + pd.sector_count) *
                                               kSectorData);
    }
    return end;
}

std::size_t RvzImage::partition_at(std::uint64_t disc_offset) const {
    for (std::size_t p = 0; p < head_.partitions.size(); ++p) {
        if (partition_data_disc_offset(p) == disc_offset && partition_data_size(p) != 0) return p;
    }
    return SIZE_MAX;
}

bool build_rvz_stub(const RvzImage& image, RvzStub& out, std::string& error) {
    out = RvzStub{};
    const RvzRawSource raw(image);
    std::vector<PartitionEntry> table;
    if (!read_partition_table(raw, table, error)) {
        error = "the RVZ's partition table: " + error;
        return false;
    }
    std::vector<RvzStubRange> ranges;
    ranges.push_back(RvzStubRange{0, 0, 0x50000});
    for (const PartitionEntry& entry : table) {
        PartitionHeader header;
        if (!read_partition_header(raw, entry.offset, header, error)) {
            error = "partition at 0x" + std::to_string(entry.offset) + ": " + error;
            return false;
        }
        // Every partition on the disc must be one the RVZ holds data for.
        if (image.partition_at(header.data_offset) == SIZE_MAX) {
            error = "the RVZ holds no data for the partition at disc offset " + std::to_string(entry.offset);
            return false;
        }
        const std::uint64_t length = header.data_offset - entry.offset;
        if (header.data_offset <= entry.offset || length > 0x100000 || entry.offset % 0x8000 != 0) {
            error = "the partition at disc offset " + std::to_string(entry.offset) + " has an impossible header size";
            return false;
        }
        ranges.push_back(RvzStubRange{entry.offset, 0, length});
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const RvzStubRange& a, const RvzStubRange& b) { return a.disc_offset < b.disc_offset; });
    for (const RvzStubRange& r : ranges) {
        if (!out.ranges.empty()) {
            RvzStubRange& last = out.ranges.back();
            if (r.disc_offset < last.disc_offset + last.length) {
                error = "partition headers overlap on the disc";
                return false;
            }
            if (r.disc_offset == last.disc_offset + last.length) {
                last.length += r.length;
                continue;
            }
        }
        out.ranges.push_back(r);
    }
    std::uint64_t at = 0;
    for (RvzStubRange& r : out.ranges) {
        r.stub_offset = at;
        at += r.length;
    }
    out.bytes.resize(static_cast<std::size_t>(at));
    for (const RvzStubRange& r : out.ranges) {
        if (!image.read_raw(r.disc_offset, out.bytes.data() + r.stub_offset, static_cast<std::size_t>(r.length))) {
            error = "cannot read the disc at " + std::to_string(r.disc_offset) + " from the RVZ: " + image.last_error();
            return false;
        }
    }
    return true;
}

bool RvzImage::read_partition(std::size_t index, std::uint64_t offset, std::uint8_t* destination,
                              std::size_t length) const {
    if (index >= head_.partitions.size()) return fail("no partition " + std::to_string(index));
    const std::uint64_t size = partition_data_size(index);
    if (offset > size || length > size - offset) {
        return fail("partition read at " + std::to_string(offset) + " runs past its end");
    }
    const RvzPartition& part = head_.partitions[index];
    const std::uint32_t per_group = head_.chunk_size / kSector;
    const std::uint32_t lists = head_.chunk_size >= kTwoMiB ? head_.chunk_size / kTwoMiB : 1;
    while (length > 0) {
        const RvzPartData* pd = nullptr;
        std::uint64_t base = 0;
        for (const RvzPartData& candidate : part.data) {
            if (candidate.sector_count == 0) continue;
            const std::uint64_t start = std::uint64_t(candidate.first_sector - part.data[0].first_sector) * kSectorData;
            if (offset >= start && offset < start + std::uint64_t(candidate.sector_count) * kSectorData) {
                pd = &candidate;
                base = start;
                break;
            }
        }
        if (pd == nullptr) return fail("partition offset " + std::to_string(offset) + " is not stored in the image");
        const std::uint64_t group_bytes = std::uint64_t(per_group) * kSectorData;
        const std::uint64_t j = (offset - base) / group_bytes;
        const std::uint32_t sectors = std::min<std::uint32_t>(per_group, pd->sector_count - static_cast<std::uint32_t>(j) * per_group);
        const std::uint32_t payload = sectors * kSectorData;
        const std::uint64_t group_start = base + j * group_bytes;
        if (!load_group(pd->group_index + static_cast<std::uint32_t>(j), payload, group_start, lists)) return false;
        const auto within = static_cast<std::size_t>(offset - group_start);
        const std::size_t take = std::min<std::size_t>(length, payload - within);
        std::memcpy(destination, cache_.data() + within, take);
        destination += take;
        offset += take;
        length -= take;
    }
    return true;
}

}
