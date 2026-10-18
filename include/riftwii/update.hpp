// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "riftwii/themepack.hpp"

// The update check: RiftWii asks GitHub for its newest release and
// compares its tag with its own version. It asks for the latest release
// (/releases/latest, which leaves pre-releases out).
namespace riftwii {

// The first "tag_name" in a GitHub API answer.
bool release_tag_from_json(const std::string& json, std::string& tag);

// A file attached to a release: where to download it, its SHA-256 (lowercase
// hex, empty when GitHub gave none) and its size in bytes (0: unknown).
struct ReleaseAsset {
    std::string url;
    std::string sha256;
    unsigned long long size = 0;
};
// The first asset named `name` in a GitHub API answer (its
// "browser_download_url", "digest": "sha256:..." and "size").
bool release_asset_from_json(const std::string& json, const std::string& name, ReleaseAsset& out);

// Orders versions such as "2.0.0-beta", "v1.0.9-beta" and "2.0.1": the
// numbers first, then a final release after any "-suffix" of the same
// numbers, then the suffixes as text ("beta" after "alpha", "rc" after
// "beta"). -1, 0 or 1.
int compare_versions(const std::string& a, const std::string& b);

// One file the in-app update writes, in the order it writes them.
struct UpdateWrite {
    std::string path;         // where on the card
    std::size_t offset = 0;   // a plugin's: its bytes in the pack's apps part
    std::size_t size = 0;
    bool dol = false;         // the new boot.dol, which comes as its own download
};

// The in-app update's writes, in order (docs/PLUGINS.md, "Updates: plugins
// first"): every plugin of the release's apps pack (`apps`, the apps part
// of riftwii-update.pack as parse_theme_pack read it; its plugins are
// riftwii/plugins/<name>.<version>.rwp) into `plugin_dir`, then the DOL at
// `dol_path`, last. The new version's stamped names sit beside the old
// version's files, so a cut between two writes leaves the old DOL with its
// own plugins and never a new DOL without its plugins. Every plugin must
// be of `release` (the release's tag; a leading 'v' is not part of the
// version): one of another version would be written for a DOL that is not
// coming. The rest of the apps part is left to the new DOL's first start.
// False, with `error`, and nothing to write, otherwise.
bool plan_update_writes(const ThemePack& apps, const std::string& release, const std::string& plugin_dir,
                        const std::string& dol_path, std::vector<UpdateWrite>& out, std::string& error);

}  // namespace riftwii
