// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gclaunch.hpp"

#include <ogc/conf.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/video.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

#include "gcplugin.hpp"
#include "gcpatch.hpp"
#include "ios_reload.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "netpacks.hpp"
#include "riftwii/apply.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/expand.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/gcoverlay.hpp"
#include "riftwii/gcvideo.hpp"
#include "riftwii/patch.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/imagevolume.hpp"
#include "sdfile.hpp"
#include "textfile.hpp"
#include "umsdev.hpp"

namespace riftwii::wii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool ends_with(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != tail[i]) return false;
    return true;
}

// A file on the card read with stdio.
class CardFile final : public ByteSource {
public:
    explicit CardFile(const std::string& path) : f_(std::fopen(path.c_str(), "rb")) {
        if (f_ && fseeko(f_, 0, SEEK_END) == 0) size_ = static_cast<std::uint64_t>(ftello(f_));
    }
    ~CardFile() override {
        if (f_) std::fclose(f_);
    }
    CardFile(const CardFile&) = delete;
    CardFile& operator=(const CardFile&) = delete;
    bool ok() const { return f_ != nullptr; }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        if (fseeko(f_, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
        return std::fread(destination, 1, length, f_) == length;
    }

private:
    std::FILE* f_;
    std::uint64_t size_ = 0;
};

// A file on the USB drive, read through the menu's view of it
// (usbcatalog.hpp's read_usb_range).
class UsbFile final : public ByteSource {
public:
    explicit UsbFile(const std::string& path) : path_(path) {
        ok_ = read_usb_range(path_, 0, nullptr, 0, size_);
    }
    bool ok() const { return ok_; }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        std::uint64_t size = 0;
        return read_usb_range(path_, offset, destination, length, size);
    }

private:
    std::string path_;
    std::uint64_t size_ = 0;
    bool ok_ = false;
};

// The image file at `path`, on the card or the USB drive; null when it
// cannot be opened.
std::shared_ptr<ByteSource> open_image_file(const std::string& path) {
    std::unique_ptr<ByteSource> file;
    if (GcImageOnUsb(path)) {
        std::unique_ptr<UsbFile> usb(new UsbFile(path));
        if (usb->ok()) file = std::move(usb);
    } else {
        std::unique_ptr<CardFile> card(new CardFile(path));
        if (card->ok()) file = std::move(card);
    }
    return std::shared_ptr<ByteSource>(std::move(file));
}

// A file shared with what else reads it (an RVZ's reader), as the disc.
class SharedFile final : public ByteSource {
public:
    explicit SharedFile(std::shared_ptr<ByteSource> file) : file_(std::move(file)) {}
    std::uint64_t size() const override { return file_->size(); }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return file_->read(offset, destination, length);
    }

private:
    std::shared_ptr<ByteSource> file_;
};

// "SD" or "USB", for the scan's lines.
const char* drive_of(const std::string& path) { return GcImageOnUsb(path) ? "USB" : "SD"; }

// What the scan needs of an image: its first 0x60 bytes (ID, magic,
// title) and, for an RVZ, check_rvz's verdict on it. Only an RVZ's
// headers are read, never its groups.
struct GcHead {
    std::uint8_t bytes[0x60] = {};
    bool rvz = false;
    RvzVerdict verdict;
    std::string describe;
    std::uint64_t disc_bytes = 0;
    std::uint32_t chunk_bytes = 0;
    // The header was read: false when the file could not be opened or its
    // first bytes not read, which the scan says apart from a missing header.
    bool header_read = false;
};

bool read_gc_head(const std::string& path, GcHead& out) {
    out = GcHead{};
    out.rvz = GcImageIsRvz(path);
    const std::shared_ptr<ByteSource> file = open_image_file(path);
    if (!file) return false;
    if (out.rvz) {
        RvzHead head;
        std::string error;
        if (!read_rvz_head(*file, head, error)) {
            logf("%s scan: %s: %s\n", drive_of(path), path.c_str(), error.c_str());
            return false;
        }
        out.header_read = true;
        if (head.disc_type != 1) return false;  // a Wii game's RVZ: not one of these
        std::memcpy(out.bytes, head.disc_header.data(), sizeof(out.bytes));
        out.verdict = check_rvz(head, file->size());
        out.describe = describe_rvz(head);
        out.disc_bytes = head.iso_size;
        out.chunk_bytes = head.chunk_size;
    } else if (!file->read(0, out.bytes, sizeof(out.bytes))) {
        return false;
    }
    out.header_read = true;
    return be32(out.bytes + 0x1C) == 0xC2339F3Du;
}

// The first 0x20 bytes say what the image is: its ID and the GameCube magic.
bool read_gc_id(const std::string& path, std::string& id) {
    GcHead head;
    if (!read_gc_head(path, head)) return false;
    id.assign(reinterpret_cast<const char*>(head.bytes), 6);
    // The ID names files downstream (an RVZ's group table, the choices
    // file): a crafted header may fill it with slashes, so such an image
    // is no image here.
    for (char c : id)
        if (c == '/' || c == '\\') return false;
    return true;
}

// A range of the image's disc: a disc file, for the packs' planning.
class IsoRange final : public ByteSource {
public:
    IsoRange(const ByteSource& iso, std::uint64_t offset, std::uint64_t size) : iso_(iso), offset_(offset), size_(size) {}
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        return iso_.read(offset_ + offset, destination, length);
    }
    std::uint64_t disc_offset() const { return offset_; }

private:
    const ByteSource& iso_;
    std::uint64_t offset_, size_;
};

// A pack's file on the USB drive, read through the menu's view of it;
// its pieces are the drive's sectors the game reads during play.
class UsbPackSource final : public ByteSource {
public:
    UsbPackSource(const std::string& path, const VolumeFile& file) : path_(path), file_(file) {}
    std::uint64_t size() const override { return file_.entry.size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        std::uint64_t size = 0;
        return read_usb_range(path_, offset, destination, length, size);
    }
    const std::string& path() const { return path_; }
    const VolumeFile& file() const { return file_; }

private:
    std::string path_;
    const VolumeFile& file_;
};

class SdSource final : public ByteSource {
public:
    explicit SdSource(const Fat32File& file) : file_(file) {}
    std::uint64_t size() const override { return file_.entry.size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return read_sd_file(file_, offset, destination, length);
    }
    const Fat32File& file() const { return file_; }

private:
    const Fat32File& file_;
};

// The USB drive's sector and bytes d2x is checked against before the game
// runs on it (activate_gamecube_usb): the first of a USB pack's files.
struct UsbCheck {
    std::string path;
    std::uint64_t sector = 0;
};

// Disc files from the image, the packs' files from the card or the USB
// drive (read raw, so their sectors are known for the overlay).
class GcProvider final : public ContentProvider {
public:
    GcProvider(const ByteSource& iso, const Fst& fst) : iso_(iso), fst_(fst) {}

    OpenStatus open_disc(const std::string& disc_path, std::unique_ptr<ByteSource>& out, std::string& error) override {
        std::uint32_t index = fst_.find(disc_path, false);
        if (index == Fst::npos) index = fst_.find(disc_path, true);
        if (index == Fst::npos) {
            error = "no such disc file '" + disc_path + "'";
            return OpenStatus::NotFound;
        }
        const FstEntry& e = fst_.entries()[index];
        if (e.is_directory) {
            error = "'" + disc_path + "' is a directory";
            return OpenStatus::Invalid;
        }
        out = std::make_unique<IsoRange>(iso_, e.offset, e.size);
        return OpenStatus::Ok;
    }

    OpenStatus open_external(const std::string& sd_path, std::unique_ptr<ByteSource>& out, std::string& error) override {
        if (is_usb_path(sd_path)) return open_usb(sd_path, out, error);
        std::string abs = sd_path.compare(0, 3, "sd:") == 0 ? sd_path.substr(3) : sd_path;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        const std::string key = "sd:" + abs;
        auto found = files_.find(key);
        if (found == files_.end()) {
            Fat32File file;
            bool missing = false;
            if (!resolve_sd_file(key, file, missing, error)) {
                const OpenStatus status = missing ? OpenStatus::NotFound : OpenStatus::IoError;
                error = "external file '" + sd_path + "' " + to_string(status) + ": " + error;
                return status;
            }
            if (file.entry.is_directory) {
                error = "'" + key + "' is a directory";
                return OpenStatus::Invalid;
            }
            found = files_.emplace(key, std::move(file)).first;
        }
        out = std::make_unique<SdSource>(found->second);
        return OpenStatus::Ok;
    }

    OpenStatus list_external(const std::string& sd_dir, std::vector<ExternalEntry>& out, std::string& error) override {
        if (is_usb_path(sd_dir)) {
            std::vector<VolumeEntry> listed;
            if (!list_usb_folder(sd_dir, listed)) {
                error = "cannot list " + sd_dir;
                return OpenStatus::NotFound;
            }
            out.clear();
            for (const VolumeEntry& e : listed) {
                ExternalEntry x;
                x.name = e.name;
                x.is_directory = e.is_directory;
                out.push_back(std::move(x));
            }
            return OpenStatus::Ok;
        }
        std::string abs = sd_dir.compare(0, 3, "sd:") == 0 ? sd_dir.substr(3) : sd_dir;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        std::vector<Fat32Entry> entries;
        bool missing = false;
        if (!list_sd_directory("sd:" + abs, entries, missing, error))
            return missing ? OpenStatus::NotFound : OpenStatus::IoError;
        out.clear();
        for (const Fat32Entry& e : entries) {
            ExternalEntry x;
            x.name = e.name;
            x.is_directory = e.is_directory;
            out.push_back(std::move(x));
        }
        return OpenStatus::Ok;
    }

    const UsbCheck& usb_check() const { return usb_check_; }

private:
    OpenStatus open_usb(const std::string& usb_path, std::unique_ptr<ByteSource>& out, std::string& error) {
        std::string abs = image_volume_path(usb_path);
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        const std::string key = usb_path_device(usb_path) + abs;
        auto found = usb_files_.find(key);
        if (found == usb_files_.end()) {
            VolumeFile file;
            if (!usb_file_lookup(key, file, error)) {
                const bool missing = error.find("no such ") != std::string::npos;
                const OpenStatus status = missing ? OpenStatus::NotFound : OpenStatus::IoError;
                error = "external file '" + usb_path + "' " + to_string(status) + ": " + error;
                return status;
            }
            if (file.entry.is_directory) {
                error = "'" + key + "' is a directory";
                return OpenStatus::Invalid;
            }
            found = usb_files_.emplace(key, std::move(file)).first;
            if (usb_check_.path.empty() && !found->second.fragments.empty()) {
                usb_check_.path = key;
                usb_check_.sector = found->second.fragments[0].sector;
            }
        }
        out = std::make_unique<UsbPackSource>(key, found->second);
        return OpenStatus::Ok;
    }

    const ByteSource& iso_;
    const Fst& fst_;
    std::map<std::string, Fat32File> files_;       // outlives every SdSource
    std::map<std::string, VolumeFile> usb_files_;  // and every UsbPackSource
    UsbCheck usb_check_;
};

// One composed file as overlay pieces: untouched bytes from the image,
// the packs' bytes from their card sectors, zeros.
bool to_pieces(const AppliedFile& file, std::vector<GcPiece>& out, std::string& error) {
    out.clear();
    for (const FlatExtent& x : file.flatten()) {
        if (x.length == 0) continue;
        if (x.kind == FlatExtent::Kind::Zero) {
            GcPiece p;
            p.kind = GcPiece::Kind::Zero;
            p.length = x.length;
            out.push_back(p);
        } else if (x.kind == FlatExtent::Kind::Original) {
            const auto* range = dynamic_cast<const IsoRange*>(x.source);
            if (!range) {
                error = "an untouched piece is not from the image";
                return false;
            }
            GcPiece p;
            p.kind = GcPiece::Kind::Image;
            p.source = range->disc_offset() + x.source_offset;
            p.length = x.length;
            out.push_back(p);
        } else if (const auto* usb = dynamic_cast<const UsbPackSource*>(x.source)) {
            // A file NTFS keeps inside its file table has no sectors of its
            // own for the game to read during play.
            if (usb->file().fragments.empty()) {
                error = usb->path() + " is kept inside the NTFS drive's file table (a file of a few hundred "
                        "bytes), where a GameCube game cannot read it: put the pack on the SD card, or on a "
                        "FAT32 drive";
                return false;
            }
            std::vector<PlacedRun> runs;
            if (!place_on_fragments(usb->file().fragments, x.source_offset, x.length, runs, error)) return false;
            for (const PlacedRun& r : runs) {
                GcPiece p;
                p.kind = GcPiece::Kind::UsbDevice;
                p.source = r.source;
                p.skip = r.skip;
                p.length = r.length;
                out.push_back(p);
            }
        } else {
            const auto* sd = dynamic_cast<const SdSource*>(x.source);
            if (!sd) {
                error = "a pack's piece is not on the SD card or the USB drive";
                return false;
            }
            std::vector<PlacedRun> runs;
            if (!place_on_fragments(sd->file().fragments, x.source_offset, x.length, runs, error)) return false;
            for (const PlacedRun& r : runs) {
                GcPiece p;
                p.kind = GcPiece::Kind::Device;
                p.source = r.source;
                p.skip = r.skip;
                p.length = r.length;
                out.push_back(p);
            }
        }
    }
    return true;
}

bool read_text(const std::string& path, std::string& text) { return ReadWholeFile(path, text); }

// The packs' files as overlay files, in the order the Wii path applies them.
bool plan_packs(const std::vector<PackageChoices>& packages, const std::uint8_t* boot, const Fst& fst,
                GcProvider& provider, std::vector<GcFile>& out, std::string& error) {
    DiscIdentity disc;
    disc.id.assign(reinterpret_cast<const char*>(boot), 6);
    disc.number = boot[6];
    disc.revision = boot[7];
    std::vector<FilePatch> files;
    for (const PackageChoices& pack : packages) {
        const std::string& xml = pack.xml_sd_path;
        const bool pack_on_usb = riftwii::is_usb_path(xml);
        std::string text;
        if (pack_on_usb ? !read_usb_text(xml, text) : !read_text(xml, text)) {
            error = "cannot open " + xml;
            return false;
        }
        Package package;
        if (!parse_package(text, package, error, PackFolderOf(xml))) {
            error = xml + ": " + error;
            return false;
        }
        for (const auto& c : pack.choices) {
            if (!select_choice(package, c.first, c.second, error)) {
                error = xml + ": " + error;
                return false;
            }
        }
        PlanOptions allowed;
        allowed.allow_filename_targets = true;
        allowed.allow_folders = true;
        Plan plan;
        if (!plan_package(package, disc, allowed, plan, error)) {
            error = xml + ": " + error;
            return false;
        }
        if (pack_on_usb) {
            // Its files are on the drive too (as for a Wii game's pack).
            const auto to_usb = [&](std::string& path) {
                if (!path.empty()) path = usb_path_device(xml) + (path[0] == '/' ? "" : "/") + path;
            };
            for (FilePatch& f : plan.files) to_usb(f.external);
            for (FolderPatch& f : plan.folders) to_usb(f.external);
        }
        // Never a partly applied mod: what GameCube games cannot take yet
        // stops the launch.
        if (!plan.memory.empty() || !plan.savegames.empty() || !plan.shifts.empty()) {
            error = xml + ": memory, savegame and shift patches are not supported for GameCube games yet";
            return false;
        }
        std::vector<std::string> notes;
        std::vector<FilePatch> expanded;
        if (!expand_plan(plan, fst, provider, expanded, notes, error)) {
            error = xml + ": " + error;
            return false;
        }
        for (const std::string& n : notes) logf("GameCube: %s\n", n.c_str());
        logf("GameCube: %s: %u file patch(es)\n", xml.c_str(), static_cast<unsigned>(expanded.size()));
        files.insert(files.end(), expanded.begin(), expanded.end());
    }

    // Patches on the same file apply in order, one on top of the other.
    std::vector<std::string> order;
    std::map<std::string, std::vector<FilePatch>> groups;
    for (const FilePatch& patch : files) {
        std::vector<std::string> targets;
        if (patch.is_filename) {
            std::vector<std::uint32_t> named = fst.find_files_named(patch.disc, true);
            for (std::uint32_t i : named) {
                std::string path;
                if (fst.path_of(i, path)) targets.push_back(path);
            }
        } else {
            targets.push_back(patch.disc);
        }
        for (const std::string& t : targets) {
            FilePatch one = patch;
            one.disc = t;
            one.is_filename = false;
            if (!groups.count(t)) order.push_back(t);
            groups[t].push_back(one);
        }
    }
    out.clear();
    for (const std::string& disc_path : order) {
        std::unique_ptr<AppliedFile> applied;
        if (!apply_patches(groups[disc_path], provider, applied, error)) return false;
        GcFile f;
        f.disc_path = disc_path;
        f.create = fst.find(disc_path, true) == Fst::npos;
        if (!to_pieces(*applied, f.pieces, error)) {
            error = disc_path + ": " + error;
            return false;
        }
        logf("GameCube: %s%s, %llu bytes in %u piece(s)\n", f.create ? "created " : "", disc_path.c_str(),
             static_cast<unsigned long long>(applied->size()), static_cast<unsigned>(f.pieces.size()));
        out.push_back(std::move(f));
    }
    return true;
}

}  // namespace

bool GcImageIsRvz(const std::string& path) { return ends_with(path, ".rvz"); }

bool GcImageOnUsb(const std::string& path) { return riftwii::is_usb_path(path); }

bool OpenGcDisc(const std::string& path, std::unique_ptr<ByteSource>& out, std::string& error) {
    const std::shared_ptr<ByteSource> file = open_image_file(path);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    if (!GcImageIsRvz(path)) {
        out.reset(new SharedFile(file));
        return true;
    }
    std::unique_ptr<RvzDiscSource> disc;
    if (!RvzDiscSource::open(file, disc, error)) {
        error = path + ": " + error;
        return false;
    }
    out = std::move(disc);
    return true;
}

bool FindGcImage(const std::string& game, const std::string& path, GcImage& out) {
    if (!path.empty()) {
        std::string id;
        if (!read_gc_id(path, id) || id.compare(0, game.size(), game) != 0) return false;
        out.path = path;
        out.id = id;
        return true;
    }
    // Use the menu's folder and duplicate rules for headless launches too.
    std::vector<ImageGame> all;
    ScanGcImages(all);
    ScanUsbGcImages(all);
    for (const ImageGame& g : all) {
        if (!g.gc_patch_source.empty() || g.id.compare(0, game.size(), game) != 0)
            continue;
        out.path = g.path;
        out.id = g.id;
        return true;
    }
    for (const ImageGame& g : all) {
        if (g.gc_patch_source.empty() || g.id.compare(0, game.size(), game) != 0) continue;
        out.path = g.path;
        out.id = g.id;
        out.patch_source = g.gc_patch_source;
        return true;
    }
    return false;
}

namespace {

// A file in a GameCube game's folder that the scan passes over, said in
// the log so a game that goes missing says why (a tester's .gcz or NKit
// image left no line at all). The first kSkipLines a scan; the rest are
// counted and said in one line at its end.
struct SkippedGcFiles {
    static constexpr unsigned kSkipLines = 10;
    unsigned said = 0;
    unsigned more = 0;
    void note(const std::string& path, const std::string& why) {
        if (said < kSkipLines) {
            logf("%s scan: %s is not listed: %s\n", drive_of(path), path.c_str(), why.c_str());
            ++said;
        } else {
            ++more;
        }
    }
    void finish(const char* drive) const {
        if (more != 0) logf("%s scan: %u more file(s) in GameCube game folders not listed\n", drive, more);
    }
};

// The GameCube image at `full` (on the card or the USB drive) as the list
// shows it, when it is one. A file that is not one is said in `skipped`,
// unless it is a Wii disc, which the Wii games' scan lists.
void add_gc_image(const std::string& full, std::vector<ImageGame>& out, SkippedGcFiles& skipped,
                  CisoFinds* ciso) {
    GcHead found;
    if (!read_gc_head(full, found)) {
        // Not read at all: the drive's file system says why when it can (a
        // FAT whose chain ends before the file's size, issue #129), which a
        // tester's Simpsons: Hit & Run never showed. An RVZ's own reading
        // error is in the log already (read_gc_head).
        if (!found.header_read) {
            if (found.rvz) return;
            std::string why = "its first bytes cannot be read";
            if (GcImageOnUsb(full)) {
                VolumeFile file;
                std::string lookup;
                if (!usb_file_lookup(full, file, lookup)) why = "it cannot be opened: " + lookup;
            } else if (!CardFile(full).ok()) {
                why = "it cannot be opened on the SD card";
            }
            skipped.note(full, why);
            return;
        }
        const bool wii_disc = be32(found.bytes + 0x18) == 0x5D1C9EA3u;
        if (wii_disc || found.rvz) return;
        // A container renamed to .iso: name what it is by its first bytes.
        const GcContainer container = gc_container_by_magic(found.bytes, sizeof(found.bytes));
        if (container == GcContainer::None) {
            skipped.note(full, "it has no GameCube disc header (damaged, cut short, or not a GameCube image; "
                               "NKit and other trimmed images are not played)");
            return;
        }
        skipped.note(full, gc_container_skip_reason(container));
        // A CISO found by what is in it gets the same popup as a .ciso name.
        if (container == GcContainer::Ciso && ciso) ciso->note(full);
        return;
    }
    const std::uint8_t* head = found.bytes;
    ImageGame g;
    g.device = GcImageOnUsb(full) ? ImageDevice::Usb : ImageDevice::Sd;
    g.path = full;
    g.format = found.rvz ? UsbImageFormat::Rvz : UsbImageFormat::Iso;
    if (found.rvz) {
        g.rvz_support = found.verdict.support;
        g.rvz_reasons = found.verdict.reasons;
        g.rvz_disc_bytes = found.disc_bytes;
        g.rvz_chunk_bytes = found.chunk_bytes;
    }
    g.id.assign(reinterpret_cast<const char*>(head), 6);
    // The ID names files downstream (an RVZ's group table, the choices
    // file): a crafted header may fill it with slashes, so such an image
    // is said in skipped instead of listed.
    for (char c : g.id) {
        if (c == '/' || c == '\\') {
            skipped.note(full, "its game ID holds a slash, so it is not listed");
            return;
        }
    }
    g.disc_number = head[6];
    g.revision = head[7];
    g.title.assign(reinterpret_cast<const char*>(head + 0x20), strnlen(reinterpret_cast<const char*>(head + 0x20), 0x40));
    g.checked = true;
    g.gamecube = true;
    logf("%s scan: GameCube %s \"%s\" at %s\n", drive_of(full), g.id.c_str(), g.title.c_str(), full.c_str());
    if (found.rvz) {
        logf("  %s\n", found.describe.c_str());
        for (const std::string& r : g.rvz_reasons) logf("  %s\n", r.c_str());
    }
    if (!try_push_back(out, std::move(g))) skipped.note(full, "there is not enough memory to list the game");
}

// Raw directory walkers bound corrupt chains and directory sizes, so a
// damaged SD directory cannot trap libfat's readdir here.
bool list_gc_directory(const std::string& path, std::vector<VolumeEntry>& out, std::string& error) {
    if (GcImageOnUsb(path)) return list_usb_folder(path, out, &error);
    std::vector<Fat32Entry> entries;
    bool missing = false;
    if (!list_sd_directory(path, entries, missing, error)) return false;
    out.clear();
    if (!try_reserve(out, entries.size())) {
        error = "there is not enough memory to list the folder";
        return false;
    }
    for (const Fat32Entry& entry : entries) {
        VolumeEntry file;
        file.name = entry.name;
        file.is_directory = entry.is_directory;
        out.push_back(std::move(file));
    }
    return true;
}

constexpr std::size_t kMaxGcImages = 4000;

void scan_gc_directory(const std::string& directory, bool nested, bool report_other_files,
                       const std::vector<ImageGame>& listed, std::vector<ImageGame>& found,
                       SkippedGcFiles& skipped, CisoFinds* ciso, unsigned& opened) {
    std::vector<VolumeEntry> entries;
    std::string error;
    if (!list_gc_directory(directory, entries, error)) {
        if (!error.empty() && error.find("no such ") == std::string::npos)
            skipped.note(directory, "the folder cannot be read: " + error);
        return;
    }
    std::sort(entries.begin(), entries.end(),
              [](const VolumeEntry& a, const VolumeEntry& b) { return a.name < b.name; });
    for (const VolumeEntry& entry : entries) {
        if (found.size() >= kMaxGcImages) return;
        std::string path;
        if (!join_game_path(directory, entry.name, path)) continue;
        if (entry.is_directory) {
            if (nested) scan_gc_directory(path, false, report_other_files, listed, found, skipped, ciso, opened);
            continue;
        }
        if (is_gc_image_name(entry.name)) {
            const bool known_wii = std::any_of(listed.begin(), listed.end(), [&](const ImageGame& game) {
                return game.path == path && game.checked && !game.gamecube;
            });
            if (!gc_scan_should_open(path, known_wii)) continue;
            ++opened;
            add_gc_image(path, found, skipped, ciso);
        } else if (report_other_files && !nested) {
            // The Wii scan already reports CISO in the roots and wbfs
            // subfolders. Only games subfolders need that work here.
            if (ciso && is_ciso_image_name(entry.name)) ciso->note(path);
            const std::string why = gc_folder_skip_reason(entry.name);
            if (!why.empty()) skipped.note(path, why);
        }
    }
}

void append_gc_images(std::vector<ImageGame>& found, std::vector<ImageGame>& out) {
    std::vector<GcCatalogImage> images;
    if (!try_reserve(images, found.size())) {
        logf("GameCube scan: there is not enough memory to choose which images to list\n");
        return;
    }
    for (const ImageGame& game : found) images.push_back({game.path, game.id, game.disc_number});
    const std::vector<int> hidden_by = gc_catalog_hidden_by(images);
    if (hidden_by.size() != found.size()) {
        logf("GameCube scan: there is not enough memory to choose which images to list\n");
        return;
    }

    // A Wii-style ID in an ISO/RVZ filename was listed without opening it.
    // Replace that placeholder even for disc 2 or a duplicate hidden below.
    out.erase(std::remove_if(out.begin(), out.end(), [&](const ImageGame& listed) {
        return std::any_of(found.begin(), found.end(), [&](const ImageGame& game) {
            return listed.path == game.path;
        });
    }), out.end());
    for (std::size_t i = 0; i < found.size(); ++i) {
        if (hidden_by[i] < 0) continue;
        const ImageGame& preferred = found[static_cast<std::size_t>(hidden_by[i])];
        const bool second_disc = found[i].disc_number == 1 && preferred.disc_number == 0;
        if (second_disc) {
            logf("%s scan: %s is disc 2 of %s (%s), so Home lists the game once; start it from disc 1, "
                 "and RiftWii changes to disc 2 when the game asks for it\n",
                 drive_of(found[i].path), found[i].path.c_str(), preferred.id.c_str(), preferred.path.c_str());
        } else {
            logf("%s scan: %s is another copy of %s; Home uses the copy in %s\n",
                 drive_of(found[i].path), found[i].path.c_str(), preferred.id.c_str(), preferred.path.c_str());
        }
    }
    for (std::size_t i = 0; i < found.size() && out.size() < kMaxGcImages; ++i) {
        if (hidden_by[i] >= 0) continue;
        if (!try_push_back(out, std::move(found[i]))) {
            logf("GameCube scan: there is not enough memory to list the remaining games\n");
            return;
        }
    }
}

void scan_gc_images(const std::string& device, std::vector<ImageGame>& out, CisoFinds* ciso) {
    const u64 started = gettime();
    unsigned opened = 0;
    SkippedGcFiles skipped;
    std::vector<ImageGame> found;
    scan_gc_directory(device + ":/games", true, true, out, found, skipped, ciso, opened);
    scan_gc_directory(device + ":/wbfs", true, false, out, found, skipped, ciso, opened);
    for (const std::string& folder : game_folders_on(Settings(), device)) {
        const std::string directory = device + ":" + folder;
        if (directory == device + ":/games" || directory == device + ":/wbfs") continue;
        scan_gc_directory(directory, true, false, out, found, skipped, ciso, opened);
    }
    skipped.finish(device == "sd" ? "SD" : "USB");
    append_gc_images(found, out);
    ScanGcPatches(out);
    const unsigned elapsed_ms = static_cast<unsigned>(ticks_to_millisecs(gettime() - started));
    logf("%s scan: the GameCube scan opened %u image(s) in %u ms\n",
         device == "sd" ? "SD" : "USB", opened, elapsed_ms);
}

}  // namespace

void ScanGcImages(std::vector<ImageGame>& out, CisoFinds* ciso) {
    forget_sd_layout();
    scan_gc_images("sd", out, ciso);
}

void ScanUsbGcImages(std::vector<ImageGame>& out, CisoFinds* ciso) {
    for (const std::string& root : usb_volume_roots()) {
        scan_gc_images(root.substr(0, root.size() - 2), out, ciso);
    }
}

namespace {

// The packs as a plan over the image (riftwii/gcoverlay.hpp): their files
// read from the image and the card, laid out past the image's end.
bool plan_gc_packs(const GcImage& image, const std::vector<PackageChoices>& packages, GcOverlayPlan& plan,
                   UsbCheck& usb_check, std::string& error) {
    std::unique_ptr<ByteSource> disc;
    if (!OpenGcDisc(image.path, disc, error)) return false;
    const ByteSource& iso = *disc;
    const std::uint64_t iso_size = iso.size();
    std::vector<std::uint8_t> boot(0x440);
    if (!iso.read(0, boot.data(), boot.size())) {
        error = "cannot read " + image.path;
        return false;
    }
    const std::uint32_t fst_size = be32(&boot[0x428]);
    // The game keeps the entire FST in MEM1; reject a corrupt size before
    // letting the disc header choose an allocation in the loader's heap.
    if (fst_size == 0 || fst_size > 0x01800000u) {
        error = "the game's FST is empty or larger than MEM1";
        logf("GameCube: %s\n", error.c_str());
        return false;
    }
    std::vector<std::uint8_t> fst_bytes(fst_size);
    if (fst_bytes.empty() || !iso.read(be32(&boot[0x424]), fst_bytes.data(), fst_bytes.size())) {
        error = "cannot read the game's FST";
        return false;
    }
    Fst fst;
    if (!Fst::parse(fst_bytes.data(), fst_bytes.size(), false, fst, error)) {
        error = "the game's FST: " + error;
        return false;
    }
    GcProvider provider(iso, fst);
    std::vector<GcFile> files;
    if (!plan_packs(packages, boot.data(), fst, provider, files, error)) return false;
    if (!plan_gc_overlay(boot, fst_bytes, iso_size, files, plan, error)) return false;
    usb_check = provider.usb_check();
    logf("GameCube: the packs change %u file(s); the new FST is %u bytes at 0x%llx\n",
         static_cast<unsigned>(files.size()), static_cast<unsigned>(plan.fst.size()),
         static_cast<unsigned long long>(plan.fst_offset));
    return true;
}

// A two-disc game's second disc beside the image the game starts from
// (riftwii/usbgame.hpp's gc_disc_change_partner): the folder's GameCube
// images by their headers, disc 2 the one with the same game ID and disc
// number 1. A second disc of another game is said and left alone. The
// plugin checks the header again (gcdiscs.hpp) before it changes discs.
void find_second_disc(GcImage& image) {
    const std::size_t slash = image.path.rfind('/');
    if (slash == std::string::npos) return;
    const std::string dir = image.path.substr(0, slash);
    std::vector<VolumeEntry> files;
    std::string error;
    if (!list_gc_directory(dir, files, error)) return;
    // The image started first, then the folder's other GameCube images.
    std::vector<std::string> paths{image.path};
    std::vector<GcFolderImage> discs;
    GcHead head;
    if (!read_gc_head(image.path, head)) return;
    discs.push_back({std::string(reinterpret_cast<const char*>(head.bytes), 6), head.bytes[6]});
    for (const VolumeEntry& file : files) {
        if (paths.size() >= kMaxGcImages) break;
        if (file.is_directory || !is_gc_image_name(file.name)) continue;
        std::string full;
        if (!join_game_path(dir, file.name, full) || full == image.path) continue;
        if (!read_gc_head(full, head)) continue;
        paths.push_back(full);
        discs.push_back({std::string(reinterpret_cast<const char*>(head.bytes), 6), head.bytes[6]});
    }
    std::vector<std::size_t> other_id;
    const int partner = gc_disc_change_partner(discs, 0, &other_id);
    for (std::size_t i : other_id)
        logf("GameCube: %s is a second disc of %s, not of %s: RiftWii does not change to it\n", paths[i].c_str(),
             discs[i].id.c_str(), discs[0].id.c_str());
    if (partner < 0) return;
    image.disc2_path = paths[static_cast<std::size_t>(partner)];
    logf("GameCube: disc 2 of %s is %s: RiftWii changes to it when the game asks for it\n", image.id.c_str(),
         image.disc2_path.c_str());
}

// A game or a pack on the USB drive: the image's pieces through the
// menu's view of the drive (libogc's USB driver, IOS 58 only), then the
// reload into a d2x cIOS whose USB device the game's loader reads the
// drive through, tried in the order a Wii game on the drive would be (the
// slot picked for the game, else image_cios_order's). d2x is checked
// against the first sector of the image, or of the first USB pack file
// (`pack_check`) when the image is on the SD card. The memory cards and
// the trace stay on the SD card, so the slot must reach it too (uses_sd).
bool ready_usb(GcImage& image, const UsbCheck& pack_check, std::string& error) {
    std::string check_path = pack_check.path;
    std::uint64_t check_sector = pack_check.sector;
    if (GcImageOnUsb(image.path)) {
        if (!usb_file_pieces(image.path, image.usb_bytes, image.usb_fragments, error)) {
            error = image.path + ": " + error;
            return false;
        }
        logf("GameCube: %s is on the USB drive, in %u piece(s) from sector %llu\n", image.path.c_str(),
             static_cast<unsigned>(image.usb_fragments.size()),
             static_cast<unsigned long long>(image.usb_fragments[0].sector));
        check_path = image.path;
        check_sector = image.usb_fragments[0].sector;
        // A second disc beside it: its pieces too, while the menu still
        // reads the drive. Without them the game starts on disc 1 alone.
        std::string disc2_error;
        if (!image.disc2_path.empty() &&
            !usb_file_pieces(image.disc2_path, image.disc2_usb_bytes, image.disc2_usb_fragments, disc2_error)) {
            logf("GameCube: %s is not used as disc 2: %s\n", image.disc2_path.c_str(), disc2_error.c_str());
            image.disc2_path.clear();
            image.disc2_usb_fragments.clear();
            image.disc2_usb_bytes = 0;
        }
    } else {
        logf("GameCube: packs on the USB drive (%s first); the game is on the SD card\n", check_path.c_str());
    }
    std::uint8_t first[32];
    std::uint64_t size = 0;
    if (check_path.empty() || !read_usb_range(check_path, 0, first, sizeof(first), size)) {
        error = "cannot read " + (check_path.empty() ? std::string("the USB drive") : check_path);
        return false;
    }
    ImageGame game;  // what image_cios_order goes by
    game.device = ImageDevice::Usb;
    game.path = image.path;
    game.id = image.id;
    // A drive d2x cannot read is said at the end, each reason once with the
    // slots that gave it (another d2x base may reach the port), as for a
    // Wii game.
    std::string first_error;
    std::vector<D2xSlotFailure> usb_failures;
    const std::vector<int> cios_order = image_cios_order(game, image.cios_slot, true);
    std::string slots;
    for (int slot : cios_order) slots += (slots.empty() ? "IOS" : ", IOS") + std::to_string(slot);
    logf("GameCube: %s will try cIOS slots for the USB drive in this order: %s\n", game.id.c_str(), slots.c_str());
    for (int slot : cios_order) {
        if (activate_gamecube_usb(first, check_sector, slot, nullptr, error)) {
            image.usb_in_use = true;
            return true;
        }
        if (reload_terminal_failure()) return false;
        if (d2x_usb_drive_failed()) {
            usb_failures.push_back({slot, error});
        } else if (first_error.empty()) {
            first_error = error;
        }
        // A call d2x never answered can't be cancelled, and another IOS
        // reload with it outstanding may hang: no further slot.
        if (ums::Stuck()) break;
    }
    if (!usb_failures.empty()) error = d2x_usb_slots_summary(usb_failures, first_error);
    else if (!first_error.empty()) error = first_error;
    return false;
}

}  // namespace

GcConsoleSettings GcConsoleNow() {
    GcConsoleSettings console;
    console.aspect = Settings().aspect;
    console.game_language = Settings().game_language;
    console.wii_widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    console.wii_progressive = CONF_GetProgressiveScan() > 0;
    console.component_cable = VIDEO_HaveComponentCable() != 0;
    console.wii_language = CONF_GetLanguage();
    console.video_width = Settings().video_width;
    return console;
}

bool LaunchGcGame(const GcImage& wanted,const std::vector<PackageChoices>& packages, std::string& error) {
    GcImage image = wanted;
    if (!image.patch_source.empty() && !PrepareGcPatch(image, error)) return false;
    // An RVZ is checked again here, as a Wii game's is before its launch:
    // one RiftWii cannot play stops now, with the reason.
    if (GcImageIsRvz(image.path)) {
        GcHead head;
        if (!read_gc_head(image.path, head)) {
            error = "cannot read " + image.path;
            return false;
        }
        logf("GameCube: %s is an RVZ: %s\n", image.path.c_str(), head.describe.c_str());
        for (const std::string& r : head.verdict.reasons) logf("GameCube: %s\n", r.c_str());
        if (head.verdict.support == RvzSupport::Unsupported) {
            error = "This RVZ cannot be played. " +
                    (head.verdict.reasons.empty() ? std::string() : head.verdict.reasons.front());
            return false;
        }
    }
    // The game's own options, from the file the menu keeps them in (the
    // choices file, riftwii/launch.hpp's restore).
    {
        // No file: the defaults, from an empty text.
        std::string text;
        ReadWholeFile("sd:/riftwii/choices/" + image.id + ".txt", text);
        LaunchModel choices;
        choices.restore(text);
        image.drive_timing = kGcDriveTimingReady && choices.game.gc_disc_reads != "fast";
        image.shared_card = choices.game.gc_card == "shared";
        image.real_card = choices.game.gc_card == "real";
        const LaunchExtras& extras = CurrentLaunchExtras();
        if (extras.game_id == image.id && extras.gc_card_fallback) {
            image.real_card = false;
            image.shared_card = false;
        }
        // "global" follows Settings and the Wii's own settings, as a Wii
        // game's rows do.
        const GcConsoleSettings console = GcConsoleNow();
        const GameSettings& game = choices.game;
        image.progressive = gc_progressive_on(game, image.id, console);
        image.widescreen = kGcWidescreenReady && gc_widescreen_on(game, console);
        image.language = gc_language_number(game, console);
        image.sharp = game.gc_sharp == "on";
        image.sixty_hz = gc_sixty_hz_on(game, image.id);
        image.vi_width = gc_vi_width(gc_picture_width(game, console));
        logf("GameCube: Picture width %s (Settings' %s): %s\n", game.gc_width.c_str(), console.video_width.c_str(),
             gc_picture_width(game, console).c_str());
        logf("GameCube: Progressive scan %s (the Wii's 480p setting %s, a component cable %s%s), Widescreen %s (Settings' aspect %s, "
             "the Wii's TV %s), Game language %s (Settings' %s, the Wii's language %d)\n",
             game.gc_progressive.c_str(), console.wii_progressive ? "on" : "off",
             console.component_cable ? "plugged in" : "not seen", gc_pal_game(image.id) ? ", a PAL game never gets 480p" : "",
             kGcWidescreenReady ? game.gc_widescreen.c_str() : "held back from this release",
             console.aspect.c_str(), console.wii_widescreen ? "16:9" : "4:3", game.gc_language.c_str(),
             console.game_language.c_str(), console.wii_language);
    }
    logf("GameCube: %s (%s), disc reads %s\n", image.id.c_str(), image.path.c_str(),
         image.drive_timing ? "at a GameCube drive's speed" : "at the SD card's speed");
    logf("GameCube: picture %s, %s, %s, %s; language %s\n", image.progressive ? "480p" : "the game's own",
         image.widescreen ? "16:9" : "the game's own aspect", image.sharp ? "sharper" : "the game's own blur",
         image.sixty_hz ? "60 Hz where the game has it" : "the game's own speed",
         image.language >= 0 ? kGcLanguages[image.language] : "the console's");
    // A pack that cannot be applied whole stops the launch (plan_packs).
    // Planned before a USB game's reload, while the menu still reads the drive.
    GcOverlayPlan plan;
    const bool has_packs = !packages.empty();
    UsbCheck usb_check;
    if (has_packs && !plan_gc_packs(image, packages, plan, usb_check, error)) return false;
    const bool packs_on_usb = has_packs && gc_plan_uses_usb(plan);
    find_second_disc(image);
    if ((GcImageOnUsb(image.path) || packs_on_usb) && !ready_usb(image, usb_check, error)) return false;
    return LaunchGcNative(image, has_packs ? &plan : nullptr, error);
}

}  // namespace riftwii::wii
