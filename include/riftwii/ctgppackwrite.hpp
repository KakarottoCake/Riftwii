// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/ctgpefa.hpp"

// Writing files back into "efa:" (sd:/ctgpr/blob.bin), the package disk the
// CTGP-R updater changes (docs/PLUGINS.md, "The ctgpupd plugin"). The disk is a FAT32 image inside an
// AES-128-CBC file cut into independent 32 KiB chunks, so a write decrypts
// the chunks it touches, changes them and encrypts them again; nothing else
// on the card is rewritten.
//
// The image is replaced one file at a time: the new contents go into freshly
// allocated clusters, the directory entry is switched over, and the old
// cluster chain is freed. A reader therefore sees either the old file or the
// new one, never a half-written one, and a file that fails to fit leaves the
// disk as it was.
namespace riftwii {

// Raw access to blob.bin: 512-byte-aligned reads and writes at any offset.
struct CtgpPackIo {
    std::function<bool(std::uint64_t offset, std::uint32_t length, std::uint8_t* out)> read;
    std::function<bool(std::uint64_t offset, const std::uint8_t* data, std::uint32_t length)> write;
};

// The largest file the updater puts back into a pack.
constexpr std::uint32_t kCtgpPackMaxFileBytes = 64u * 1024u * 1024u;

class CtgpPackWriter {
public:
    CtgpPackWriter();
    ~CtgpPackWriter();
    CtgpPackWriter(const CtgpPackWriter&) = delete;
    CtgpPackWriter& operator=(const CtgpPackWriter&) = delete;

    // Checks the boot sector and every structure the writes rely on. On
    // success `error` is empty.
    bool open(const CtgpPackIo& io, std::uint64_t size, std::string& error);
    // The same for a disk with another key: jelly.bin ("efb:"), whose key
    // comes from its own FAT times (ctgp_efb_key). Used to read its
    // /PACKAGES.BIN.
    bool open(const CtgpPackIo& io, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
              const CtgpChunkIv& iv, std::string& error);
    void close();

    // Replaces the file at an absolute path ("/packages/ctgpr/race/course/
    // C8.szs", case-insensitive) with `data`. The file must exist; the new
    // contents may be shorter or longer than the old ones. The directory
    // entry keeps its name and times.
    bool replace_file(const std::string& path, const std::uint8_t* data, std::size_t size, std::string& error);
    // The same, taking the new contents from `read` (offset, out, length)
    // one cluster at a time, so they need not be in one block of memory.
    using Reader = std::function<bool(std::size_t offset, std::uint8_t* out, std::size_t length)>;
    bool replace_file(const std::string& path, std::size_t size, const Reader& read, std::string& error);

    // Reads a file out of the disk, for checking a write or for the bases the
    // updater rebuilds from. `out` is resized to the file's size.
    bool read_file(const std::string& path, std::vector<std::uint8_t>& out, std::string& error);
    // The same, handing the file to `sink` one cluster at a time; `size`
    // (may be null) gets the file's length before the first piece.
    using Sink = std::function<bool(const std::uint8_t* data, std::size_t length)>;
    bool read_file(const std::string& path, const Sink& sink, std::uint32_t* size, std::string& error);

    // Free clusters, as the FAT says before any pending write is flushed.
    std::uint32_t free_clusters() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace riftwii
