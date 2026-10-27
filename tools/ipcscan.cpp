// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the structural IPC search on a game's main.dol (as dumped by the
// autorun's `dol` command) and prints what it finds: a check of the
// search on titles before they ever meet the runtime. Also the way back's
// and the HOME Menu's functions (runtime/pad).
//   ipcscan <main.dol> [<main.dol> ...]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/hbmsearch.hpp"
#include "riftwii/nandchannel.hpp"
#include "riftwii/osrestart.hpp"
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
        const std::string file = ss.str();
        std::vector<std::uint8_t> program(file.begin(), file.end());
        riftwii::DolHeader dol;
        std::string error;
        std::cout << argv[i] << ":" << std::endl;
        // A channel's program (a WiiWare or Virtual Console content) as the
        // channel loader reads it: unpacked first when it is compressed.
        if (riftwii::channel_program_compressed(program.data(), program.size()) &&
            !riftwii::unpack_channel_program(program, error)) {
            std::cout << "  cannot unpack the program: " << error << std::endl;
            ++failures;
            continue;
        }
        std::string format = "DOL";
        if (!in || (!riftwii::parse_dol_header(program.data(), program.size(), dol, error) &&
                    !riftwii::parse_channel_program(program.data(), program.size(), dol, format, error))) {
            std::cout << "  cannot read the DOL: " << error << std::endl;
            ++failures;
            continue;
        }
        if (format != "DOL") std::cout << "  read as " << format << std::endl;
        const std::string bytes(program.begin(), program.end());
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
        std::printf("  their submit routine 0x%08x\n", api.submit);
        // The exception handler the crash record hooks (runtime/fault).
        std::vector<riftwii::CodeRange> all;
        for (std::size_t s = 0; s < riftwii::kDolSections; ++s) {
            const riftwii::DolSection& sec = dol.sections[s];
            if (!sec.used() || sec.offset + sec.size > bytes.size()) continue;
            all.push_back({sec.address, reinterpret_cast<const std::uint8_t*>(bytes.data()) + sec.offset, sec.size});
        }
        std::uint32_t unhandled = 0;
        if (riftwii::find_unhandled_exception(text, all, unhandled, error)) {
            std::printf("  __OSUnhandledException 0x%08x\n", unhandled);
        } else {
            std::cout << "  __OSUnhandledException: " << error << std::endl;
            ++failures;
        }
        // The way back that a GameCube-pad combo in a Wii game takes (runtime/pad).
        std::uint32_t return_to_menu = 0;
        if (riftwii::find_os_return_to_menu(text, all, return_to_menu, error)) {
            std::printf("  OSReturnToMenu 0x%08x\n", return_to_menu);
        } else {
            std::cout << "  OSReturnToMenu: " << error << std::endl;
            ++failures;
        }
        std::printf("  OSSleepThread 0x%08x, OSSuspendThread 0x%08x\n", riftwii::find_os_sleep_thread(text),
                    riftwii::find_os_suspend_thread(text));
        // The HOME Menu by a GameCube controller (#224 part 1): a game
        // without any of these keeps the way back above.
        const riftwii::KpadRead kpad = riftwii::find_kpad_read(text);
        const std::uint32_t probe = riftwii::find_wpad_probe(text);
        const std::uint32_t small_data = riftwii::find_small_data_base(all, dol.entry);
        const riftwii::HbmFunctions hbm = riftwii::find_hbm_functions(text, all, small_data);
        std::printf("  KPAD read 0x%08x (status %u bytes%s), WPADProbe 0x%08x, r13 0x%08x, HBMInit 0x%08x, "
                    "HBMCalc 0x%08x%s\n",
                    kpad.address, kpad.status_bytes, kpad.takes_error ? ", takes the error's address" : "", probe,
                    small_data, hbm.init, hbm.calc,
                    kpad.address && probe && hbm.calc ? "" : "  (HOME Menu by a GameCube controller: off)");
    }
    return failures == 0 ? 0 : 1;
}
