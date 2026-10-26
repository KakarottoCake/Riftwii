// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "packcatalog.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "log.hpp"

namespace riftwii::wii {
namespace {

// Do this during Home's scan: lowercasing every XML again on each game
// page costs time even when none of the packs belongs to that game.
void ExcludePackCodeBuilds(const std::string& text, std::vector<CodeBuildFile>& builds) {
    const bool has_candidates = std::any_of(builds.begin(), builds.end(), [](const CodeBuildFile& build) {
        return !build.picked && build.gct.compare(0, 4, "sd:/") == 0 && !build.name.empty();
    });
    if (!has_candidates) return;
    const auto mentions = [&](const std::string& folder) {
        return std::search(text.begin(), text.end(), folder.begin(), folder.end(), [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == static_cast<unsigned char>(right);
        }) != text.end();
    };
    builds.erase(std::remove_if(builds.begin(), builds.end(), [&](const CodeBuildFile& build) {
        if (build.picked || build.gct.compare(0, 4, "sd:/") != 0 || build.name.empty()) return false;
        std::string folder = build.name;
        for (char& c : folder) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool owns = mentions("\"/" + folder + "/") || mentions("\"/" + folder + "\"") ||
                          mentions("\"" + folder + "/");
        if (owns) {
            logf("Code builds: %s is in a pack's folder (an XML reads sd:/%s), not listed\n",
                 build.gct.c_str(), build.name.c_str());
        }
        return owns;
    }), builds.end());
}

}  // namespace

PackageCatalog ReadPackageCatalog(PackIndex* index) {
    PackageCatalog catalog;
    if (index) *index = PackIndex{};
    // Home's index admits 256 files; the game's option browser uses the
    // first 150. Preserve that difference when handing the snapshot over.
    catalog.files = ListPackFiles(256, catalog.limited, true);
    catalog.code_builds = ListCodeBuilds();
    if (index) {
        for (const CodeBuildFile& build : catalog.code_builds) index->add_game(build.game_id);
    }
    std::size_t cached_bytes = 0;
    for (const PackFile& pack : catalog.files) cached_bytes += pack.text.size();
    for (PackFile& pack : catalog.files) {
        std::string uncached_text;
        if (!pack.text_cached) uncached_text = ReadPackText(pack.path);
        const std::string& text = pack.text_cached ? pack.text : uncached_text;
        LaunchModel parsed;
        parsed.add(pack.file, pack.path, text, nullptr);
        CatalogPackage entry;
        if (!parsed.packages.empty()) {
            entry.listed = true;
            entry.summary = std::move(parsed.packages.front());
            if (index) {
                if (entry.summary.valid) index->add_filter(entry.summary.package.filter);
                else if (!text.empty()) index->add(text);
            }
            // A second full model would exhaust the Wii's heap. Only
            // metadata for rejecting unrelated packs survives the scan.
            Package summary;
            summary.filter = std::move(entry.summary.package.filter);
            entry.summary.package = std::move(summary);
        }
        if (catalog.packages.size() < kMaxGamePackages) ExcludePackCodeBuilds(text, catalog.code_builds);
        catalog.packages.push_back(std::move(entry));
        if (!pack.text_cached && text.size() <= kPackTextCacheBytes - cached_bytes) {
            cached_bytes += text.size();
            pack.text.swap(uncached_text);
            pack.text_cached = true;
        }
    }
    catalog.ready = true;
    return catalog;
}

void AddCatalogPackages(const PackageCatalog& catalog, LaunchModel& model, const DiscIdentity* disc) {
    const std::size_t count = std::min(kMaxGamePackages, catalog.files.size());
    for (std::size_t i = 0; i < count; ++i) {
        const PackFile& pack = catalog.files[i];
        const CatalogPackage& entry = catalog.packages[i];
        if (!entry.listed) continue;
        if (disc && entry.summary.valid && !entry.summary.package.filter.matches(*disc)) {
            LaunchPackage unrelated = entry.summary;
            unrelated.for_disc = false;
            const std::string& game = unrelated.package.filter.game;
            unrelated.detail += "; for " + (game.empty() ? std::string("another disc") : game);
            model.packages.push_back(std::move(unrelated));
            continue;
        }
        const std::string uncached_text = pack.text_cached ? std::string() : ReadPackText(pack.path);
        model.add(pack.file, pack.path, pack.text_cached ? pack.text : uncached_text, disc);
    }
    for (const CodeBuildFile& build : catalog.code_builds) {
        model.add_code_build(build.key, build.folder, build.gct, build.game_id, disc);
    }
}

}  // namespace riftwii::wii
