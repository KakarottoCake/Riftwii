// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "codebuilds.hpp"
#include "netpacks.hpp"

namespace riftwii::wii {

// Home has already read these files for its mod badges. Keep that snapshot
// until the next drive scan, including card replacement and network refresh.
// Only the XML text is retained (at most 2 MiB), not another parsed model.
struct PackageCatalog {
    std::vector<PackFile> files;
    std::vector<CodeBuildFile> code_builds;
    bool limited = false;
    bool ready = false;
};

// Home also builds its badges during this read, so a file too large to
// retain need not be opened again just to learn which game it belongs to.
PackageCatalog ReadPackageCatalog(PackIndex* index = nullptr);

}  // namespace riftwii::wii
