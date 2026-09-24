// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"
#include "rtrvz.h"

namespace riftwii {

// RVZ disc images as Dolphin writes them (format: Dolphin's
// docs/WiaAndRvz.md). WIA, the format RVZ grew out of, is recognised only
// to be turned away with a clear message.
//
// A game reads its partition decrypted and without hashes, and that is
// how RVZ stores it, so a reader never needs to encrypt or hash anything.

enum class RvzMethod : std::uint32_t { None = 0, Purge = 1, Bzip2 = 2, Lzma = 3, Lzma2 = 4, Zstd = 5 };
const char* to_string(RvzMethod method);

struct RvzPartData {
    std::uint32_t first_sector = 0;  // 0x8000-byte sectors on the disc
    std::uint32_t sector_count = 0;
    std::uint32_t group_index = 0;
    std::uint32_t group_count = 0;
};

struct RvzPartition {
    std::array<std::uint8_t, 16> title_key{};  // already decrypted
    RvzPartData data[2];                        // boot to FST, then the rest
};

// The headers stored uncompressed at the start of the file: enough to
// decide whether RiftWii can play it (check_rvz) without decoding anything.
struct RvzHead {
    bool wia = false;  // "WIA\1" rather than "RVZ\1"
    std::uint32_t version = 0;
    std::uint32_t version_compatible = 0;
    std::uint64_t iso_size = 0;
    std::uint64_t file_size = 0;
    std::uint32_t disc_type = 0;  // 1 GameCube, 2 Wii
    std::uint32_t method = 0;     // RvzMethod, or an unknown value
    std::int32_t level = 0;       // signed for RVZ (Zstandard has negative levels)
    std::uint32_t chunk_size = 0;
    std::array<std::uint8_t, 0x80> disc_header{};
    std::uint64_t raw_table_offset = 0;
    std::uint32_t raw_count = 0;
    std::uint32_t raw_table_bytes = 0;  // as stored (compressed)
    std::uint64_t group_table_offset = 0;
    std::uint32_t group_count = 0;
    std::uint32_t group_table_bytes = 0;
    std::vector<RvzPartition> partitions;
};

// Reads and checks the headers: the magic, sizes and the three SHA-1s they
// carry. Fails for anything that is not a whole WIA or RVZ header.
bool read_rvz_head(const ByteSource& file, RvzHead& out, std::string& error);

// What RiftWii will do with an image. The limits below are guards: a game
// either plays as tested, plays with a warning ("at your own risk"), or is
// refused before anything is loaded. Chunk sizes cost the in-game runtime
// a buffer each and a whole chunk's decompression for every read outside
// the cached one, so they are the main limit. Zstandard's level does not
// change what decompressing costs here (Super Mario Galaxy 2 at levels 5
// and 22 decodes at the same speed, and a chunk never needs a window
// larger than itself); most RVZs in the wild use 22, the highest. A level
// outside Zstandard's range means a writer RiftWii does not know.
enum class RvzSupport { Supported, AtOwnRisk, Unsupported };

constexpr std::uint32_t kRvzVersion = 0x01000000;         // 1.00, the format this reader implements
constexpr std::uint32_t kRvzOldestVersion = 0x00030000;   // earlier RVZs predate the format's release
constexpr std::uint32_t kRvzTestedChunk = 128 * 1024;     // Dolphin's default
constexpr std::uint32_t kRvzMaxChunk = 512 * 1024;
constexpr std::int32_t kRvzMaxZstdLevel = 22;

struct RvzVerdict {
    RvzSupport support = RvzSupport::Supported;
    // One sentence per reason, for the menu and the log: why it is
    // refused, or else why it is at the player's own risk.
    std::vector<std::string> reasons;
};

// `file_bytes` is the size of the file on the card, to catch a copy that
// did not finish.
RvzVerdict check_rvz(const RvzHead& head, std::uint64_t file_bytes);

// "RVZ 1.00, Zstandard level 5, 128 KiB chunks", for the log.
std::string describe_rvz(const RvzHead& head);

// What the in-game runtime needs to serve one partition's data from the
// RVZ (runtime/resident/rt_hook.h: rt_rvz_state). Sizes in KiB, as there.
struct RvzRuntimeTable {
    std::uint32_t data_kib = 0;
    std::uint32_t group_kib = 0;
    std::uint32_t split_kib = 0;     // where the second segment's groups start
    std::uint32_t first_groups = 0;  // groups before it
    std::uint32_t group_count = 0;
    std::uint32_t lists = 0;         // exception lists at the start of each group
    std::uint32_t max_stored = 0;    // the largest group as stored in the file
    std::uint32_t max_body = 0;      // the largest group's data after its exception lists
    // 8 bytes per group in data order, big endian: the file offset / 4,
    // then the stored size | 0x80000000 when Zstandard-compressed |
    // 0x40000000 when RVZ-packed.
    std::vector<std::uint8_t> entries;
};

// Reads a supported RVZ (Zstandard or no compression; any chunk size the
// format allows, whatever check_rvz says about it). The last group read is
// kept decoded, so small reads in a row cost one decode. Not thread safe.
class RvzImage {
public:
    static bool open(std::shared_ptr<const ByteSource> file, std::unique_ptr<RvzImage>& out, std::string& error);
    ~RvzImage();

    const RvzHead& head() const { return head_; }

    // The disc outside partition data (header, partition tables, tickets,
    // TMDs, H3 tables, padding), by disc offset. Partition data is not
    // stored in its encrypted form, so reads that touch it fail.
    bool read_raw(std::uint64_t disc_offset, std::uint8_t* destination, std::size_t length) const;

    // Partition `index`'s data as a game reads it: decrypted, without the
    // hashes, 0x7C00 bytes for every 0x8000 on the disc.
    bool read_partition(std::size_t index, std::uint64_t offset, std::uint8_t* destination, std::size_t length) const;
    std::uint64_t partition_data_size(std::size_t index) const;
    // Where the partition's encrypted data starts on the disc.
    std::uint64_t partition_data_disc_offset(std::size_t index) const;
    // The partition whose encrypted data starts at `disc_offset`, or
    // SIZE_MAX.
    std::size_t partition_at(std::uint64_t disc_offset) const;
    // Partition `index`'s groups for the in-game runtime. Fails for a
    // layout it cannot describe (segments that are not back to back).
    bool runtime_table(std::size_t index, RvzRuntimeTable& out, std::string& error) const;

    const std::string& last_error() const { return error_; }

private:
    struct Raw {
        std::uint64_t start = 0;  // rounded down to 0x8000
        std::uint64_t end = 0;
        std::uint32_t group_index = 0;
        std::uint32_t group_count = 0;
    };
    struct Group {
        std::uint32_t offset4 = 0;
        std::uint32_t size = 0;  // with the "compressed" bit
        std::uint32_t packed = 0;
    };
    RvzImage() = default;
    // Decodes group `index` into cache_ unless it is there already.
    // `payload` is its size decoded, `data_offset` where its first byte
    // sits for the padding generator, `lists` its exception lists.
    bool load_group(std::uint32_t index, std::uint32_t payload, std::uint64_t data_offset, std::uint32_t lists) const;
    bool decode(std::uint64_t file_offset, std::uint32_t stored, bool compressed, std::uint32_t expected,
                std::vector<std::uint8_t>& out) const;
    bool fail(std::string message) const;

    std::shared_ptr<const ByteSource> file_;
    RvzHead head_;
    std::vector<Raw> raw_;
    std::vector<Group> groups_;
    void* dctx_ = nullptr;
    mutable std::uint32_t cached_ = UINT32_MAX;
    mutable std::vector<std::uint8_t> cache_;
    mutable std::vector<std::uint8_t> stored_;
    mutable std::vector<std::uint8_t> unpacked_;
    mutable rtrvz_junk junk_{};
    mutable std::string error_;
};

// The disc outside partition data, as large as the disc, for the parsers
// in riftwii/disc.hpp. Reads that touch partition data fail.
class RvzRawSource final : public ByteSource {
public:
    explicit RvzRawSource(const RvzImage& image) : image_(image) {}
    std::uint64_t size() const override { return image_.head().iso_size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return image_.read_raw(offset, destination, length);
    }

private:
    const RvzImage& image_;
};

// One partition's data, decrypted and without hashes.
class RvzPartitionSource final : public ByteSource {
public:
    RvzPartitionSource(const RvzImage& image, std::size_t index) : image_(image), index_(index) {}
    std::uint64_t size() const override { return image_.partition_data_size(index_); }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return image_.read_partition(index_, offset, destination, length);
    }

private:
    const RvzImage& image_;
    std::size_t index_;
};

// What d2x must find on the card to open an RVZ game's partitions: the
// disc's first 0x50000 bytes (header, partition table, region) and each
// partition's header (ticket, TMD, certificates, H3 table), stored back
// to back in `bytes`. Everything else d2x reads as zeros; the game's data
// is served from the RVZ instead.
struct RvzStubRange {
    std::uint64_t disc_offset = 0;
    std::uint64_t stub_offset = 0;
    std::uint64_t length = 0;
};

struct RvzStub {
    std::vector<std::uint8_t> bytes;
    std::vector<RvzStubRange> ranges;  // sorted, 512-byte aligned
};

bool build_rvz_stub(const RvzImage& image, RvzStub& out, std::string& error);

}
