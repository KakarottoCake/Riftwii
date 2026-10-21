// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// read_package from a stream: for the host tests only. The Wii build leaves
// this file out (Makefile.wii's EXCLUDES), because a reference to
// std::istream brings libstdc++'s locale and stream code into the DOL; the
// Wii reads a pack's XML into a string and calls parse_package.
#include "riftwii/patch.hpp"

#include <istream>
#include <new>

namespace riftwii {

bool read_package(std::istream& input, Package& output, std::string& error, const std::string& folder) {
    try {
        std::string xml;
        char chunk[4096];
        while (input.read(chunk, sizeof(chunk)) || input.gcount() > 0) {
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > kMaxPackageXml - xml.size()) {
                error = "xml too large";
                return false;
            }
            xml.append(chunk, count);
        }
        if (input.bad() || !input.eof()) {
            error = "read error";
            return false;
        }
        return parse_package(xml, output, error, folder);
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    } catch (...) {
        error = "read error";
        return false;
    }
}

}  // namespace riftwii
