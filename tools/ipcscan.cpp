// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the structural IPC search on a game's main.dol (as dumped by the
// autorun's `dol` command) and prints what it finds: a check of the
// search on titles before they ever meet the runtime.
//   ipcscan <main.dol> [<main.dol> ...]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/symsearch.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ipcscan <main.dol>..." << std::endl;
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string bytes = ss.str();
        riftwii::DolHeader dol;
        std::string error;
        std::cout << argv[i] << ":" << std::endl;
        if (!in || !riftwii::parse_dol_header(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), dol, error)) {
            std::cout << "  cannot read the DOL: " << error << std::endl;
            ++failures;
            continue;
        }
        std::vector<riftwii::CodeRange> text;
        for (std::size_t s = 0; s < riftwii::kDolTextSections; ++s) {
            const riftwii::DolSection& sec = dol.sections[s];
            if (!sec.used() || sec.offset + sec.size > bytes.size()) continue;
            text.push_back({sec.address, reinterpret_cast<const std::uint8_t*>(bytes.data()) + sec.offset, sec.size});
        }
        riftwii::IpcSymbols symbols;
        if (!riftwii::find_ipc_symbols(text, symbols, error)) {
            std::cout << "  " << error << std::endl;
            ++failures;
            continue;
        }
        std::printf("  IOS_IoctlAsync 0x%08x (%u DI commands agree), IOS_IoctlvAsync 0x%08x\n", symbols.ioctl_async,
                    symbols.ioctl_async_commands, symbols.ioctlv_async);
        riftwii::IpcApi api;
        if (!riftwii::find_ipc_api(text, symbols, api, error)) {
            std::cout << "  " << error << std::endl;
            ++failures;
            continue;
        }
        static const char* const names[8] = {"", "Open", "Close", "Read", "Write", "Seek", "Ioctl", "Ioctlv"};
        for (int c = 1; c <= 7; ++c) {
            std::printf("  IOS_%-6s async 0x%08x  sync 0x%08x%s\n", names[c], api.async[c], api.sync[c],
                        api.async[c] == 0 || api.sync[c] == 0 ? "  (a form the game never calls is not in its DOL)" : "");
        }
    }
    return failures == 0 ? 0 : 1;
}
