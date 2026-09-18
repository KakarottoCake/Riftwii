#include "riftwii/apply.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

#include "riftwii/source.hpp"

namespace riftwii {

DirectoryProvider::DirectoryProvider(std::string disc_root, std::string sd_root)
    : disc_root_(std::move(disc_root)), sd_root_(std::move(sd_root)) {}

std::string DirectoryProvider::join(const std::string& root, const std::string& abs_path) {
    std::string r = root;
    while (r.size() > 1 && (r.back() == '/' || r.back() == '\\')) {
        r.pop_back();
    }
    if (r.empty()) r = ".";
    return r + abs_path;  // abs_path always starts with '/'.
}

bool DirectoryProvider::open_disc(const std::string& disc_path,
                                  std::unique_ptr<ByteSource>& out, std::string& error) {
    if (disc_path.empty() || disc_path[0] != '/') {
        error = "disc path must be absolute '" + disc_path + "'";
        return false;
    }
    std::unique_ptr<FileByteSource> f;
    if (!FileByteSource::open(join(disc_root_, disc_path), f, error)) {
        error = "disc file not found '" + disc_path + "': " + error;
        return false;
    }
    out = std::move(f);
    return true;
}

bool DirectoryProvider::open_external(const std::string& sd_path,
                                      std::unique_ptr<ByteSource>& out, std::string& error) {
    if (sd_path.empty()) {
        error = "external path empty";
        return false;
    }
    // Planned patches are always resolved to absolute SD paths, but accept a
    // bare relative name defensively by treating it as root-relative.
    std::string abs = sd_path;
    if (abs[0] != '/') abs = "/" + abs;
    std::unique_ptr<FileByteSource> f;
    if (!FileByteSource::open(join(sd_root_, abs), f, error)) {
        error = "external file not found '" + sd_path + "': " + error;
        return false;
    }
    out = std::move(f);
    return true;
}

const ByteSource& AppliedFile::view() const {
    return *overlay_;
}

std::uint64_t AppliedFile::size() const {
    return overlay_->size();
}

bool AppliedFile::read(std::uint64_t offset, std::uint8_t* destination,
                       std::size_t length) const {
    return overlay_->read(offset, destination, length);
}

bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                       std::unique_ptr<AppliedFile>& out, std::string& error) {
    try {
        if (patch.disc.empty() || patch.disc[0] != '/') {
            error = "file disc must be absolute";
            return false;
        }
        if (patch.external.empty()) {
            error = "file external empty";
            return false;
        }

        // Real Riivolution hardware ignores the low two bits of the file
        // offset; Dolphin adopted the same masking to match console output.
        // Apply it here so host results equal on-console results.
        const std::uint64_t patch_start = patch.offset & ~std::uint64_t(3);

        std::unique_ptr<ByteSource> original;
        std::uint64_t orig_size = 0;
        {
            std::unique_ptr<ByteSource> opened;
            std::string open_err;
            if (provider.open_disc(patch.disc, opened, open_err)) {
                orig_size = opened->size();
                if (orig_size > kMaxFileBytes) {
                    error = "disc file too large '" + patch.disc + "'";
                    return false;
                }
                original = std::move(opened);
            } else if (patch.create) {
                original.reset(new MemorySource(std::vector<std::uint8_t>()));
                orig_size = 0;
            } else {
                error = open_err.empty() ? ("disc file not found '" + patch.disc + "'")
                                         : open_err;
                return false;
            }
        }

        std::unique_ptr<ByteSource> external;
        {
            std::string open_err;
            if (!provider.open_external(patch.external, external, open_err)) {
                error = open_err.empty() ? ("external file not found '" + patch.external + "'")
                                         : open_err;
                return false;
            }
        }
        const std::uint64_t ext_raw = external->size();
        if (ext_raw > kMaxFileBytes) {
            error = "external file too large '" + patch.external + "'";
            return false;
        }

        const std::uint64_t ext_off =
            patch.file_offset > ext_raw ? ext_raw : patch.file_offset;
        const std::uint64_t ext_usable = ext_raw - ext_off;
        const std::uint64_t patch_size =
            patch.length == 0 ? ext_usable : patch.length;

        if (patch_size > kMaxFileBytes) {
            error = "patch range too large for '" + patch.disc + "'";
            return false;
        }
        if (patch_size > std::numeric_limits<std::uint64_t>::max() - patch_start) {
            error = "patch range overflows for '" + patch.disc + "'";
            return false;
        }
        const std::uint64_t patch_end = patch_start + patch_size;
        const std::uint64_t target =
            patch.resize ? patch_end : std::max(orig_size, patch_end);
        if (target > kMaxFileBytes) {
            error = "replacement too large for '" + patch.disc + "'";
            return false;
        }

        const std::uint64_t copy_len = std::min(patch_size, ext_usable);

        std::unique_ptr<AppliedFile> applied(new AppliedFile());
        applied->original_ = std::move(original);
        applied->external_ = std::move(external);
        applied->overlay_.reset(new ReadOverlay(*applied->original_, target));

        if (copy_len > 0) {
            OverlayExtent ext{patch_start, ext_off, copy_len, applied->external_.get()};
            if (!applied->overlay_->append(ext, error)) {
                error = "cannot stage external bytes for '" + patch.disc + "': " + error;
                return false;
            }
        }
        if (patch_size > copy_len) {
            // Zero-fill the remainder of the patch window. This must be an
            // explicit extent: inside the original file the overlay would
            // otherwise fall back to original bytes, but the spec (and
            // Dolphin) requires zeroes when the external is short.
            applied->zero_.reset(new ZeroSource());
            OverlayExtent pad{patch_start + copy_len, 0, patch_size - copy_len,
                              applied->zero_.get()};
            if (!applied->overlay_->append(pad, error)) {
                error = "cannot stage zero padding for '" + patch.disc + "': " + error;
                return false;
            }
        }

        error.clear();
        out = std::move(applied);
        return true;
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    } catch (...) {
        error = "allocation failure";
        return false;
    }
}

}  // namespace riftwii
