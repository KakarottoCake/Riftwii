// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads an apps.json from standard input with the Wii's own parser
// (src/net/applist.cpp) and says how many apps it keeps. Exit 1 when the
// list is unreadable or the parser leaves any app out: tools/app_list.py
// runs it on every list it builds, so no app is published that the Wii
// would drop.
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "riftwii/applist.hpp"

int main() {
    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::vector<riftwii::AppListing> apps(riftwii::kAppListMaxApps);
    std::size_t count = 0, skipped = 0;
    std::string error;
    if (!riftwii::parse_app_list(text, apps.data(), apps.size(), count, skipped, error)) {
        std::cerr << error << std::endl;
        return 1;
    }
    std::cout << "the Wii reads " << count << " app(s)" << std::endl;
    if (skipped != 0) {
        std::cerr << skipped << " app(s) left out by the Wii's rules" << std::endl;
        return 1;
    }
    return 0;
}
