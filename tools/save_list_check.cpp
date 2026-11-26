// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "riftwii/savelist.hpp"

int main() {
    const std::string text{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    std::vector<riftwii::SaveListing> saves(riftwii::kSaveListMaxEntries);
    std::size_t count = 0, skipped = 0;
    std::string error;
    if (!riftwii::parse_save_list(text.data(), text.size(), saves.data(), saves.size(), count, skipped, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "the Wii reads " << count << " save(s); " << skipped << " left out\n";
    return skipped == 0 ? 0 : 1;
}
