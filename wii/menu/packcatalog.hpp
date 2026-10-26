// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "codebuilds.hpp"
#include "netpacks.hpp"

namespace riftwii::wii {

// Home has already read these files for its mod badges. Keep that snapshot
// until the next drive scan, including card replacement and network refresh.
// Keep small summaries as well as at most 2 MiB of XML text. Unrelated
// packs need no file read or full parse when a game's page opens.
constexpr std::size_t kMaxGamePackages = 150;

struct CatalogPackage {
    bool listed = false;  // foreign XMLs are omitted by LaunchModel::add
    LaunchPackage summary;  // filter and detail only, without options or patches
};

struct PackageCatalog {
    std::vector<PackFile> files;
    std::vector<CatalogPackage> packages;  // one summary per file, in the same order
    std::vector<CodeBuildFile> code_builds;
    bool limited = false;
    bool ready = false;
};

// Home also builds its badges during this read, so a file too large to
// retain need not be opened again just to learn which game it belongs to.
PackageCatalog ReadPackageCatalog(PackIndex* index = nullptr);

// Matching packs still get their complete options and patches. Others keep
// their summary in the model, just as a full scan would have listed them.
void AddCatalogPackages(const PackageCatalog& catalog, LaunchModel& model, const DiscIdentity* disc);

}  // namespace riftwii::wii
