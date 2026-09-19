// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/apply.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace riftwii {

bool ContentProvider::list_external(const std::string& sd_dir, std::vector<ExternalEntry>& out, std::string& error) {
    (void)sd_dir;
    (void)out;
    error = "this provider cannot list directories";
    return false;
}

bool list_native_directory(const std::string& native_path, std::vector<ExternalEntry>& out, std::string& error) {
    DIR* dir = opendir(native_path.c_str());
    if (!dir) {
        error = std::string("opendir: ") + std::strerror(errno);
        return false;
    }
    std::vector<ExternalEntry> entries;
    for (;;) {
        errno = 0;
        const dirent* e = readdir(dir);
        if (!e) {
            if (errno != 0) {
                error = std::string("readdir: ") + std::strerror(errno);
                closedir(dir);
                return false;
            }
            break;
        }
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        ExternalEntry entry;
        entry.name = name;
        bool known = false;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) {
            entry.is_directory = true;
            known = true;
        } else if (e->d_type == DT_REG) {
            known = true;
        }
#endif
        if (!known) {
            struct stat st;
            const std::string full = native_path + "/" + name;
            if (stat(full.c_str(), &st) != 0) {
                error = "stat '" + full + "': " + std::strerror(errno);
                closedir(dir);
                return false;
            }
            entry.is_directory = S_ISDIR(st.st_mode);
        }
        entries.push_back(std::move(entry));
    }
    closedir(dir);
    out = std::move(entries);
    error.clear();
    return true;
}

DirectoryProvider::DirectoryProvider(std::string disc_root, std::string sd_root)
    : disc_root_(std::move(disc_root)), sd_root_(std::move(sd_root)) {}

bool DirectoryProvider::list_external(const std::string& sd_dir, std::vector<ExternalEntry>& out,
                                      std::string& error) {
    std::string abs = sd_dir;
    if (abs.empty() || abs[0] != '/') abs = "/" + abs;
    return list_native_directory(join(sd_root_, abs), out, error);
}

std::string DirectoryProvider::join(const std::string& root, const std::string& abs_path) {
    std::string r = root;
    while (r.size() > 1 && (r.back() == '/' || r.back() == '\\')) {
        r.pop_back();
    }
    if (r.empty()) r = ".";
    return r + abs_path;  // abs_path always starts with '/'.
}

OpenStatus DirectoryProvider::open_disc(const std::string& disc_path,
                                        std::unique_ptr<ByteSource>& out, std::string& error) {
    if (disc_path.empty() || disc_path[0] != '/') {
        error = "disc path must be absolute '" + disc_path + "'";
        return OpenStatus::Invalid;
    }
    std::unique_ptr<FileByteSource> f;
    const OpenStatus status = FileByteSource::open(join(disc_root_, disc_path), f, error);
    if (status != OpenStatus::Ok) {
        error = "disc file '" + disc_path + "' " + to_string(status) + ": " + error;
        return status;
    }
    out = std::move(f);
    return OpenStatus::Ok;
}

OpenStatus DirectoryProvider::open_external(const std::string& sd_path,
                                            std::unique_ptr<ByteSource>& out, std::string& error) {
    if (sd_path.empty()) {
        error = "external path empty";
        return OpenStatus::Invalid;
    }
    // Planned patches are always resolved to absolute SD paths, but accept a
    // bare relative name defensively by treating it as root-relative.
    std::string abs = sd_path;
    if (abs[0] != '/') abs = "/" + abs;
    std::unique_ptr<FileByteSource> f;
    const OpenStatus status = FileByteSource::open(join(sd_root_, abs), f, error);
    if (status != OpenStatus::Ok) {
        error = "external file '" + sd_path + "' " + to_string(status) + ": " + error;
        return status;
    }
    out = std::move(f);
    return OpenStatus::Ok;
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

namespace {

// Replaces [e.dest, e.dest + e.length) inside a sorted, gap-free list.
void OverlayFlat(std::vector<FlatExtent>& list, const FlatExtent& e) {
    const std::uint64_t start = e.dest;
    const std::uint64_t end = e.dest + e.length;
    std::vector<FlatExtent> result;
    result.reserve(list.size() + 2);
    bool inserted = false;
    for (const FlatExtent& x : list) {
        const std::uint64_t xs = x.dest;
        const std::uint64_t xe = x.dest + x.length;
        if (xe <= start || xs >= end) {
            if (xs >= end && !inserted) {
                result.push_back(e);
                inserted = true;
            }
            result.push_back(x);
            continue;
        }
        if (xs < start) {
            FlatExtent head = x;
            head.length = start - xs;
            result.push_back(head);
        }
        if (!inserted) {
            result.push_back(e);
            inserted = true;
        }
        if (xe > end) {
            FlatExtent tail = x;
            tail.dest = end;
            tail.length = xe - end;
            tail.source_offset = x.source_offset + (end - xs);
            result.push_back(tail);
        }
    }
    if (!inserted) result.push_back(e);
    list.swap(result);
}

// Joins neighbours that continue the same source without a break.
void CoalesceFlat(std::vector<FlatExtent>& list) {
    std::vector<FlatExtent> result;
    for (const FlatExtent& x : list) {
        if (!result.empty()) {
            FlatExtent& p = result.back();
            const bool contiguous = p.dest + p.length == x.dest;
            const bool same = p.kind == x.kind && p.source == x.source &&
                              (p.kind == FlatExtent::Kind::Zero || p.source_offset + p.length == x.source_offset);
            if (contiguous && same) {
                p.length += x.length;
                continue;
            }
        }
        result.push_back(x);
    }
    list.swap(result);
}

}  // namespace

std::vector<FlatExtent> AppliedFile::flatten() const {
    const std::uint64_t size = overlay_->size();
    std::vector<FlatExtent> list;
    if (base_) {
        list = base_->flatten();
    } else if (original_ && original_->size() > 0) {
        FlatExtent o;
        o.kind = FlatExtent::Kind::Original;
        o.dest = 0;
        o.length = original_->size();
        o.source = original_.get();
        o.source_offset = 0;
        list.push_back(o);
    }
    // Clip the layer below to this layer's size and zero-fill past its end.
    std::vector<FlatExtent> clipped;
    std::uint64_t covered = 0;
    for (const FlatExtent& x : list) {
        if (x.dest >= size) break;
        FlatExtent c = x;
        if (c.dest + c.length > size) c.length = size - c.dest;
        clipped.push_back(c);
        covered = c.dest + c.length;
    }
    if (covered < size) {
        FlatExtent z;
        z.kind = FlatExtent::Kind::Zero;
        z.dest = covered;
        z.length = size - covered;
        clipped.push_back(z);
    }
    for (const OverlayExtent& ext : overlay_->extents()) {
        FlatExtent e;
        e.kind = (ext.source == zero_.get()) ? FlatExtent::Kind::Zero : FlatExtent::Kind::External;
        e.dest = ext.destination;
        e.length = ext.length;
        e.source = e.kind == FlatExtent::Kind::Zero ? nullptr : ext.source;
        e.source_offset = e.kind == FlatExtent::Kind::Zero ? 0 : ext.source_offset;
        OverlayFlat(clipped, e);
    }
    CoalesceFlat(clipped);
    return clipped;
}

bool AppliedFile::build(const FilePatch& patch, ContentProvider& provider,
                        std::unique_ptr<AppliedFile> base, std::unique_ptr<AppliedFile>& out,
                        std::string& error) {
    try {
        if (patch.disc.empty() || patch.disc[0] != '/') {
            error = "file disc must be absolute";
            return false;
        }
        if (patch.external.empty()) {
            error = "file external empty";
            return false;
        }

        // DI reads address the disc in 4-byte words, so the low two bits of
        // a file offset cannot be expressed; clear them here so host output
        // matches what the console can actually do (Dolphin does the same).
        const std::uint64_t patch_start = patch.offset & ~std::uint64_t(3);

        // The "original" is the layer below when composing, otherwise the
        // disc file (or an empty file when it is absent and create="true").
        std::unique_ptr<ByteSource> original;
        const ByteSource* original_ref = nullptr;
        std::uint64_t orig_size = 0;
        if (base) {
            original_ref = &base->view();
            orig_size = base->size();
        } else {
            std::unique_ptr<ByteSource> opened;
            std::string open_err;
            const OpenStatus status = provider.open_disc(patch.disc, opened, open_err);
            if (status == OpenStatus::Ok) {
                if (!opened) {
                    error = "provider returned no source for '" + patch.disc + "'";
                    return false;
                }
                orig_size = opened->size();
                if (orig_size > kMaxFileBytes) {
                    error = "disc file too large '" + patch.disc + "'";
                    return false;
                }
                original = std::move(opened);
            } else if (status == OpenStatus::NotFound && patch.create) {
                original.reset(new MemorySource(std::vector<std::uint8_t>()));
                orig_size = 0;
            } else {
                error = open_err.empty()
                            ? ("disc file '" + patch.disc + "' " + to_string(status))
                            : open_err;
                return false;
            }
            original_ref = original.get();
        }

        std::unique_ptr<ByteSource> external;
        {
            std::string open_err;
            const OpenStatus status = provider.open_external(patch.external, external, open_err);
            if (status != OpenStatus::Ok || !external) {
                error = open_err.empty()
                            ? ("external file '" + patch.external + "' " + to_string(status))
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
        applied->base_ = std::move(base);
        applied->original_ = std::move(original);
        applied->external_ = std::move(external);
        applied->overlay_.reset(new ReadOverlay(*original_ref, target));

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

bool build_replacement(const FilePatch& patch, ContentProvider& provider,
                       std::unique_ptr<AppliedFile>& out, std::string& error) {
    return AppliedFile::build(patch, provider, nullptr, out, error);
}

bool apply_patches(const std::vector<FilePatch>& patches, ContentProvider& provider,
                   std::unique_ptr<AppliedFile>& out, std::string& error) {
    if (patches.empty()) {
        error = "no patches to apply";
        return false;
    }
    for (const auto& p : patches) {
        if (p.disc != patches[0].disc) {
            error = "apply_patches: mixed disc targets '" + patches[0].disc + "' and '" + p.disc + "'";
            return false;
        }
    }
    std::unique_ptr<AppliedFile> current;
    for (const auto& p : patches) {
        std::unique_ptr<AppliedFile> next;
        if (!AppliedFile::build(p, provider, std::move(current), next, error)) {
            return false;
        }
        current = std::move(next);
    }
    error.clear();
    out = std::move(current);
    return true;
}

}  // namespace riftwii
