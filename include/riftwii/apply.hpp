#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"
#include "riftwii/patch.hpp"

namespace riftwii {

// Abstracts "where bytes come from" so the replacement math below does not
// care whether the game lives in a host test directory, an extracted Dolphin
// filesystem dump, or (later) a Wii disc reader. Paths use the same form as
// the planner: disc paths are absolute disc paths ("/dir/file.bin") and
// sd paths are already-resolved absolute SD paths ("/riivolution/...").
class ContentProvider {
public:
    virtual ~ContentProvider() = default;
    virtual bool open_disc(const std::string& disc_path,
                           std::unique_ptr<ByteSource>& out,
                           std::string& error) = 0;
    virtual bool open_external(const std::string& sd_path,
                               std::unique_ptr<ByteSource>& out,
                               std::string& error) = 0;
};

// Maps absolute disc/sd paths onto two host (or sd:/) directory prefixes by
// plain string concatenation. Resolved planner paths never contain "..",
// so joining cannot escape the roots.
class DirectoryProvider final : public ContentProvider {
public:
    DirectoryProvider(std::string disc_root, std::string sd_root);
    bool open_disc(const std::string& disc_path, std::unique_ptr<ByteSource>& out,
                   std::string& error) override;
    bool open_external(const std::string& sd_path, std::unique_ptr<ByteSource>& out,
                       std::string& error) override;

private:
    static std::string join(const std::string& root, const std::string& abs_path);
    std::string disc_root_;
    std::string sd_root_;
};

// Owns every source a replacement view points at. ReadOverlay borrows its
// original/external pointers, so the overlay is only valid while this object
// is alive; consumers must keep the AppliedFile around while reading.
class AppliedFile {
public:
    const ByteSource& view() const;
    std::uint64_t size() const;
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t length) const;

private:
    friend bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                                  std::unique_ptr<AppliedFile>& out, std::string& error);
    std::unique_ptr<ByteSource> original_;
    std::unique_ptr<ByteSource> external_;
    std::unique_ptr<ByteSource> zero_;
    std::unique_ptr<ReadOverlay> overlay_;
};

// Builds one consumed replacement for a single planned FilePatch.
//
// Semantics (derived from public Riivolution patch-format docs and observed
// Dolphin patcher behaviour, re-implemented here in our own way):
//   effective_offset = patch.offset with the low 2 bits cleared (hardware
//       ignores them; keeps host output identical to console output).
//   external_offset  = min(patch.file_offset, external_size).
//   external_usable  = external_size - external_offset.
//   patch_size       = patch.length == 0 ? external_usable : patch.length.
//   patch range      = [effective_offset, effective_offset + patch_size).
//   target size      = resize ? patch_end : max(original_size, patch_end).
// The first min(patch_size, external_usable) bytes come from the external
// file; any remainder of the patch range is zero-filled (never original
// bytes). Gaps past the original end are zero-filled by the overlay itself.
// Missing disc file + create="true" behaves as an empty original; missing
// disc + create="false", or any missing external, is an error. All arithmetic
// is overflow-checked and outputs are capped at kMaxFileBytes.
bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                       std::unique_ptr<AppliedFile>& out, std::string& error);

}  // namespace riftwii
