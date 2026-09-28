// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii::wii {

// Settings > Credits and licence, as lines for a list: RiftWii's copyright
// and licence notice (GPLv3 section 5(d)'s "Appropriate Legal Notices":
// the notice, the lack of warranty, where the source and the licence
// are), who its parts come from (NOTICE.md has the detail), then the GNU
// GPL version 3 in full, unpacked from the copy built into the program.
// Each line is at most `width` characters.
std::vector<std::string> CreditsLines(std::size_t width);

}  // namespace riftwii::wii
