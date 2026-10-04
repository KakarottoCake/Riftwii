// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "modplan.hpp"

#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "di.hpp"
#include "log.hpp"
#include "netpacks.hpp"
#include "progress.hpp"
#include "riftwii/apply.hpp"
#include "riftwii/dol.hpp"
#include "riftwii/expand.hpp"
#include "riftwii/fat32.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/mempatch.hpp"
#include "riftwii/patch.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/source.hpp"
#include "sdfile.hpp"
#include "umsdev.hpp"

namespace riftwii::wii {
namespace {

// A disc file as a ByteSource: its FST extent read through DI.
class DiscFileSource final : public ByteSource {
public:
    DiscFileSource(std::uint64_t offset, std::uint64_t size) : offset_(offset), size_(size) {}
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || length > size_ - offset) return false;
        if (length == 0) return true;
        di::PartitionSource data;
        return data.read(offset_ + offset, destination, length);
    }

private:
    std::uint64_t offset_;
    std::uint64_t size_;
};

// An SD file as a ByteSource: read raw through the sectors its lookup
// found, the same ones the redirect table will point at.
class SdFileSource final : public ByteSource {
public:
    explicit SdFileSource(const Fat32File& file) : file_(file) {}
    std::uint64_t size() const override { return file_.entry.size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return read_sd_file(file_, offset, destination, length);
    }

private:
    const Fat32File& file_;  // owned by the provider's map, which outlives the sources
};

// A file on the USB drive (FAT32 or NTFS) as a ByteSource, read through
// d2x the same way.
class UsbFileSource final : public ByteSource {
public:
    UsbFileSource(const VolumeFile& file, const ImageVolume& volume) : file_(file), volume_(volume) {}
    std::uint64_t size() const override { return file_.entry.size; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        return volume_.read(file_, offset, destination, length);
    }

private:
    const VolumeFile& file_;  // owned by the provider's map, which outlives the sources
    const ImageVolume& volume_;
};

// The volumes' own "not there" errors (fat32.cpp, ntfs.cpp).
bool is_missing(const std::string& error) { return error.find("no such ") != std::string::npos; }

// A pack on the USB drive names its files with this in front
// ("usb:/riivolution/..."); every other external is on the SD card.
constexpr char kUsbPrefix[] = "usb:";
bool OnUsb(const std::string& external) { return external.compare(0, 4, kUsbPrefix) == 0; }

// The disc path the compile gives the game's executable, which the FST
// does not list (absolute, as patches need; no FST name holds '<').
constexpr const char* kMainDolPath = "/<main.dol>";

// The disc side comes from the FST, the SD side from the card read raw
// (riftwii/fat32.hpp, folders cached): libfat opened each of a big mod's
// two thousand files twice and walked the same folders every time. The
// lookup is kept per source so the placer has its sectors.
class WiiProvider final : public ContentProvider {
public:
    explicit WiiProvider(const Fst& fst) : fst_(fst) {}
    void set_main_dol(std::uint64_t offset, std::uint64_t size) {
        dol_offset_ = offset;
        dol_size_ = size;
    }

    OpenStatus open_disc(const std::string& disc_path, std::unique_ptr<ByteSource>& out,
                         std::string& error) override {
        if (disc_path == kMainDolPath && dol_size_ != 0) {
            out = std::make_unique<DiscFileSource>(dol_offset_, dol_size_);
            return OpenStatus::Ok;
        }
        std::uint32_t index = fst_.find(disc_path, false);
        if (index == Fst::npos) index = fst_.find(disc_path, true);
        if (index == Fst::npos) {
            error = "no such disc file '" + disc_path + "'";
            return OpenStatus::NotFound;
        }
        const FstEntry& entry = fst_.entries()[index];
        if (entry.is_directory) {
            error = "'" + disc_path + "' is a directory";
            return OpenStatus::Invalid;
        }
        out = std::make_unique<DiscFileSource>(entry.offset, entry.size);
        return OpenStatus::Ok;
    }

    OpenStatus open_external(const std::string& sd_path, std::unique_ptr<ByteSource>& out,
                             std::string& error) override {
        const bool usb = OnUsb(sd_path);
        std::string abs = usb ? sd_path.substr(4) : sd_path;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        const std::string key = (usb ? "usb:" : "sd:") + abs;
        if (usb) {
            const ImageVolume* volume = nullptr;
            if (!ums::Volume(volume, error)) {
                error = "external file '" + sd_path + "': " + error;
                return OpenStatus::IoError;
            }
            auto found = usb_files_.find(key);
            if (found == usb_files_.end()) {
                VolumeFile file;
                if (!volume->lookup(abs, file, error)) {
                    const OpenStatus status = is_missing(error) ? OpenStatus::NotFound : OpenStatus::IoError;
                    error = "external file '" + sd_path + "' " + to_string(status) + ": " + error;
                    return status;
                }
                if (file.entry.is_directory) {
                    error = "'" + key + "' is a directory";
                    return OpenStatus::Invalid;
                }
                found = usb_files_.emplace(key, std::move(file)).first;
            }
            auto source = std::make_unique<UsbFileSource>(found->second, *volume);
            sources_[source.get()] = Placed{&found->second.fragments, true, &found->second.inline_bytes};
            out = std::move(source);
            return OpenStatus::Ok;
        }
        auto found = files_.find(key);
        if (found == files_.end()) {
            Fat32File file;
            bool missing = false;
            const bool ok = resolve_sd_file(key, file, missing, error);
            if (ok && file.entry.is_directory) {
                error = "'" + key + "' is a directory";
                return OpenStatus::Invalid;
            }
            if (!ok) {
                const OpenStatus status = missing ? OpenStatus::NotFound : OpenStatus::IoError;
                error = "external file '" + sd_path + "' " + to_string(status) + ": " + error;
                return status;
            }
            found = files_.emplace(key, std::move(file)).first;
        }
        auto source = std::make_unique<SdFileSource>(found->second);
        sources_[source.get()] = Placed{&found->second.fragments, false, nullptr};
        out = std::move(source);
        return OpenStatus::Ok;
    }

    OpenStatus list_external(const std::string& sd_dir, std::vector<ExternalEntry>& out, std::string& error) override {
        const bool usb = OnUsb(sd_dir);
        std::string abs = usb ? sd_dir.substr(4) : sd_dir;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        out.clear();
        if (usb) {
            const ImageVolume* volume = nullptr;
            if (!ums::Volume(volume, error)) return OpenStatus::IoError;
            std::vector<VolumeEntry> listed;
            if (!volume->list(abs, listed, error)) return is_missing(error) ? OpenStatus::NotFound : OpenStatus::IoError;
            out.reserve(listed.size());
            for (const VolumeEntry& e : listed) {
                ExternalEntry x;
                x.name = e.name;
                x.is_directory = e.is_directory;
                out.push_back(std::move(x));
            }
            return OpenStatus::Ok;
        }
        std::vector<Fat32Entry> entries;
        bool missing = false;
        if (!list_sd_directory("sd:" + abs, entries, missing, error)) {
            return missing ? OpenStatus::NotFound : OpenStatus::IoError;
        }
        out.clear();
        out.reserve(entries.size());
        for (const Fat32Entry& e : entries) {
            ExternalEntry x;
            x.name = e.name;
            x.is_directory = e.is_directory;
            out.push_back(std::move(x));
        }
        return OpenStatus::Ok;
    }

    // The sectors of an external source, from its lookup, and whether
    // they are the USB drive's; or its bytes, for a small NTFS file kept
    // in its MFT record (`in_record` not empty).
    bool fragments_of(const ByteSource* source, const std::vector<Fragment>*& out,
                      const std::vector<std::uint8_t>*& in_record, bool& usb, std::string& error) {
        const auto found = sources_.find(source);
        if (found == sources_.end()) {
            error = "placer: unknown external source";
            return false;
        }
        out = found->second.fragments;
        in_record = found->second.in_record;
        usb = found->second.usb;
        return true;
    }

private:
    const Fst& fst_;
    std::uint64_t dol_offset_ = 0;
    std::uint64_t dol_size_ = 0;
    struct Placed {
        const std::vector<Fragment>* fragments = nullptr;
        bool usb = false;
        const std::vector<std::uint8_t>* in_record = nullptr;
    };
    std::map<std::string, Fat32File> files_;  // node-based: the sources keep references
    std::map<std::string, VolumeFile> usb_files_;
    std::map<const ByteSource*, Placed> sources_;
};

// "sd:/projectm/pf holds: menu2, sound, system": the deepest folder of
// `sd_path` that exists and its first names. Empty when nothing is found.
std::string nearest_on_card(const std::string& sd_path) {
    if (OnUsb(sd_path)) return "";  // the card's folders only
    std::string dir = sd_path;
    if (dir.empty() || dir[0] != '/') dir = "/" + dir;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    for (int depth = 0; depth < 16; ++depth) {
        const std::size_t slash = dir.find_last_of('/');
        dir = slash == 0 ? std::string("/") : dir.substr(0, slash);
        std::vector<Fat32Entry> entries;
        bool missing = false;
        std::string ignored;
        if (list_sd_directory("sd:" + dir, entries, missing, ignored)) {
            std::string names;
            std::size_t shown = 0;
            for (const Fat32Entry& e : entries) {
                if (shown == 12) break;
                names += (shown++ ? ", " : "") + e.name + (e.is_directory ? "/" : "");
            }
            if (entries.size() > shown) names += ", +" + std::to_string(entries.size() - shown) + " more";
            return "sd:" + dir + " holds: " + (names.empty() ? std::string("nothing") : names);
        }
        if (!missing || dir == "/") return "";
    }
    return "";
}

// A pack's folder copied into the XML's own folder (sd:/riivolution/nmg
// for root="/nmg"): each top-level folder the plan names that is not
// on the drive but is beside the XML is read from there. `prefix` is
// "usb:" for a pack on the drive, else empty.
void use_misplaced_folders(Plan& plan, const std::string& xml_sd_path, const std::string& prefix,
                           WiiProvider& provider, CompiledMod& mod) {
    const std::string pack_dir = PackFolderOf(xml_sd_path);
    if (pack_dir == "/" || !NetworkRootOf(xml_sd_path).empty()) return;
    // "/nmg/LayoutData/x.arc" -> "nmg"; a file at the top has no folder.
    const auto top_of = [&](const std::string& path) {
        std::string p = path.compare(0, prefix.size(), prefix) == 0 ? path.substr(prefix.size()) : path;
        const std::size_t from = !p.empty() && p[0] == '/' ? 1 : 0;
        const std::size_t slash = p.find('/', from);
        return slash == std::string::npos ? std::string() : p.substr(from, slash - from);
    };
    std::vector<std::string*> paths;
    for (FilePatch& f : plan.files) paths.push_back(&f.external);
    for (FolderPatch& f : plan.folders) paths.push_back(&f.external);
    for (MemoryPatch& m : plan.memory) paths.push_back(&m.valuefile);
    std::set<std::string> tops;
    for (const std::string* p : paths) {
        const std::string top = top_of(*p);
        if (!top.empty()) tops.insert(top);
    }
    const std::string where = prefix.empty() ? "sd:" : prefix;
    for (const std::string& top : tops) {
        std::vector<ExternalEntry> listed;
        std::string ignored;
        if (provider.list_external(prefix + "/" + top, listed, ignored) != OpenStatus::NotFound) continue;
        const std::string moved = pack_dir + "/" + top;
        if (provider.list_external(prefix + moved, listed, ignored) != OpenStatus::Ok) continue;
        for (std::string* p : paths) {
            if (top_of(*p) != top) continue;
            std::string rest = p->compare(0, prefix.size(), prefix) == 0 ? p->substr(prefix.size()) : *p;
            if (rest.empty() || rest[0] != '/') rest = "/" + rest;
            *p = prefix + pack_dir + rest;
        }
        // The save folder is always on the SD card, with no prefix.
        for (SavegamePatch& s : plan.savegames) {
            std::string rest = s.external;
            if (rest.empty() || rest[0] != '/') rest = "/" + rest;
            if (top_of(rest) == top) s.external = pack_dir + rest;
        }
        const std::string note = where + "/" + top + " is not there; using " + where + moved + " instead";
        mod.notes.push_back(note);
        logf("Mods: %s\n", note.c_str());
    }
}

}  // namespace

// One package: parse, plan with its default choices, expand folders and
// read valuefiles, appending to the combined lists.
// <shift>, after every package's files are laid out: the destination's
// FST entry takes the source's extent where the patches left it (its
// relocation, else its disc entry), as Riivolution points one node at
// the other. A path that is not a disc file is skipped with a note, as
// Riivolution skips it.
static void apply_shifts(const std::vector<ShiftPatch>& shifts, const Fst& fst, CompiledMod& mod) {
    const auto same = [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) == 0; };
    const auto find_file = [&](const std::string& path) {
        std::uint32_t index = fst.find(path, false);
        if (index == Fst::npos) index = fst.find(path, true);
        return index != Fst::npos && fst.entries()[index].is_directory ? Fst::npos : index;
    };
    for (const ShiftPatch& shift : shifts) {
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
        bool have_source = false;
        for (const FstRelocation& r : mod.relocations) {
            if (!same(r.disc_path, shift.source)) continue;
            offset = r.offset;
            size = r.size;
            have_source = true;
        }
        const std::uint32_t source = find_file(shift.source);
        if (!have_source && source != Fst::npos) {
            offset = fst.entries()[source].offset;
            size = fst.entries()[source].size;
            have_source = true;
        }
        std::string destination = shift.destination;
        const std::uint32_t target = find_file(shift.destination);
        if (target != Fst::npos) fst.path_of(target, destination);
        bool placed = false;
        if (have_source) {
            for (FstRelocation& r : mod.relocations) {
                if (!same(r.disc_path, destination)) continue;
                r.offset = offset;
                r.size = size;
                placed = true;
            }
            if (!placed && target != Fst::npos) {
                FstRelocation r;
                r.disc_path = destination;
                r.offset = offset;
                r.size = size;
                mod.relocations.push_back(r);
                placed = true;
            }
        }
        const std::string head = "shift " + shift.source + " -> " + shift.destination + ": ";
        if (!placed) {
            mod.notes.push_back(head + (have_source ? "the destination" : "the source") + " is not a disc file, skipped");
            continue;
        }
        mod.notes.push_back(head + std::to_string(size) + " bytes");
        logf("Mods: %s%u bytes\n", head.c_str(), static_cast<unsigned>(size));
    }
}

static bool gather_package(const PackageSelection& selection, const DiscProbe& probe, const Fst& fst,
                           WiiProvider& provider, std::vector<FilePatch>& files, std::vector<ShiftPatch>& shifts,
                           CompiledMod& mod, std::string& error) {
    const std::string& xml_sd_path = selection.xml_sd_path;
    // A pack on the USB drive is read through d2x (libogc's driver is
    // gone by now), and so are its files.
    const bool on_usb = OnUsb(xml_sd_path);
    std::string text;
    if (on_usb) {
        if (!ums::ReadText(xml_sd_path, text, error)) {
            error = xml_sd_path + ": " + error;
            return false;
        }
    } else {
        std::ifstream xml(xml_sd_path, std::ios::binary);
        if (!xml) {
            error = "cannot open " + xml_sd_path;
            return false;
        }
        std::stringstream all;
        all << xml.rdbuf();
        text = all.str();
    }
    Package package;
    if (!parse_package(text, package, error, PackFolderOf(xml_sd_path))) return false;
    mod.warnings.insert(mod.warnings.end(), package.warnings.begin(), package.warnings.end());
    for (const auto& c : selection.choices) {
        if (!select_choice(package, c.first, c.second, error)) {
            error = xml_sd_path + ": " + error;
            return false;
        }
    }
    const DiscIdentity disc = PackIdentity(probe);
    PlanOptions allowed;
    allowed.allow_filename_targets = true;
    allowed.allow_folders = true;
    allowed.allow_memory = true;
    allowed.allow_savegames = true;
    Plan plan;
    if (!plan_package(package, disc, allowed, plan, error)) return false;
    // A network pack's files are in its cache folder, as on the PC.
    const std::string net_root = NetworkRootOf(xml_sd_path);
    if (!net_root.empty()) RebasePlan(plan, net_root);
    if (on_usb) {
        // Its files are on the drive; a save folder stays on the card.
        const auto to_usb = [](std::string& path) {
            if (!path.empty()) path = std::string(kUsbPrefix) + (path[0] == '/' ? "" : "/") + path;
        };
        for (FilePatch& f : plan.files) to_usb(f.external);
        for (FolderPatch& f : plan.folders) to_usb(f.external);
        for (MemoryPatch& m : plan.memory) to_usb(m.valuefile);
    }
    use_misplaced_folders(plan, xml_sd_path, on_usb ? std::string(kUsbPrefix) : std::string(), provider, mod);

    // <savegame>: one folder per launch; a second, different one is an
    // error rather than a silent choice. `clone` (the default) copies the
    // NAND save into a folder created at this launch; the runtime does
    // that as the game, which may read its own data directory.
    for (const SavegamePatch& sg : plan.savegames) {
        std::string abs = sg.external;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        while (abs.size() > 1 && abs.back() == '/') abs.pop_back();
        const std::string sd = "sd:" + abs;
        if (!mod.savegame_dir.empty() && mod.savegame_dir != sd) {
            error = xml_sd_path + ": <savegame external=\"" + sg.external + "\"> after another save folder (" +
                    mod.savegame_dir + "); one per launch";
            return false;
        }
        mod.savegame_dir = sd;
        if (sg.clone) mod.savegame_clone = true;
        mod.notes.push_back("savegame redirected to " + sd + (sg.clone ? " (NAND save cloned into a new folder)" : " (no clone)"));
    }

    // <folder> patches become <file> patches (listing the card). The note
    // count below lets an all-skipped package report what its folders
    // found instead of looking like a wrong-game refusal.
    const std::size_t expand_notes_from = mod.notes.size();
    std::vector<FilePatch> expanded;
    if (!expand_plan(plan, fst, provider, expanded, mod.notes, error)) return false;
    files.insert(files.end(), expanded.begin(), expanded.end());
    shifts.insert(shifts.end(), plan.shifts.begin(), plan.shifts.end());
    logf("Mods: %s: %u file patch(es), %u memory patch(es)\n", xml_sd_path.c_str(),
         static_cast<unsigned>(expanded.size()), static_cast<unsigned>(plan.memory.size()));

    // Where a pack expected a folder or file the card does not have, what
    // the nearest folder that exists holds, so a misplaced or incomplete
    // copy shows in the log. A few distinct folders at most.
    std::set<std::string> hinted;
    std::string first_missing;  // the first folder or file the card lacks
    for (std::size_t i = expand_notes_from; i < mod.notes.size() && hinted.size() < 4; ++i) {
        const std::string& note = mod.notes[i];
        if (note.find("not on the card") == std::string::npos) continue;
        std::string missing;
        const std::size_t folder = note.find("<folder ");
        const std::size_t quote = note.find("external '");
        if (folder != std::string::npos) {
            const std::size_t arrow = note.find(" -> ", folder);
            if (arrow != std::string::npos) missing = note.substr(folder + 8, arrow - folder - 8);
        } else if (quote != std::string::npos) {
            const std::size_t end = note.find('\'', quote + 10);
            if (end != std::string::npos) missing = note.substr(quote + 10, end - quote - 10);
        }
        if (first_missing.empty()) first_missing = missing;
        const std::string hint = missing.empty() ? std::string() : nearest_on_card(missing);
        if (!hint.empty() && hinted.insert(hint).second) mod.notes.push_back(hint);
    }

    // Memory patches: a valuefile is read now, while the card is mounted.
    // One the card lacks is skipped with a warning, as Dolphin does, unless
    // the pack has no files at all on the card (below): then it is usually
    // the pack's own code, which its other memory patches jump into, so the
    // game would start to a black screen (Neo Mario Galaxy copied into the
    // wrong folder did). An unreadable one stops the launch.
    std::string missing_value;
    for (MemoryPatch m : plan.memory) {
        if (!m.valuefile.empty()) {
            std::unique_ptr<ByteSource> source;
            std::string open_error;
            const OpenStatus opened = provider.open_external(m.valuefile, source, open_error);
            if (opened == OpenStatus::NotFound) {
                std::string warning = "memory patch skipped: its file '" + m.valuefile + "' is not on the card";
                const std::string hint = nearest_on_card(m.valuefile);
                if (!hint.empty()) warning += " (" + hint + ")";
                mod.warnings.push_back(warning);
                if (missing_value.empty()) missing_value = m.valuefile;
                continue;
            }
            if (opened != OpenStatus::Ok || !source) {
                error = "memory patch valuefile: " + open_error;
                return false;
            }
            if (source->size() == 0 || source->size() > kMaxMemoryValueBytes) {
                error = "memory patch valuefile '" + m.valuefile + "' is empty or larger than " +
                        std::to_string(kMaxMemoryValueBytes) + " bytes";
                return false;
            }
            m.value.resize(static_cast<std::size_t>(source->size()));
            if (!source->read(0, m.value.data(), m.value.size())) {
                error = "cannot read memory patch valuefile '" + m.valuefile + "'";
                return false;
            }
            if (m.search && m.value.size() != m.original.size()) {
                error = "memory patch valuefile '" + m.valuefile + "' differs in length from its original";
                return false;
            }
        }
        mod.memory.push_back(std::move(m));
    }
    if (!plan.memory.empty()) mod.notes.push_back(std::to_string(plan.memory.size()) + " memory patch(es)");
    // A pack whose files are not where its XML says (its folder copied one
    // level too deep, or not at all) is not started half applied.
    if (expanded.empty() && (!missing_value.empty() || !first_missing.empty())) {
        const std::string& what = !missing_value.empty() ? missing_value : first_missing;
        const bool usb = OnUsb(what);
        std::string path = usb ? what.substr(4) : what;
        if (path.empty() || path[0] != '/') path = "/" + path;
        const std::string device = usb ? "usb:" : "sd:";
        const std::string top = device + path.substr(0, path.find('/', 1));
        const std::string xml_name = xml_sd_path.substr(xml_sd_path.find_last_of('/') + 1);
        error = "This won't work: " + xml_name + " needs its files in " + top + ", and " +
                (missing_value.empty() ? std::string("none of them are there") : device + path + " is missing") +
                ". Copy the pack's whole folder onto the " + (usb ? "USB drive" : "SD card") +
                " in the same place as the riivolution folder, not inside it.";
        const std::string hint = nearest_on_card(what);
        if (!hint.empty()) error += " (" + hint + ")";
        return false;
    }
    if (expanded.empty() && plan.memory.empty() && plan.savegames.empty() && plan.shifts.empty()) {
        std::vector<std::string> folder_notes(mod.notes.begin() + expand_notes_from, mod.notes.end());
        error = describe_empty_plan(xml_sd_path, probe.header.game_id, plan, folder_notes);
        return false;
    }
    return true;
}

bool homebrew_app_stand_in(const std::vector<PackageSelection>& packages, const std::string& game_id,
                           std::string& app, std::string& dropped) {
    app.clear();
    dropped.clear();
    DiscIdentity disc;
    disc.id = game_id;
    std::string found;
    for (const PackageSelection& selection : packages) {
        const std::string& xml_sd_path = selection.xml_sd_path;
        if (OnUsb(xml_sd_path) || xml_sd_path.compare(0, 4, "sd:/") != 0 || !NetworkRootOf(xml_sd_path).empty())
            return false;
        std::ifstream xml(xml_sd_path, std::ios::binary);
        if (!xml) return false;
        std::stringstream all;
        all << xml.rdbuf();
        Package package;
        std::string error;
        if (!parse_package(all.str(), package, error, PackFolderOf(xml_sd_path))) return false;
        for (const auto& c : selection.choices) {
            if (!select_choice(package, c.first, c.second, error)) return false;
        }
        PlanOptions allowed;
        allowed.allow_filename_targets = true;
        allowed.allow_folders = true;
        allowed.allow_memory = true;
        allowed.allow_savegames = true;
        Plan plan;
        if (!plan_package(package, disc, allowed, plan, error)) return false;
        if (!plan.folders.empty() || !plan.memory.empty() || !plan.shifts.empty()) return false;
        if (!plan.savegames.empty() && dropped.empty()) dropped = "the save redirect";
        for (const FilePatch& f : plan.files) {
            const bool main_dol = f.is_filename && strcasecmp(f.disc.c_str(), "main.dol") == 0;
            std::string external = OnUsb(f.external) ? std::string() : f.external;
            if (!external.empty() && external[0] != '/') external = "/" + external;
            const bool from_apps = external.size() > 10 && strncasecmp(external.c_str(), "/apps/", 6) == 0 &&
                                   strcasecmp(external.c_str() + external.size() - 4, ".dol") == 0 &&
                                   external.find('/', 6) != std::string::npos;
            if (!main_dol || !from_apps || !found.empty() || f.offset != 0 || f.file_offset != 0 ||
                f.length != 0)
                return false;
            found = external;
        }
    }
    if (found.empty()) return false;
    struct stat st;
    if (stat(("sd:" + found).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    app = "sd:" + found;
    return true;
}

bool compile_packages(const std::vector<PackageSelection>& packages, const DiscProbe& probe,
                      const OpenedPartition& partition, CompiledMod& out, std::string& error) {
    CompiledMod mod;
    const Fst& fst = partition.fst;
    // Network packs: what the chosen options need comes from the PC first.
    ProgressStage("Reading the mod packs", 8);
    if (!SyncNetworkPackages(packages, probe, mod.warnings, error)) return false;
    // Listings from an earlier compile may predate files libfat wrote since.
    forget_sd_layout();
    WiiProvider provider(fst);

    // 1. Every package's file patches, in package then document order,
    //    and its memory patches.
    std::vector<FilePatch> files;
    std::vector<ShiftPatch> shifts;
    for (const PackageSelection& selection : packages) {
        ProgressWithin(mod.xml_paths.size(), packages.size(), 10, 25);
        mod.xml_paths.push_back(selection.xml_sd_path);
        if (!gather_package(selection, probe, fst, provider, files, shifts, mod, error)) return false;
    }
    if (files.empty()) {
        apply_shifts(shifts, fst, mod);
        out = std::move(mod);
        error.clear();
        return true;
    }

    // 2. Bare file names become FST paths and existing paths take the
    //    disc's spelling; then group by disc file, in the order the files
    //    first appear, so patches on one file compose in order wherever
    //    they came from.
    std::uint64_t window_cursor = kVirtualWindowStart;
    std::vector<std::string> order;
    std::map<std::string, std::vector<FilePatch>> groups;
    const auto add = [&](const FilePatch& patch) {
        if (groups.find(patch.disc) == groups.end()) order.push_back(patch.disc);
        groups[patch.disc].push_back(patch);
    };
    std::vector<FilePatch> main_dol;  // patches on the executable, in order
    // What Riivolution skips rather than refuses is skipped here too, with a
    // note so the preflight screen and boot.log still say so: a file whose
    // external is not on the card, a bare name no disc file carries, and
    // (step 3) a disc path this disc lacks when nothing creates it. Packs
    // covering several regions or optional content depend on this. Any
    // other failure (an unreadable card, a malformed path) still stops
    // the launch.
    for (FilePatch patch : files) {
        {
            std::unique_ptr<ByteSource> external;
            std::string open_error;
            if (provider.open_external(patch.external, external, open_error) == OpenStatus::NotFound) {
                mod.notes.push_back(patch.disc + ": external '" + patch.external + "' not on the card, skipped");
                continue;
            }
        }
        if (patch.is_filename && patch.disc.size() == 8 && strcasecmp(patch.disc.c_str(), "main.dol") == 0) {
            // The bare name "main.dol" is the game's executable, not a
            // file search (Riivolution and Dolphin treat it so).
            patch.is_filename = false;
            patch.disc = kMainDolPath;
            main_dol.push_back(patch);
            continue;
        }
        if (patch.is_filename) {
            // A bare name is a search: every disc file called that is
            // patched (as a by-name <folder> does).
            const std::vector<std::uint32_t> matches = fst.find_files_named(patch.disc, true);
            if (matches.empty()) {
                mod.notes.push_back("no disc file is called '" + patch.disc + "', skipped");
                continue;
            }
            patch.is_filename = false;
            for (std::uint32_t m : matches) {
                FilePatch one = patch;
                if (!fst.path_of(m, one.disc)) {
                    error = "cannot name '" + patch.disc + "'";
                    return false;
                }
                add(one);
            }
            continue;
        }
        const std::uint32_t index = fst.find(patch.disc, true);
        if (index != Fst::npos && !fst.path_of(index, patch.disc)) {
            error = "cannot name '" + patch.disc + "'";
            return false;
        }
        add(patch);
    }

    // The executable: patched in full now, read by the apploader from
    // memory at the launch (wii/boot.cpp), never by the game again.
    if (!main_dol.empty()) {
        std::uint8_t head[kDolHeaderBytes];
        DolHeader original;
        di::PartitionSource data;
        if (!data.read(partition.data_header.dol_offset, head, sizeof(head)) ||
            !parse_dol_header(head, sizeof(head), original, error)) {
            error = "main.dol: cannot read the disc's DOL header: " + error;
            return false;
        }
        provider.set_main_dol(partition.data_header.dol_offset, original.image_size());
        std::unique_ptr<AppliedFile> file;
        if (!apply_patches(main_dol, provider, file, error)) {
            error = "main.dol: " + error;
            return false;
        }
        if (file->size() < kDolHeaderBytes || file->size() > 0x1000000) {
            error = "main.dol: the patched executable is " + std::to_string(file->size()) + " bytes";
            return false;
        }
        mod.main_dol.resize(static_cast<std::size_t>(file->size()));
        DolHeader patched;
        // A read that fails says nothing itself (short of memory, or the
        // card): not "not a valid DOL" with an empty reason, as 2.7.0 RC3 said.
        if (!file->read(0, mod.main_dol.data(), mod.main_dol.size())) {
            error = "main.dol: cannot read the patched executable (" + std::to_string(file->size()) +
                    " bytes) from the pack's files; RiftWii may be short of memory";
            return false;
        }
        if (!parse_dol_header(mod.main_dol.data(), mod.main_dol.size(), patched, error)) {
            error = "main.dol: the patched executable is not a valid DOL: " + error;
            return false;
        }
        if (patched.image_size() > mod.main_dol.size()) {
            error = "main.dol: the patched executable's sections run past its end";
            return false;
        }
        char note[96];
        std::snprintf(note, sizeof(note), "main.dol: %llu -> %u bytes, entry 0x%08x",
                      static_cast<unsigned long long>(original.image_size()),
                      static_cast<unsigned>(mod.main_dol.size()), static_cast<unsigned>(patched.entry));
        mod.notes.push_back(note);
        logf("Mods: %s\n", note);
    }

    // 3. Apply each group and lay the result out: same size stays in place,
    //    anything else moves into the virtual window.
    std::vector<std::unique_ptr<AppliedFile>> applied;
    std::vector<VirtualFileLayout> layouts;
    constexpr std::uint64_t kWindowEnd = 0x400000000ull;
    // Progress for big mods: every line is a card write, so one per
    // kProgressStep files.
    constexpr std::size_t kProgressStep = 250;
    std::size_t done = 0;
    ProgressStage("Patching the game's files", 25);
    for (const std::string& disc_path : order) {
        ProgressWithin(done, order.size(), 25, 55);
        if (++done % kProgressStep == 0) {
            logf("Mods: %u of %u disc files patched\n", static_cast<unsigned>(done),
                 static_cast<unsigned>(order.size()));
        }
        std::uint32_t index = fst.find(disc_path, false);
        if (index == Fst::npos) index = fst.find(disc_path, true);
        const bool create = index == Fst::npos;
        if (create) {
            // Only patches that may create the file apply to a missing one.
            std::vector<FilePatch>& group = groups[disc_path];
            const std::size_t before = group.size();
            group.erase(std::remove_if(group.begin(), group.end(), [](const FilePatch& p) { return !p.create; }),
                        group.end());
            if (group.empty()) {
                mod.notes.push_back(disc_path + ": not on the disc, skipped");
                continue;
            }
            if (group.size() != before) {
                mod.notes.push_back(disc_path + ": not on the disc; " + std::to_string(before - group.size()) +
                                    " patch(es) without create skipped");
            }
        }
        // A created file starts empty (apply_patches does that for
        // create="true") and always takes a slot in the window.
        const FstEntry entry = create ? FstEntry() : fst.entries()[index];
        std::unique_ptr<AppliedFile> file;
        if (!apply_patches(groups[disc_path], provider, file, error)) return false;
        VirtualFileLayout layout;
        layout.file = file.get();
        layout.original_offset = entry.offset;
        const std::uint64_t size = file->size();
        if (!create && size == entry.size) {
            layout.virtual_offset = entry.offset;
            mod.notes.push_back(disc_path + ": " + std::to_string(size) + " bytes, in place");
        } else {
            // A file of no bytes (a mod nulling a video, say) takes an
            // entry with size 0 and no window space.
            const std::uint64_t padded = (size + 31) & ~std::uint64_t(31);
            if (size > 0xFFFFFFFFull || kWindowEnd - window_cursor < padded) {
                error = "'" + disc_path + "' (" + std::to_string(size) + " bytes) does not fit the virtual window";
                return false;
            }
            layout.virtual_offset = window_cursor;
            FstRelocation relocation;
            relocation.disc_path = disc_path;
            relocation.offset = window_cursor;
            relocation.size = static_cast<std::uint32_t>(size);
            relocation.create = create;
            mod.relocations.push_back(relocation);
            window_cursor += padded;
            mod.notes.push_back(create ? disc_path + ": " + std::to_string(size) + " bytes, created"
                                       : disc_path + ": " + std::to_string(entry.size) + " -> " +
                                             std::to_string(size) + " bytes, relocated");
        }
        layouts.push_back(layout);
        applied.push_back(std::move(file));
    }

    if (layouts.empty()) {  // every file patch was skipped above
        apply_shifts(shifts, fst, mod);
        out = std::move(mod);
        error.clear();
        return true;
    }

    // 4. External bytes to SD sectors, and the table.
    logf("Mods: %u disc file(s) patched; mapping them to the card\n", static_cast<unsigned>(layouts.size()));
    ProgressStage("Finding the files on the SD card", 56);
    // A small NTFS file has no sectors to point at: its bytes go into the
    // game's memory instead. Placed as MEM runs whose source is an offset
    // into `in_memory` until they are split off below.
    std::vector<std::uint8_t> in_memory;
    const ExternalPlacer placer = [&](const ByteSource* external, std::uint64_t source_offset, std::uint64_t length,
                                      std::vector<PlacedRun>& runs, std::string& e) {
        const std::vector<Fragment>* fragments = nullptr;
        const std::vector<std::uint8_t>* in_record = nullptr;
        bool usb = false;
        if (!provider.fragments_of(external, fragments, in_record, usb, e)) return false;
        if (in_record != nullptr && !in_record->empty()) {
            if (source_offset > in_record->size() || length > in_record->size() - source_offset) {
                e = "range extends past the file";
                return false;
            }
            PlacedRun r;
            r.kind = RT_KIND_MEM;
            r.length = length;
            r.source = in_memory.size();
            in_memory.insert(in_memory.end(), in_record->begin() + source_offset,
                             in_record->begin() + source_offset + length);
            runs.assign(1, r);
            return true;
        }
        return place_on_fragments(*fragments, source_offset, length, runs, e, usb ? RT_KIND_USB : RT_KIND_SD);
    };
    std::vector<std::uint8_t> table;
    if (!build_redirect_table(layouts, placer, 0xFFFFFFFFu, static_cast<std::uint64_t>(probe.partition.offset), table,
                              error)) {
        return false;
    }
    const auto* header = reinterpret_cast<const rt_header*>(table.data());
    const int status = rt_validate(header, table.size());
    if (status != RT_OK) {
        error = "compiled table failed validation: " + std::to_string(status);
        return false;
    }
    mod.entries.clear();
    for (const rt_entry* e = rt_entries(header); e != rt_entries(header) + header->entry_count; ++e) {
        if (e->kind != RT_KIND_MEM) {
            mod.entries.push_back(*e);
            continue;
        }
        MemReplacement m;
        m.virtual_offset = e->vstart;
        m.bytes.assign(in_memory.begin() + e->source, in_memory.begin() + e->source + e->length);
        mod.mem.push_back(std::move(m));
    }
    if (!in_memory.empty())
        mod.notes.push_back(std::to_string(in_memory.size()) + " bytes of small NTFS files go into memory");
    apply_shifts(shifts, fst, mod);
    out = std::move(mod);
    error.clear();
    return true;
}

}  // namespace riftwii::wii
