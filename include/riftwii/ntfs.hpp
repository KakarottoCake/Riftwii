// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/imagevolume.hpp"

namespace riftwii {

// Read-only NTFS walker for drives of game images, written from the public
// on-disk format descriptions (boot sector, MFT records with update
// sequence fixups, attributes and run lists, $ATTRIBUTE_LIST, the $I30
// directory index B-tree). It lists directories, resolves paths and turns a
// file into the device blocks that hold it, which is what d2x's fragment
// mode needs; it never writes. Compressed, encrypted and sparse files are
// refused: d2x reads raw sectors and cannot expand them. Every record,
// attribute, run and index node is bounds-checked, and directory walks are
// bounded in depth and in index blocks visited.

struct NtfsGeometry {
    std::uint64_t volume_lba = 0;       // device block of the boot sector
    std::uint32_t bytes_per_sector = 0;
    std::uint32_t sectors_per_cluster = 0;
    std::uint64_t cluster_bytes = 0;
    std::uint32_t mft_record_bytes = 0;
    std::uint32_t index_block_bytes = 0;
    std::uint64_t total_clusters = 0;
    std::uint64_t mft_lcn = 0;
};

// One run of an attribute's data: `length` clusters from virtual cluster
// `vcn`, stored at logical cluster `lcn` (absent when `sparse`).
struct NtfsRun {
    std::uint64_t vcn = 0;
    std::uint64_t lcn = 0;
    std::uint64_t length = 0;
    bool sparse = false;
};

// Decodes an NTFS mapping-pairs array (the run list) starting at virtual
// cluster `first_vcn`. Exposed for tests.
bool ntfs_decode_runs(const std::uint8_t* pairs, std::size_t size, std::uint64_t first_vcn,
                      std::vector<NtfsRun>& out, std::string& error);

class NtfsVolume final : public ImageVolume {
public:
    // True when the 512 bytes look like an NTFS boot sector (OEM id "NTFS").
    static bool is_boot_sector(const std::uint8_t* block);
    // Mounts the volume whose boot sector is at device block `volume_lba`.
    static bool mount_at(BlockReader reader, std::uint64_t volume_lba, NtfsVolume& out, std::string& error);

    const NtfsGeometry& geometry() const { return geo_; }

    const char* kind() const override { return "NTFS"; }
    bool lookup(const std::string& path, VolumeFile& out, std::string& error) const override;
    bool list(const std::string& path, std::vector<VolumeEntry>& out, std::string& error) const override;
    bool read(const VolumeFile& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const override;

private:
    struct DirEntry {
        VolumeEntry entry;
        std::uint64_t record = 0;
    };
    struct AttrData {
        bool resident = false;
        std::vector<std::uint8_t> value;  // resident value
        std::vector<NtfsRun> runs;        // non-resident extents, in vcn order
        std::uint64_t data_size = 0;
        std::uint16_t flags = 0;
    };

    bool read_device(std::uint64_t byte_offset, std::size_t length, std::uint8_t* out) const;
    bool read_runs(const std::vector<NtfsRun>& runs, std::uint64_t byte_offset, std::size_t length,
                   std::uint8_t* out, std::string& error) const;
    bool read_record(std::uint64_t record, std::vector<std::uint8_t>& out, std::string& error) const;
    bool find_attribute(std::uint64_t record_number, const std::vector<std::uint8_t>& record, std::uint32_t type,
                        const std::u16string& name, AttrData& out, bool& found, std::string& error) const;
    bool walk_directory(std::uint64_t record_number, std::vector<DirEntry>& out, std::string& error) const;
    bool resolve(const std::string& path, DirEntry& out, std::string& error) const;
    bool file_of(const DirEntry& entry, VolumeFile& out, std::string& error) const;

    BlockReader reader_;
    NtfsGeometry geo_;
    std::vector<NtfsRun> mft_runs_;
};

}  // namespace riftwii
