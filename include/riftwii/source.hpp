// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"

namespace riftwii {

// Upper bound for any single game/external file handled by this milestone.
// Keeps host tests and Wii memory use predictable; larger files are rejected
// with a clear error instead of being partially consumed.
constexpr std::uint64_t kMaxFileBytes = 256ULL * 1024ULL * 1024ULL;

// Why an open failed. Only NotFound may be turned into "an empty original"
// by create="true"; every other status must surface as an error so that an
// unreadable or oversized disc file is never silently replaced by zeros.
enum class OpenStatus {
    Ok,
    NotFound,   // the path does not exist
    IoError,    // exists but cannot be opened/read (permissions, device error)
    TooLarge,   // exceeds kMaxFileBytes
    Invalid,    // malformed path or not a regular file
};

const char* to_string(OpenStatus status);

// In-memory byte source. Used for tests, for missing-disc "empty file"
// handling (create="true"), and as oracle data in the end-to-end test.
class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<std::uint8_t> data);
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t length) const override;

private:
    std::vector<std::uint8_t> data_;
};

// Infinite zero source used for explicit zero-fill extents. The read overlay
// only returns zeroes for regions past the original file; regions *inside*
// the original file fall back to original bytes unless covered by an extent,
// so padding (external smaller than length) needs a real zero extent.
class ZeroSource final : public ByteSource {
public:
    ZeroSource() = default;
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t length) const override;
};

// Streaming file source backed by <cstdio> so the same code builds for the
// host test rig and the Wii frontend (libfat exposes sd:/ paths through
// the same fopen/fseek/fread API). The file is opened afresh on every read
// so the object stays const-correct and re-entrant. The size comes from
// stat(), not ftell(), because `long` is 32 bits on the Wii and on this
// Windows toolchain and would wrap for files of 4 GiB and more.
class FileByteSource final : public ByteSource {
public:
    static OpenStatus open(const std::string& path, std::unique_ptr<FileByteSource>& out,
                           std::string& error);
    std::uint64_t size() const override;
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t length) const override;

private:
    FileByteSource(std::string path, std::uint64_t size);
    std::string path_;
    std::uint64_t size_;
};

}  // namespace riftwii
