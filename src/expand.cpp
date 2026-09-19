// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/expand.hpp"

#include <algorithm>

namespace riftwii {
namespace {

// Directory nesting a folder patch may walk; deeper trees are refused so a
// looping card cannot recurse forever.
constexpr unsigned kMaxFolderDepth = 32;

char fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool name_less(const ExternalEntry& a, const ExternalEntry& b) {
    const std::size_t n = std::min(a.name.size(), b.name.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char x = fold(a.name[i]);
        const char y = fold(b.name[i]);
        if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
    }
    if (a.name.size() != b.name.size()) return a.name.size() < b.name.size();
    return a.name < b.name;
}

bool valid_entry_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if (c == '/' || c == '\\' || c < 0x20) return false;
    }
    return true;
}

bool list_sorted(ContentProvider& provider, const std::string& sd_dir, std::vector<ExternalEntry>& out,
                 std::string& error) {
    out.clear();
    if (!provider.list_external(sd_dir, out, error)) {
        error = "cannot list '" + sd_dir + "': " + error;
        return false;
    }
    std::vector<ExternalEntry> kept;
    for (ExternalEntry& e : out) {
        if (valid_entry_name(e.name)) kept.push_back(std::move(e));
    }
    std::sort(kept.begin(), kept.end(), name_less);
    out = std::move(kept);
    return true;
}

FilePatch make_file(const FolderPatch& folder, const std::string& disc_path, const std::string& external,
                    bool create) {
    FilePatch f;
    f.disc = disc_path;
    f.external = external;
    f.length = folder.length;
    f.resize = folder.resize;
    f.create = create;
    return f;
}

struct Expansion {
    std::vector<FilePatch> files;
    unsigned replaced = 0;
    unsigned created = 0;
    unsigned skipped = 0;
};

// Rooted form: `disc_dir` is the disc directory (its own spelling when it
// exists, the patch's when it is being created) matched against `sd_dir`.
bool walk_rooted(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, const std::string& sd_dir,
                 const std::string& disc_dir, unsigned depth, Expansion& x, std::string& error) {
    if (depth > kMaxFolderDepth) {
        error = "'" + sd_dir + "' is nested too deeply";
        return false;
    }
    std::vector<ExternalEntry> entries;
    if (!list_sorted(provider, sd_dir, entries, error)) return false;
    for (const ExternalEntry& e : entries) {
        const std::string sd_path = sd_dir + "/" + e.name;
        const std::string disc_path = disc_dir + "/" + e.name;
        const std::uint32_t index = fst.find(disc_path, true);
        const bool on_disc = index != Fst::npos;
        const bool disc_is_dir = on_disc && fst.entries()[index].is_directory;
        if (e.is_directory) {
            if (!folder.recursive) continue;
            if (on_disc && !disc_is_dir) {
                ++x.skipped;  // a file on the disc where the card has a folder
                continue;
            }
            if (!on_disc && !folder.create) {
                ++x.skipped;
                continue;
            }
            std::string spelled = disc_path;
            if (on_disc && !fst.path_of(index, spelled)) {
                error = "cannot name '" + disc_path + "'";
                return false;
            }
            if (!walk_rooted(folder, fst, provider, sd_path, spelled, depth + 1, x, error)) return false;
            continue;
        }
        if (on_disc) {
            if (disc_is_dir) {
                ++x.skipped;  // a folder on the disc where the card has a file
                continue;
            }
            std::string spelled;
            if (!fst.path_of(index, spelled)) {
                error = "cannot name '" + disc_path + "'";
                return false;
            }
            x.files.push_back(make_file(folder, spelled, sd_path, false));
            ++x.replaced;
        } else if (folder.create) {
            x.files.push_back(make_file(folder, disc_path, sd_path, true));
            ++x.created;
        } else {
            ++x.skipped;
        }
    }
    return true;
}

// Filename search: each external file replaces every disc file of its name.
bool search_by_name(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, Expansion& x,
                    std::string& error) {
    std::vector<ExternalEntry> entries;
    if (!list_sorted(provider, folder.external, entries, error)) return false;
    for (const ExternalEntry& e : entries) {
        if (e.is_directory) continue;
        const std::vector<std::uint32_t> matches = fst.find_files_named(e.name, true);
        if (matches.empty()) {
            ++x.skipped;
            continue;
        }
        for (std::uint32_t m : matches) {
            std::string spelled;
            if (!fst.path_of(m, spelled)) {
                error = "cannot name a disc file called '" + e.name + "'";
                return false;
            }
            x.files.push_back(make_file(folder, spelled, folder.external + "/" + e.name, false));
            ++x.replaced;
        }
    }
    return true;
}

bool expand_folder(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, Expansion& x,
                   std::string& error) {
    const bool rooted = !folder.disc.empty() && folder.disc[0] == '/';
    if (!rooted) return search_by_name(folder, fst, provider, x, error);
    std::string disc_dir = folder.disc;
    const std::uint32_t index = fst.find(folder.disc, true);
    if (index != Fst::npos) {
        if (!fst.entries()[index].is_directory) {
            error = "folder target '" + folder.disc + "' is a file on the disc";
            return false;
        }
        if (!fst.path_of(index, disc_dir)) {
            error = "cannot name '" + folder.disc + "'";
            return false;
        }
    } else if (!folder.create) {
        error = "folder target '" + folder.disc + "' is not on the disc";
        return false;
    }
    if (disc_dir == "/") disc_dir.clear();  // so the root's children are "/name"
    return walk_rooted(folder, fst, provider, folder.external, disc_dir, 0, x, error);
}

}  // namespace

bool expand_plan(const Plan& plan, const Fst& fst, ContentProvider& provider, std::vector<FilePatch>& out,
                 std::vector<std::string>& notes, std::string& error) {
    std::vector<FilePatch> files;
    std::vector<std::string> lines;
    // Plans built in code may leave `order` empty (plan_package always fills
    // it); then the kinds go one after the other, files first.
    std::vector<PatchStep> order = plan.order;
    if (order.empty()) {
        for (std::size_t i = 0; i < plan.files.size(); ++i) order.push_back(PatchStep{PatchKind::File, i});
        for (std::size_t i = 0; i < plan.folders.size(); ++i) order.push_back(PatchStep{PatchKind::Folder, i});
    }
    for (const PatchStep& step : order) {
        switch (step.kind) {
        case PatchKind::File:
            if (step.index >= plan.files.size()) {
                error = "corrupt plan order";
                return false;
            }
            files.push_back(plan.files[step.index]);
            break;
        case PatchKind::Folder: {
            if (step.index >= plan.folders.size()) {
                error = "corrupt plan order";
                return false;
            }
            const FolderPatch& folder = plan.folders[step.index];
            Expansion x;
            if (!expand_folder(folder, fst, provider, x, error)) return false;
            files.insert(files.end(), x.files.begin(), x.files.end());
            lines.push_back("<folder " + folder.external + " -> " + (folder.disc.empty() ? "by name" : folder.disc) +
                            ">: " + std::to_string(x.replaced) + " replaced, " + std::to_string(x.created) +
                            " created, " + std::to_string(x.skipped) + " skipped");
            break;
        }
        case PatchKind::Memory:
        case PatchKind::Savegame:
            break;
        }
    }
    out = std::move(files);
    notes.insert(notes.end(), lines.begin(), lines.end());
    error.clear();
    return true;
}

}  // namespace riftwii
