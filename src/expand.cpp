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

bool same_folded(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
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

// "/dir" + "name" -> "/dir/name"; the root ("" or "/") gives "/name".
std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/") return "/" + name;
    return dir + "/" + name;
}

std::string without_trailing_slash(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

// Ok, NotFound (the folder is not on the card) or IoError, with `error`
// naming the folder for the last two.
OpenStatus list_sorted(ContentProvider& provider, const std::string& sd_dir, std::vector<ExternalEntry>& out,
                       std::string& error) {
    out.clear();
    const OpenStatus status = provider.list_external(sd_dir, out, error);
    if (status != OpenStatus::Ok) {
        error = "cannot list '" + sd_dir + "': " + error;
        return status == OpenStatus::NotFound ? OpenStatus::NotFound : OpenStatus::IoError;
    }
    std::vector<ExternalEntry> kept;
    for (ExternalEntry& e : out) {
        if (valid_entry_name(e.name)) kept.push_back(std::move(e));
    }
    std::sort(kept.begin(), kept.end(), name_less);
    out = std::move(kept);
    return OpenStatus::Ok;
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
    bool missing = false;  // the external folder itself is not on the card
};

// The child of directory `kids` (the FST indices) called `name`, matched
// case-insensitively; npos when there is none.
std::uint32_t child_named(const Fst& fst, const std::vector<std::uint32_t>& kids, const std::string& name) {
    for (std::uint32_t k : kids) {
        if (same_folded(fst.entries()[k].name, name)) return k;
    }
    return Fst::npos;
}

// Rooted form: `disc_dir` is the disc directory (its own spelling when it
// exists, the patch's when it is being created; `disc_index` is its FST
// entry or npos) matched against `sd_dir`. The directory's children are
// listed once and matched by name.
bool walk_rooted(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, const std::string& sd_dir,
                 const std::string& disc_dir, std::uint32_t disc_index, unsigned depth, Expansion& x,
                 std::string& error) {
    if (depth > kMaxFolderDepth) {
        error = "'" + sd_dir + "' is nested too deeply";
        return false;
    }
    std::vector<ExternalEntry> entries;
    const OpenStatus listed = list_sorted(provider, sd_dir, entries, error);
    if (listed == OpenStatus::NotFound && depth == 0) {
        // Riivolution skips a folder patch whose external folder does not
        // exist; packs rely on it for optional or per-region content. A
        // subfolder vanishing mid-walk (depth > 0) is a card problem.
        x.missing = true;
        error.clear();
        return true;
    }
    if (listed != OpenStatus::Ok) return false;
    std::vector<std::uint32_t> kids;
    if (disc_index != Fst::npos && !fst.children(disc_index, kids)) {
        error = "fst directory '" + disc_dir + "' is corrupt";
        return false;
    }
    for (const ExternalEntry& e : entries) {
        const std::string sd_path = join(sd_dir, e.name);
        const std::uint32_t index = child_named(fst, kids, e.name);
        const bool on_disc = index != Fst::npos;
        const bool disc_is_dir = on_disc && fst.entries()[index].is_directory;
        // What exists keeps the disc's spelling; what is created takes the card's.
        const std::string disc_path = join(disc_dir, on_disc ? fst.entries()[index].name : e.name);
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
            if (!walk_rooted(folder, fst, provider, sd_path, disc_path, index, depth + 1, x, error)) return false;
            continue;
        }
        if (on_disc) {
            if (disc_is_dir) {
                ++x.skipped;  // a folder on the disc where the card has a file
                continue;
            }
            x.files.push_back(make_file(folder, disc_path, sd_path, false));
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
bool search_by_name(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, const std::string& sd_dir,
                    Expansion& x, std::string& error) {
    std::vector<ExternalEntry> entries;
    const OpenStatus listed = list_sorted(provider, sd_dir, entries, error);
    if (listed == OpenStatus::NotFound) {
        x.missing = true;  // skipped, as for a rooted folder
        error.clear();
        return true;
    }
    if (listed != OpenStatus::Ok) return false;
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
            x.files.push_back(make_file(folder, spelled, join(sd_dir, e.name), false));
            ++x.replaced;
        }
    }
    return true;
}

bool expand_folder(const FolderPatch& folder, const Fst& fst, ContentProvider& provider, Expansion& x,
                   std::string& error) {
    const std::string sd_dir = without_trailing_slash(folder.external);
    const std::string disc = without_trailing_slash(folder.disc);
    const bool rooted = !disc.empty() && disc[0] == '/';
    if (!rooted) return search_by_name(folder, fst, provider, sd_dir, x, error);
    std::string disc_dir = disc;
    const std::uint32_t index = fst.find(disc, true);
    if (index != Fst::npos) {
        if (!fst.entries()[index].is_directory) {
            error = "folder target '" + folder.disc + "' is a file on the disc";
            return false;
        }
        if (!fst.path_of(index, disc_dir)) {
            error = "cannot name '" + folder.disc + "'";
            return false;
        }
    }
    // A package can list equivalent directories for several game regions.
    // If this disc has no such directory and creation is disabled, the walk
    // records the external entries as skipped.  Other folder patches in the
    // same package can still apply to the directories for this disc.
    return walk_rooted(folder, fst, provider, sd_dir, disc_dir, index, 0, x, error);
}

}  // namespace

std::string describe_empty_plan(const std::string& xml_sd_path, const std::string& game_id, const Plan& plan,
                                const std::vector<std::string>& folder_notes) {
    if (plan.files.empty() && plan.folders.empty() && plan.memory.empty() && plan.savegames.empty()) {
        return xml_sd_path + ": no patches selected for " + game_id + " (all options are off)";
    }
    std::string error = xml_sd_path + ": no files matched on " + game_id;
    for (const std::string& line : folder_notes) error += "; " + line;
    error += "; check the external folders exist and filenames match the disc (new files need create=\"true\")";
    return error;
}

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
            const std::string head =
                "<folder " + folder.external + " -> " + (folder.disc.empty() ? "by name" : folder.disc) + ">: ";
            lines.push_back(x.missing ? head + "not on the card, skipped"
                                      : head + std::to_string(x.replaced) + " replaced, " +
                                            std::to_string(x.created) + " created, " + std::to_string(x.skipped) +
                                            " skipped");
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
