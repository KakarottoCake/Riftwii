// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The update check: RiftWii asks GitHub for its newest release
// (https://api.github.com/repos/KakarottoCake/Riftwii/releases?per_page=1,
// which lists pre-releases too) and compares its tag with its own version.
namespace riftwii {

// The first "tag_name" in a GitHub API answer.
bool release_tag_from_json(const std::string& json, std::string& tag);

// Orders versions such as "2.0.0-beta", "v1.0.9-beta" and "2.0.1": the
// numbers first, then a final release after any "-suffix" of the same
// numbers, then the suffixes as text ("beta" after "alpha", "rc" after
// "beta"). -1, 0 or 1.
int compare_versions(const std::string& a, const std::string& b);

}  // namespace riftwii
