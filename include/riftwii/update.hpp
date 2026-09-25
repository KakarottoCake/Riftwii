// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The update check: RiftWii asks GitHub for its newest release and
// compares its tag with its own version. The Stable channel asks for the
// latest release (/releases/latest, which leaves pre-releases out), the
// Beta channel for the newest of all (/releases?per_page=1).
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

// The update channel a setting means for this build: "stable" or "beta"
// as set, and "auto" follows the version: one with a "-suffix" (beta,
// rc1) is a pre-release build and follows Beta.
std::string effective_update_channel(const std::string& setting, const std::string& version);

}  // namespace riftwii
