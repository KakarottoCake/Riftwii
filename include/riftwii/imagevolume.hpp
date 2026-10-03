// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/overlay.hpp"

namespace riftwii {

// The file systems a drive of game images may carry: FAT32 or NTFS. The
// image catalog only lists directories, reads headers and asks where a
// file's bytes lie (the device blocks d2x is given), so this read-only
// view is all it needs. Sizes are 64-bit: an ISO on NTFS may pass 4 GiB.

struct VolumeEntry {
    std::string name;  // UTF-8
    bool is_directory = false;
    std::uint64_t size = 0;  // zero for directories
};

struct VolumeFile {
    VolumeEntry entry;
    std::vector<Fragment> fragments;  // absolute 512-byte device blocks, in file order, coalesced
    // NTFS keeps a small file (up to about 700 bytes) inside its MFT record:
    // its bytes are here and it has no fragments.
    std::vector<std::uint8_t> inline_bytes;
};

class ImageVolume {
public:
    virtual ~ImageVolume() = default;
    virtual const char* kind() const = 0;  // "FAT32", "NTFS"
    // Absolute path ("/wbfs/Game [ID]/ID.wbfs"), ASCII case-insensitive.
    virtual bool lookup(const std::string& path, VolumeFile& out, std::string& error) const = 0;
    // A directory's entries, without "." and "..".
    virtual bool list(const std::string& path, std::vector<VolumeEntry>& out, std::string& error) const = 0;
    // File bytes through the fragment list; fails past the end of the file.
    virtual bool read(const VolumeFile& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const = 0;
};

// A partition the drive's MBR (or the GPT behind a protective MBR) lists:
// its first device block and how many blocks it holds.
struct DrivePartition {
    std::uint64_t lba = 0;
    std::uint64_t blocks = 0;
};
// The partitions in `mbr` (the drive's block 0), in table order (an
// extended partition's logical ones in its place), leaving out empty and
// extended entries. Empty when block 0 is not an MBR.
std::vector<DrivePartition> drive_partitions(const BlockReader& reader, const std::uint8_t* mbr);

// Mounts whichever file system the drive holds: FAT32 or NTFS, at block 0
// or in an MBR (primary or logical) or GPT partition. With several, the
// one whose top folder holds the most of `wanted` wins (earlier names
// weigh more; ASCII case-insensitive), then the first. `error` names
// what was tried when none mounts.
bool mount_image_volume(BlockReader reader, std::unique_ptr<ImageVolume>& out, std::string& error);
bool mount_image_volume(BlockReader reader, std::unique_ptr<ImageVolume>& out, std::string& error,
                        const std::vector<std::string>& wanted);

// The top folders mount_image_volume looks for by default: game folders
// first, then the mod packs.
const std::vector<std::string>& default_wanted_folders();

// A file of an ImageVolume as a ByteSource (the volume must outlive it).
class VolumeFileSource final : public ByteSource {
public:
    VolumeFileSource(const ImageVolume& volume, VolumeFile file) : volume_(volume), file_(std::move(file)) {}
    std::uint64_t size() const override { return file_.entry.size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return volume_.read(file_, offset, destination, length);
    }

private:
    const ImageVolume& volume_;
    VolumeFile file_;
};

}  // namespace riftwii
