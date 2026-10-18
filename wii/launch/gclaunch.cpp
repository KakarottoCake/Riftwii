// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gclaunch.hpp"

#include <dirent.h>
#include <ogc/conf.h>
#include <ogc/video.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "gcnative.hpp"
#include "gcpatch.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "netpacks.hpp"
#include "riftwii/apply.hpp"
#include "riftwii/expand.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/gcoverlay.hpp"
#include "riftwii/gcvideo.hpp"
#include "riftwii/patch.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/rvz.hpp"
#include "sdfile.hpp"

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

bool is_gc_image_name(const std::string& name) {
    return name[0] != '.' && (ends_with(name, ".iso") || ends_with(name, ".gcm") || ends_with(name, ".rvz"));
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
};

bool read_gc_head(const std::string& path, GcHead& out) {
    out = GcHead{};
    out.rvz = GcImageIsRvz(path);
    auto file = std::make_shared<CardFile>(path);
    if (!file->ok()) return false;
    if (out.rvz) {
        RvzHead head;
        std::string error;
        if (!read_rvz_head(*file, head, error)) {
            logf("SD scan: %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        if (head.disc_type != 1) return false;  // a Wii game's RVZ: not one of these
        std::memcpy(out.bytes, head.disc_header.data(), sizeof(out.bytes));
        out.verdict = check_rvz(head, file->size());
        out.describe = describe_rvz(head);
        out.disc_bytes = head.iso_size;
        out.chunk_bytes = head.chunk_size;
    } else if (!file->read(0, out.bytes, sizeof(out.bytes))) {
        return false;
    }
    return be32(out.bytes + 0x1C) == 0xC2339F3Du;
}

// The first 0x20 bytes say what the image is: its ID and the GameCube magic.
bool read_gc_id(const std::string& path, std::string& id) {
    GcHead head;
    if (!read_gc_head(path, head)) return false;
    id.assign(reinterpret_cast<const char*>(head.bytes), 6);
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

// Disc files from the image, the packs' files from the card (read raw,
// so their sectors are known for the overlay).
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

private:
    const ByteSource& iso_;
    const Fst& fst_;
    std::map<std::string, Fat32File> files_;  // outlives every SdSource
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
        } else {
            const auto* sd = dynamic_cast<const SdSource*>(x.source);
            if (!sd) {
                error = "a pack's piece is not on the SD card";
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

bool read_text(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream all;
    all << in.rdbuf();
    text = all.str();
    return true;
}

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
        std::string text;
        if (!read_text(xml, text)) {
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

bool OpenGcDisc(const std::string& path, std::unique_ptr<ByteSource>& out, std::string& error) {
    if (!GcImageIsRvz(path)) {
        std::unique_ptr<CardFile> iso(new CardFile(path));
        if (!iso->ok()) {
            error = "cannot open " + path;
            return false;
        }
        out = std::move(iso);
        return true;
    }
    auto file = std::make_shared<CardFile>(path);
    if (!file->ok()) {
        error = "cannot open " + path;
        return false;
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
    DIR* games = opendir("sd:/games");
    if (!games) return false;
    bool found = false;
    while (dirent* folder = readdir(games)) {
        if (folder->d_name[0] == '.') continue;
        const std::string dir = std::string("sd:/games/") + folder->d_name;
        DIR* inner = opendir(dir.c_str());
        if (!inner) continue;
        while (dirent* file = readdir(inner)) {
            const std::string name = file->d_name;
            if (!is_gc_image_name(name)) continue;
            std::string id;
            const std::string full = dir + "/" + name;
            if (read_gc_id(full, id) && id.compare(0, game.size(), game) == 0) {
                out.path = full;
                out.id = id;
                found = true;
                break;
            }
        }
        closedir(inner);
        if (found) break;
    }
    closedir(games);
    if (found) return true;
    // A patch that makes it from one of them.
    std::vector<ImageGame> all;
    ScanGcImages(all);
    for (const ImageGame& g : all) {
        if (g.gc_patch_source.empty() || g.id.compare(0, game.size(), game) != 0) continue;
        out.path = g.path;
        out.id = g.id;
        out.patch_source = g.gc_patch_source;
        return true;
    }
    return false;
}

void ScanGcImages(std::vector<ImageGame>& out) {
    DIR* games = opendir("sd:/games");
    if (!games) return;
    while (dirent* folder = readdir(games)) {
        if (folder->d_name[0] == '.') continue;
        const std::string dir = std::string("sd:/games/") + folder->d_name;
        DIR* inner = opendir(dir.c_str());
        if (!inner) continue;
        while (dirent* file = readdir(inner)) {
            const std::string name = file->d_name;
            if (!is_gc_image_name(name)) continue;
            const std::string full = dir + "/" + name;
            GcHead found;
            if (!read_gc_head(full, found)) continue;
            const std::uint8_t* head = found.bytes;
            ImageGame g;
            g.device = ImageDevice::Sd;
            g.path = full;
            g.format = found.rvz ? UsbImageFormat::Rvz : UsbImageFormat::Iso;
            if (found.rvz) {
                g.rvz_support = found.verdict.support;
                g.rvz_reasons = found.verdict.reasons;
                g.rvz_disc_bytes = found.disc_bytes;
                g.rvz_chunk_bytes = found.chunk_bytes;
            }
            g.id.assign(reinterpret_cast<const char*>(head), 6);
            g.disc_number = head[6];
            g.revision = head[7];
            g.title.assign(reinterpret_cast<const char*>(head + 0x20), strnlen(reinterpret_cast<const char*>(head + 0x20), 0x40));
            g.checked = true;
            g.gamecube = true;
            logf("SD scan: GameCube %s \"%s\" at %s\n", g.id.c_str(), g.title.c_str(), full.c_str());
            if (found.rvz) {
                logf("  %s\n", found.describe.c_str());
                for (const std::string& r : g.rvz_reasons) logf("  %s\n", r.c_str());
            }
            out.push_back(std::move(g));
        }
        closedir(inner);
    }
    closedir(games);
    ScanGcPatches(out);
}

namespace {

// The packs as a plan over the image (riftwii/gcoverlay.hpp): their files
// read from the image and the card, laid out past the image's end.
bool plan_gc_packs(const GcImage& image, const std::vector<PackageChoices>& packages, GcOverlayPlan& plan,
                   std::string& error) {
    std::unique_ptr<ByteSource> disc;
    if (!OpenGcDisc(image.path, disc, error)) return false;
    const ByteSource& iso = *disc;
    const std::uint64_t iso_size = iso.size();
    std::vector<std::uint8_t> boot(0x440);
    if (!iso.read(0, boot.data(), boot.size())) {
        error = "cannot read " + image.path;
        return false;
    }
    std::vector<std::uint8_t> fst_bytes(be32(&boot[0x428]));
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
    logf("GameCube: the packs change %u file(s); the new FST is %u bytes at 0x%llx\n",
         static_cast<unsigned>(files.size()), static_cast<unsigned>(plan.fst.size()),
         static_cast<unsigned long long>(plan.fst_offset));
    return true;
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
        std::ifstream in("sd:/riftwii/choices/" + image.id + ".txt");
        std::stringstream text;
        text << in.rdbuf();
        LaunchModel choices;
        choices.restore(text.str());
        image.drive_timing = kGcDriveTimingReady && choices.game.gc_disc_reads != "fast";
        image.shared_card = choices.game.gc_card == "shared";
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
    if (packages.empty()) return LaunchGcNative(image, nullptr, error);
    // A pack that cannot be applied whole stops the launch (plan_packs).
    GcOverlayPlan plan;
    if (!plan_gc_packs(image, packages, plan, error)) return false;
    return LaunchGcNative(image, &plan, error);
}

}  // namespace riftwii::wii
