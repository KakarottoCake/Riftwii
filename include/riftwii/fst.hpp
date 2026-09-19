// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// One entry of a GameCube/Wii file system table. The on-disc form is twelve
// big-endian bytes: flags (0 file, 1 directory), a 24-bit name offset into
// the string table that follows the entries, then for files the disc offset
// (stored >> 2 on Wii) and the length, and for directories the parent index
// and the index of the first entry after the directory's subtree. Entry 0
// is the root; its "next" field is the total entry count.
struct FstEntry {
    bool is_directory = false;
    std::string name;            // empty for the root
    std::uint64_t offset = 0;    // files: byte offset inside the partition data
    std::uint32_t size = 0;      // files: byte length
    std::uint32_t parent = 0;    // directories: index of the enclosing directory
    std::uint32_t next = 0;      // directories: first index after the subtree
};

struct FstLimits {
    std::uint32_t max_entries = 65536;
    std::uint32_t max_depth = 64;
    std::uint32_t max_name = 255;
};

class Fst {
public:
    static constexpr std::uint32_t npos = 0xFFFFFFFFu;

    // Parses an FST image. `wii_offsets` selects the >> 2 file offset encoding.
    // Every index, name offset and nesting relation is bounds-checked; the
    // output is untouched on failure.
    static bool parse(const std::uint8_t* data, std::size_t size, bool wii_offsets,
                      Fst& out, std::string& error, const FstLimits& limits = FstLimits());

    // Writes the table back. The string table is rebuilt in entry order
    // (which is how the original tools lay it out, so an unmodified table
    // round-trips byte for byte when it was built that way).
    bool serialize(std::vector<std::uint8_t>& out, std::string& error) const;
    // Writes this table's file extents (offset and size) over `image`, the
    // FST image this table was parsed from, leaving its string table and any
    // trailing padding as they are. Fails if the image's entry count or
    // file/directory flags disagree with the table.
    bool patch_image(std::vector<std::uint8_t>& image, std::string& error) const;

    bool wii_offsets() const { return wii_offsets_; }
    const std::vector<FstEntry>& entries() const { return entries_; }
    std::uint32_t count() const { return static_cast<std::uint32_t>(entries_.size()); }

    // Absolute path ("/dir/file.bin") to entry index, or npos. Names are
    // compared byte for byte; `case_insensitive` folds ASCII letters.
    std::uint32_t find(const std::string& absolute_path, bool case_insensitive = false) const;
    // Every file (not directory) whose name matches, in table order.
    std::vector<std::uint32_t> find_files_named(const std::string& name, bool case_insensitive = false) const;
    // Direct children of a directory entry, in table order.
    bool children(std::uint32_t directory, std::vector<std::uint32_t>& out) const;
    // Absolute path of an entry ("/" for the root).
    bool path_of(std::uint32_t index, std::string& out) const;

    // Mutation used when rewriting the table for resized/created files.
    bool set_file_extent(std::uint32_t index, std::uint64_t offset, std::uint32_t size, std::string& error);
    // Appends a file at the end of `directory`'s subtree; returns its index.
    bool add_file(std::uint32_t directory, const std::string& name, std::uint64_t offset,
                  std::uint32_t size, std::uint32_t& index, std::string& error);
    // Appends an empty directory at the end of `directory`'s subtree.
    bool add_directory(std::uint32_t directory, const std::string& name, std::uint32_t& index,
                       std::string& error);
    // Creates a file at an absolute path, adding the directories along the
    // way that do not exist yet (existing ones match case-insensitively, as
    // the SDK resolves paths). Fails when the path already exists.
    bool create_file(const std::string& absolute_path, std::uint64_t offset, std::uint32_t size,
                     std::uint32_t& index, std::string& error);

private:
    bool insert_entry(std::uint32_t directory, FstEntry entry, std::uint32_t& index, std::string& error);
    std::vector<FstEntry> entries_;
    bool wii_offsets_ = true;
    FstLimits limits_;
};

}  // namespace riftwii
