// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"
#include "riftwii/patch.hpp"
#include "riftwii/source.hpp"

namespace riftwii {

// Abstracts "where bytes come from" so the replacement math below does not
// care whether the game lives in a host test directory, an extracted Dolphin
// filesystem dump, or (later) a Wii disc reader. Paths use the same form as
// the planner: disc paths are absolute disc paths ("/dir/file.bin") and
// sd paths are already-resolved absolute SD paths ("/riivolution/...").
// Implementations must return NotFound only for genuine absence; see
// OpenStatus.
class ContentProvider {
public:
    virtual ~ContentProvider() = default;
    virtual OpenStatus open_disc(const std::string& disc_path,
                                 std::unique_ptr<ByteSource>& out,
                                 std::string& error) = 0;
    virtual OpenStatus open_external(const std::string& sd_path,
                                     std::unique_ptr<ByteSource>& out,
                                     std::string& error) = 0;
};

// Maps absolute disc/sd paths onto two host (or sd:/) directory prefixes by
// plain string concatenation. Resolved planner paths never contain "..",
// so joining cannot escape the roots.
class DirectoryProvider final : public ContentProvider {
public:
    DirectoryProvider(std::string disc_root, std::string sd_root);
    OpenStatus open_disc(const std::string& disc_path, std::unique_ptr<ByteSource>& out,
                         std::string& error) override;
    OpenStatus open_external(const std::string& sd_path, std::unique_ptr<ByteSource>& out,
                             std::string& error) override;

private:
    static std::string join(const std::string& root, const std::string& abs_path);
    std::string disc_root_;
    std::string sd_root_;
};

// One contiguous piece of a fully composed replacement, after every layer
// has been resolved: bytes come from the untouched disc file, from an
// external file, or are zero. A flattened file is a sorted, gap-free list
// of these covering [0, size).
struct FlatExtent {
    enum class Kind { Original, External, Zero };
    Kind kind = Kind::Zero;
    std::uint64_t dest = 0;                 // offset inside the composed file
    std::uint64_t length = 0;
    const ByteSource* source = nullptr;     // Original: disc file; External: external file
    std::uint64_t source_offset = 0;        // offset inside `source`
};

// Owns every source a replacement view points at. ReadOverlay borrows its
// original/external pointers, so the overlay is only valid while this object
// is alive; consumers must keep the AppliedFile around while reading.
// Composed patches form a chain: each layer's overlay reads its "original"
// bytes from the layer below (base_), and only the first layer opens the
// disc file.
class AppliedFile {
public:
    const ByteSource& view() const;
    std::uint64_t size() const;
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t length) const;
    // Resolves the whole composition chain into one flat extent list. The
    // pointers stay valid for the lifetime of this AppliedFile.
    std::vector<FlatExtent> flatten() const;

private:
    AppliedFile() = default;
    static bool build(const FilePatch& patch, ContentProvider& provider,
                      std::unique_ptr<AppliedFile> base, std::unique_ptr<AppliedFile>& out,
                      std::string& error);
    friend bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                                  std::unique_ptr<AppliedFile>& out, std::string& error);
    friend bool apply_patches(const std::vector<FilePatch>& patches, ContentProvider& provider,
                              std::unique_ptr<AppliedFile>& out, std::string& error);
    std::unique_ptr<AppliedFile> base_;     // declared first: destroyed last
    std::unique_ptr<ByteSource> original_;
    std::unique_ptr<ByteSource> external_;
    std::unique_ptr<ByteSource> zero_;
    std::unique_ptr<ReadOverlay> overlay_;
};

// Builds one consumed replacement for a single planned FilePatch.
//
// Semantics (from the public patch-format documentation, cross-checked
// against Dolphin's independent implementation; DI reads take their offset
// in 4-byte words, which is why the low two bits of `offset` are ignored):
//   effective_offset = patch.offset with the low 2 bits cleared.
//   external_offset  = min(patch.file_offset, external_size).
//   external_usable  = external_size - external_offset.
//   patch_size       = patch.length == 0 ? external_usable : patch.length.
//   patch range      = [effective_offset, effective_offset + patch_size).
//   target size      = resize ? patch_end : max(original_size, patch_end).
// The first min(patch_size, external_usable) bytes come from the external
// file; any remainder of the patch range is zero-filled (never original
// bytes). Gaps past the original end are zero-filled by the overlay itself.
// A disc file reported NotFound with create="true" behaves as an empty
// original; any other open failure, and any missing external, is an error.
// All arithmetic is overflow-checked and outputs are capped at kMaxFileBytes.
bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                       std::unique_ptr<AppliedFile>& out, std::string& error);

// Applies several patches that target the same disc file, in order, each one
// seeing the result of the previous (so a later patch can overwrite bytes an
// earlier one produced, and resizes accumulate). All entries must share the
// same `disc`; the vector must not be empty. Output is untouched on failure.
bool apply_patches(const std::vector<FilePatch>& patches, ContentProvider& provider,
                   std::unique_ptr<AppliedFile>& out, std::string& error);

}  // namespace riftwii
