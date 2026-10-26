// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "packcatalog.hpp"

namespace riftwii::wii {

PackageCatalog ReadPackageCatalog(PackIndex* index) {
    PackageCatalog catalog;
    if (index) *index = PackIndex{};
    // Home's index admits 256 files; the game's option browser uses the
    // first 150. Preserve that difference when handing the snapshot over.
    catalog.files = ListPackFiles(256, catalog.limited, true);
    std::size_t cached_bytes = 0;
    for (const PackFile& pack : catalog.files) cached_bytes += pack.text.size();
    for (PackFile& pack : catalog.files) {
        if (pack.text_cached) {
            if (index && !pack.text.empty()) index->add(pack.text);
            continue;
        }
        std::string text = ReadPackText(pack.path);
        if (index && !text.empty()) index->add(text);
        if (text.size() <= kPackTextCacheBytes - cached_bytes) {
            cached_bytes += text.size();
            pack.text.swap(text);
            pack.text_cached = true;
        }
    }
    catalog.code_builds = ListCodeBuilds();
    if (index) {
        for (const CodeBuildFile& build : catalog.code_builds) index->add_game(build.game_id);
    }
    catalog.ready = true;
    return catalog;
}

}  // namespace riftwii::wii
