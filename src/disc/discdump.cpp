// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Kwiirk (libwbfs, the WBFS layout)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/discdump.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/disc.hpp"
#include "riftwii/dol.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/wiicrypt.hpp"

namespace riftwii {
namespace {

// A file table bigger than this is not read: the partition is kept whole.
// The largest retail ones are well under 2 MiB.
constexpr std::uint64_t kMaxFstBytes = 32u << 20;
// The H3 hash table in front of a partition's data.
constexpr std::uint64_t kH3Bytes = 0x18000;
// The disc's own header, partition table and region settings: all in the
// first 0x50000 bytes, which the drive reads without a partition open.
constexpr std::uint64_t kSystemAreaBytes = 0x50000;
constexpr std::uint64_t kDualLayerBytes = std::uint64_t(kWiiDualLayerSectors) * kWiiSectorBytes;
// A pressed dual-layer Wii disc's real end, 0x1FB4E0000 bytes: libwbfs's
// table is sized for a little more.
constexpr std::uint32_t kWiiDualLayerRealSectors = 0x1FB4E0000ull / kWiiSectorBytes;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// Marks the raw disc bytes [offset, offset + length) as used, clipped to a
// dual-layer disc.
void mark_raw(DiscUsage& usage, std::uint64_t offset, std::uint64_t length) {
    if (length == 0 || offset >= kDualLayerBytes) return;
    const std::uint64_t end = std::min(kDualLayerBytes, offset + std::min(length, kDualLayerBytes));
    for (std::uint64_t sector = offset / kWiiSectorBytes; sector * kWiiSectorBytes < end; ++sector) {
        usage.sectors[static_cast<std::size_t>(sector)] = true;
    }
}

// One partition's decrypted data, and the clusters behind it.
class PartitionMarker {
public:
    PartitionMarker(DiscUsage& usage, const PartitionHeader& header, std::uint64_t data_bytes)
        : usage_(usage), header_(header), data_bytes_(data_bytes) {}

    // Marks the clusters that hold the decrypted bytes [offset, offset +
    // length). False when the range lies outside the partition's data.
    bool mark(std::uint64_t offset, std::uint64_t length) {
        if (length == 0) return true;
        if (offset >= data_bytes_ || length > data_bytes_ - offset) return false;
        const std::uint64_t first = offset / kClusterDataBytes;
        const std::uint64_t last = (offset + length - 1) / kClusterDataBytes;
        for (std::uint64_t cluster = first; cluster <= last; ++cluster) {
            mark_raw(usage_, header_.data_offset + cluster * kClusterBytes, kClusterBytes);
        }
        return true;
    }

private:
    DiscUsage& usage_;
    const PartitionHeader& header_;
    std::uint64_t data_bytes_;
};

// Marks what the partition's files need, through its decrypted data; false
// with `why` when they cannot be read, and the caller keeps it whole.
bool mark_partition_files(const ByteSource& disc, const PartitionHeader& header, const std::uint8_t* common_key,
                          DiscUsage& usage, std::string& why) {
    if (common_key == nullptr) {
        why = "this Wii's disc key could not be read";
        return false;
    }
    std::uint8_t ticket[kTicketBytes];
    if (!disc.read(header.offset, ticket, sizeof(ticket))) {
        why = "its ticket could not be read";
        return false;
    }
    std::uint8_t title_key[16];
    if (!decrypt_title_key(ticket, sizeof(ticket), common_key, title_key, why)) return false;
    const WiiPartitionSource data(disc, header.data_offset, header.data_size, title_key);
    std::memset(title_key, 0, sizeof(title_key));
    PartitionMarker marker(usage, header, data.size());

    // Boot header, its second half (bi2) and the apploader's header: the
    // apploader's own size follows from it.
    std::uint8_t boot[0x480];
    std::uint8_t apploader[0x20];
    if (!data.read(0, boot, sizeof(boot)) || !data.read(kApploaderOffset, apploader, sizeof(apploader))) {
        why = "its boot header could not be read (wrong key?)";
        return false;
    }
    if (be32(boot + 0x18) != kWiiMagic) {
        why = "its boot header has no Wii disc mark (wrong key?)";
        return false;
    }
    const std::uint64_t apploader_bytes =
        kApploaderHeaderBytes + std::uint64_t(be32(apploader + 0x14)) + be32(apploader + 0x18);
    if (!marker.mark(0, kApploaderOffset) || !marker.mark(kApploaderOffset, apploader_bytes)) {
        why = "its apploader lies outside its data";
        return false;
    }

    // The main program, as far as its last section reaches.
    const std::uint64_t dol_offset = std::uint64_t(be32(boot + 0x420)) << 2;
    std::uint8_t dol[kDolHeaderBytes];
    if (dol_offset != 0) {
        if (!data.read(dol_offset, dol, sizeof(dol))) {
            why = "its main program could not be read";
            return false;
        }
        std::uint64_t dol_bytes = kDolHeaderBytes;
        for (std::size_t i = 0; i < kDolSections; ++i) {
            const std::uint64_t section_offset = be32(dol + 4 * i);
            const std::uint64_t section_size = be32(dol + 0x90 + 4 * i);
            if (section_size != 0) dol_bytes = std::max(dol_bytes, section_offset + section_size);
        }
        if (!marker.mark(dol_offset, dol_bytes)) {
            why = "its main program lies outside its data";
            return false;
        }
    }

    // The file table, then every file it names.
    const std::uint64_t fst_offset = std::uint64_t(be32(boot + 0x424)) << 2;
    const std::uint64_t fst_bytes = std::uint64_t(be32(boot + 0x428)) << 2;
    if (fst_offset == 0 || fst_bytes == 0) return true;  // no files (some update partitions)
    if (fst_bytes > kMaxFstBytes) {
        why = "its file table is implausibly large";
        return false;
    }
    if (!marker.mark(fst_offset, fst_bytes)) {
        why = "its file table lies outside its data";
        return false;
    }
    std::vector<std::uint8_t> table;
    if (!try_resize(table, static_cast<std::size_t>(fst_bytes))) {
        why = "there is no memory for its file table";
        return false;
    }
    if (!data.read(fst_offset, table.data(), table.size())) {
        why = "its file table could not be read";
        return false;
    }
    FstLimits limits;
    limits.max_entries = static_cast<std::uint32_t>(fst_bytes / 12);
    Fst fst;
    if (!Fst::parse(table.data(), table.size(), true, fst, why, limits)) {
        why = "its file table does not parse: " + why;
        return false;
    }
    for (const FstEntry& entry : fst.entries()) {
        if (entry.is_directory) continue;
        if (!marker.mark(entry.offset, entry.size)) {
            why = "a file in it lies outside its data";
            return false;
        }
    }
    return true;
}

// A partition kept whole: everything from its start to the end of its data.
void mark_partition_whole(const PartitionHeader& header, DiscUsage& usage) {
    mark_raw(usage, header.offset, header.data_offset - header.offset);
    mark_raw(usage, header.data_offset, header.data_size);
}

}  // namespace

std::uint32_t DiscUsage::used_sectors() const {
    return static_cast<std::uint32_t>(std::count(sectors.begin(), sectors.end(), true));
}

bool map_disc_usage(const ByteSource& disc, const std::uint8_t* common_key, DiscUsage& out, std::string& error) {
    DiscHeader disc_header;
    if (!read_disc_header(disc, disc_header, error)) return false;
    if (!disc_header.wii_magic) {
        error = "the disc is not a Wii disc";
        return false;
    }
    std::vector<PartitionEntry> table;
    if (!read_partition_table(disc, table, error)) return false;

    DiscUsage usage;
    usage.sectors.assign(kWiiDualLayerSectors, false);
    mark_raw(usage, 0, kSystemAreaBytes);
    for (const PartitionEntry& entry : table) {
        DiscUsagePartition part;
        part.offset = entry.offset;
        part.type = entry.type;
        part.group = entry.group;
        PartitionHeader header;
        std::string why;
        if (!read_partition_header(disc, entry.offset, header, why)) {
            // Without its header there is no telling where the partition
            // ends: the copy cannot be whole, so it is not made.
            error = "partition at " + std::to_string(entry.offset) + " cannot be read: " + why;
            return false;
        }
        // The ticket, TMD, certificates and H3 table lie in front of the
        // data; each is marked on its own as well, wherever it is.
        mark_raw(usage, header.offset, header.data_offset - header.offset);
        mark_raw(usage, header.tmd_offset, header.tmd_size);
        mark_raw(usage, header.cert_offset, header.cert_size);
        mark_raw(usage, header.h3_offset, kH3Bytes);
        if (!mark_partition_files(disc, header, common_key, usage, why)) {
            part.whole = true;
            part.why = why;
            mark_partition_whole(header, usage);
        }
        usage.partitions.push_back(part);
    }
    out = std::move(usage);
    error.clear();
    return true;
}

bool plan_wbfs_image(const DiscUsage& usage, WbfsImagePlan& out, std::string& error) {
    if (usage.sectors.size() != kWiiDualLayerSectors) {
        error = "the disc's usage map has the wrong size";
        return false;
    }
    WbfsImagePlan plan;
    plan.block_table.assign(kWbfsFileDiscBlocks, 0);
    for (std::uint32_t block = 0; block < kWbfsFileDiscBlocks; ++block) {
        const auto first = usage.sectors.begin() + std::size_t(block) * kWbfsSectorsPerBlock;
        if (std::find(first, first + kWbfsSectorsPerBlock, true) == first + kWbfsSectorsPerBlock) continue;
        plan.stored.push_back(block);
        // Block 0 of the file is its header: the disc's blocks follow it.
        plan.block_table[block] = static_cast<std::uint16_t>(plan.stored.size());
    }
    if (plan.stored.empty()) {
        error = "nothing on the disc is used";
        return false;
    }
    plan.file_bytes = (std::uint64_t(plan.stored.size()) + 1) * kWbfsFileBlockBytes;
    out = std::move(plan);
    error.clear();
    return true;
}

std::vector<std::uint8_t> wbfs_image_head(const std::uint8_t disc_header[0x100], const WbfsImagePlan& plan) {
    const std::size_t table_bytes = std::size_t(kWbfsFileDiscBlocks) * 2;
    const std::size_t info_bytes = (0x100 + table_bytes + 511) / 512 * 512;
    std::vector<std::uint8_t> head(512 + info_bytes, 0);
    std::memcpy(head.data(), "WBFS", 4);
    // The 512-byte sectors the file holds: libwbfs's wbfs_trim leaves the
    // count at the end of the last stored block.
    put_be32(head.data() + 4, static_cast<std::uint32_t>(plan.file_bytes / 512));
    head[8] = 9;  // 512-byte sectors
    head[9] = kWbfsFileBlockShift;
    head[12] = 1;  // slot 0 holds a disc
    std::memcpy(head.data() + 512, disc_header, 0x100);
    std::uint8_t* table = head.data() + 512 + 0x100;
    for (std::size_t i = 0; i < plan.block_table.size() && i < kWbfsFileDiscBlocks; ++i) {
        table[2 * i] = static_cast<std::uint8_t>(plan.block_table[i] >> 8);
        table[2 * i + 1] = static_cast<std::uint8_t>(plan.block_table[i]);
    }
    return head;
}

std::string wbfs_piece_path(const std::string& primary, unsigned index) {
    if (index == 0) return primary;
    const std::size_t dot = primary.rfind('.');
    const std::string stem = dot == std::string::npos ? primary : primary.substr(0, dot);
    return stem + ".wbf" + std::to_string(index);
}

unsigned wbfs_piece_count(std::uint64_t file_bytes, std::uint64_t split_bytes) {
    if (file_bytes == 0 || split_bytes == 0) return 1;
    return static_cast<unsigned>((file_bytes + split_bytes - 1) / split_bytes);
}

namespace {

bool valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        const std::size_t length = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size()) return false;
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += length;
    }
    return true;
}

}  // namespace

std::string wbfs_game_folder_name(const std::string& title, const std::string& id6) {
    // A Japanese disc's header title is Shift-JIS, whose second bytes can be
    // a backslash or '|': made UTF-8 first, so no character is cut in two below.
    const std::string text = valid_utf8(title) ? title : gc_card_text_utf8(title, true);
    std::string clean;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::strchr("\"*/:<>?\\|", c) != nullptr) {
            clean += '_';
        } else {
            clean += c;
        }
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    const std::size_t start = clean.find_first_not_of(' ');
    clean = start == std::string::npos ? std::string() : clean.substr(start);
    // A FAT32 name holds 255 characters; the ID and brackets take nine.
    if (clean.size() > 200) {
        std::size_t cut = 200;
        while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80) --cut;
        clean.resize(cut);
        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    }
    if (clean.empty()) return id6;
    return clean + " [" + id6 + "]";
}

bool write_wbfs_image(const ByteSource& disc, const WbfsImagePlan& plan, const DiscUsage& usage, WbfsSink& sink,
                      const DiscCopyProgress& progress, std::string& error) {
    std::uint8_t disc_header[0x100];
    if (!disc.read(0, disc_header, sizeof(disc_header))) {
        error = "the disc's header could not be read";
        return false;
    }
    // 32-byte aligned, so the Wii's drive can read straight into it.
    std::vector<std::uint8_t> storage;
    if (!try_resize(storage, std::size_t(kWbfsFileBlockBytes) + 32)) {
        error = "there is no memory for a 2 MiB copy buffer";
        return false;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(storage.data());
    std::uint8_t* const block = storage.data() + ((32 - (start & 31)) & 31);
    // The file's first block: header, slot info, then zeros.
    const std::vector<std::uint8_t> head = wbfs_image_head(disc_header, plan);
    std::memset(block, 0, kWbfsFileBlockBytes);
    std::memcpy(block, head.data(), head.size());
    if (!sink.write(block, kWbfsFileBlockBytes, error)) return false;

    const std::uint64_t total = std::uint64_t(plan.stored.size()) * kWbfsFileBlockBytes;
    std::uint64_t done = 0;
    if (progress && !progress(done, total)) {
        error = "the copy was stopped";
        return false;
    }
    // Where the disc can end: a disc that needs nothing past the first
    // layer is taken as single-layer, and its blocks are not read past
    // there (a drive can take a while to refuse each such read); a
    // dual-layer one ends at 8.5 GB.
    const std::uint32_t needed_end = static_cast<std::uint32_t>(
        std::find(usage.sectors.rbegin(), usage.sectors.rend(), true).base() - usage.sectors.begin());
    const std::uint32_t readable_end = needed_end <= kWiiSingleLayerSectors
        ? kWiiSingleLayerSectors
        : std::max(kWiiDualLayerRealSectors, needed_end);
    for (const std::uint32_t disc_block : plan.stored) {
        const std::uint32_t first_sector = disc_block * kWbfsSectorsPerBlock;
        const std::uint32_t readable = readable_end <= first_sector
            ? 0
            : std::min(kWbfsSectorsPerBlock, readable_end - first_sector);
        std::memset(block + std::size_t(readable) * kWiiSectorBytes, 0,
                    std::size_t(kWbfsSectorsPerBlock - readable) * kWiiSectorBytes);
        // The block in one read; when that fails, sector by sector, so a
        // sector that no one needs and does not read becomes zeros and
        // only a needed one stops the copy.
        if (readable != 0 &&
            !disc.read(std::uint64_t(first_sector) * kWiiSectorBytes, block, std::size_t(readable) * kWiiSectorBytes)) {
            for (std::uint32_t i = 0; i < readable; ++i) {
                const std::uint32_t sector = first_sector + i;
                std::uint8_t* at = block + std::size_t(i) * kWiiSectorBytes;
                if (disc.read(std::uint64_t(sector) * kWiiSectorBytes, at, kWiiSectorBytes)) continue;
                if (usage.sectors[sector]) {
                    error = "the disc could not be read at sector " + std::to_string(sector) + " (" +
                            std::to_string((std::uint64_t(sector) * kWiiSectorBytes) >> 20) + " MiB)";
                    return false;
                }
                std::memset(at, 0, kWiiSectorBytes);
            }
        }
        if (!sink.write(block, kWbfsFileBlockBytes, error)) return false;
        done += kWbfsFileBlockBytes;
        if (progress && !progress(done, total)) {
            error = "the copy was stopped";
            return false;
        }
    }
    error.clear();
    return true;
}

}  // namespace riftwii
