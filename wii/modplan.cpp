// SPDX-License-Identifier: GPL-3.0-or-later
#include "modplan.hpp"

#include <fstream>
#include <map>
#include <memory>

#include "di.hpp"
#include "log.hpp"
#include "riftwii/apply.hpp"
#include "riftwii/expand.hpp"
#include "riftwii/fat32.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/mempatch.hpp"
#include "riftwii/patch.hpp"
#include "riftwii/redirect.hpp"
#include "riftwii/source.hpp"
#include "sdfile.hpp"

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

// The disc side comes from the FST, the SD side from libfat; the SD paths
// are remembered per source so the placer can resolve their sectors.
class WiiProvider final : public ContentProvider {
public:
    explicit WiiProvider(const Fst& fst) : fst_(fst) {}

    OpenStatus open_disc(const std::string& disc_path, std::unique_ptr<ByteSource>& out,
                         std::string& error) override {
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
        std::string abs = sd_path;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        std::unique_ptr<FileByteSource> f;
        const OpenStatus status = FileByteSource::open("sd:" + abs, f, error);
        if (status != OpenStatus::Ok) {
            error = "external file '" + sd_path + "' " + to_string(status) + ": " + error;
            return status;
        }
        paths_[f.get()] = "sd:" + abs;
        out = std::move(f);
        return OpenStatus::Ok;
    }

    bool list_external(const std::string& sd_dir, std::vector<ExternalEntry>& out, std::string& error) override {
        std::string abs = sd_dir;
        if (abs.empty() || abs[0] != '/') abs = "/" + abs;
        return list_native_directory("sd:" + abs, out, error);
    }

    // The sectors of an external source, resolved once per path.
    bool fragments_of(const ByteSource* source, const std::vector<Fragment>*& out, std::string& error) {
        const auto path = paths_.find(source);
        if (path == paths_.end()) {
            error = "placer: unknown external source";
            return false;
        }
        auto cached = fragments_.find(path->second);
        if (cached == fragments_.end()) {
            Fat32File file;
            if (!resolve_sd_file(path->second, file, error)) return false;
            cached = fragments_.emplace(path->second, std::move(file.fragments)).first;
        }
        out = &cached->second;
        return true;
    }

private:
    const Fst& fst_;
    std::map<const ByteSource*, std::string> paths_;
    std::map<std::string, std::vector<Fragment>> fragments_;
};

}  // namespace

// One package: parse, plan with its default choices, expand folders and
// read valuefiles, appending to the combined lists.
static bool gather_package(const std::string& xml_sd_path, const DiscProbe& probe, const Fst& fst,
                           WiiProvider& provider, std::vector<FilePatch>& files, CompiledMod& mod,
                           std::string& error) {
    std::ifstream xml(xml_sd_path, std::ios::binary);
    if (!xml) {
        error = "cannot open " + xml_sd_path;
        return false;
    }
    Package package;
    if (!read_package(xml, package, error)) return false;
    mod.warnings.insert(mod.warnings.end(), package.warnings.begin(), package.warnings.end());
    const DiscIdentity disc = probe.header.identity();
    PlanOptions allowed;
    allowed.allow_filename_targets = true;
    allowed.allow_folders = true;
    allowed.allow_memory = true;
    Plan plan;
    if (!plan_package(package, disc, allowed, plan, error)) return false;

    // <folder> patches become <file> patches (listing the card).
    std::vector<FilePatch> expanded;
    if (!expand_plan(plan, fst, provider, expanded, mod.notes, error)) return false;
    files.insert(files.end(), expanded.begin(), expanded.end());

    // Memory patches: a valuefile is read now, while the card is mounted.
    for (MemoryPatch m : plan.memory) {
        if (!m.valuefile.empty()) {
            std::unique_ptr<ByteSource> source;
            std::string open_error;
            if (provider.open_external(m.valuefile, source, open_error) != OpenStatus::Ok || !source) {
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
        }
        mod.memory.push_back(std::move(m));
    }
    if (!plan.memory.empty()) mod.notes.push_back(std::to_string(plan.memory.size()) + " memory patch(es)");
    if (expanded.empty() && plan.memory.empty()) {
        error = xml_sd_path + " does not apply to " + probe.header.game_id + " (nothing selected)";
        return false;
    }
    return true;
}

bool compile_packages(const std::vector<std::string>& xml_sd_paths, const DiscProbe& probe,
                      const OpenedPartition& partition, CompiledMod& out, std::string& error) {
    CompiledMod mod;
    mod.xml_paths = xml_sd_paths;
    const Fst& fst = partition.fst;
    WiiProvider provider(fst);

    // 1. Every package's file patches, in package then document order,
    //    and its memory patches.
    std::vector<FilePatch> files;
    for (const std::string& path : xml_sd_paths) {
        if (!gather_package(path, probe, fst, provider, files, mod, error)) return false;
    }
    if (files.empty()) {
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
    for (FilePatch patch : files) {
        if (patch.is_filename) {
            const std::vector<std::uint32_t> matches = fst.find_files_named(patch.disc, true);
            if (matches.size() != 1) {
                error = "'" + patch.disc + "' matches " + std::to_string(matches.size()) + " disc files";
                return false;
            }
            if (!fst.path_of(matches[0], patch.disc)) {
                error = "cannot name '" + patch.disc + "'";
                return false;
            }
            patch.is_filename = false;
        } else {
            const std::uint32_t index = fst.find(patch.disc, true);
            if (index != Fst::npos && !fst.path_of(index, patch.disc)) {
                error = "cannot name '" + patch.disc + "'";
                return false;
            }
        }
        if (groups.find(patch.disc) == groups.end()) order.push_back(patch.disc);
        groups[patch.disc].push_back(patch);
    }

    // 3. Apply each group and lay the result out: same size stays in place,
    //    anything else moves into the virtual window.
    std::vector<std::unique_ptr<AppliedFile>> applied;
    std::vector<VirtualFileLayout> layouts;
    constexpr std::uint64_t kWindowEnd = 0x400000000ull;
    for (const std::string& disc_path : order) {
        std::uint32_t index = fst.find(disc_path, false);
        if (index == Fst::npos) index = fst.find(disc_path, true);
        const bool create = index == Fst::npos;
        if (create && !groups[disc_path].front().create) {
            error = "'" + disc_path + "' is not on the disc";
            return false;
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
            const std::uint64_t padded = (size + 31) & ~std::uint64_t(31);
            if (size == 0 || size > 0xFFFFFFFFull || window_cursor + padded > kWindowEnd) {
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

    // 4. External bytes to SD sectors, and the table.
    const ExternalPlacer placer = [&](const ByteSource* external, std::uint64_t source_offset, std::uint64_t length,
                                      std::vector<PlacedRun>& runs, std::string& e) {
        const std::vector<Fragment>* fragments = nullptr;
        if (!provider.fragments_of(external, fragments, e)) return false;
        return place_on_fragments(*fragments, source_offset, length, runs, e);
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
    mod.entries.assign(rt_entries(header), rt_entries(header) + header->entry_count);
    out = std::move(mod);
    error.clear();
    return true;
}

}  // namespace riftwii::wii
