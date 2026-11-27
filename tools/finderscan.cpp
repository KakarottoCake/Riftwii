// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Every finder the controller hooks use (src/patching/hbmsearch.cpp,
// symsearch.cpp, osrestart.cpp), run over dumped game programs, one line
// per program. Saved once as a baseline, it shows whether a change to a
// finder moved any real game's match: run it again and compare (`diff`)
// before the change goes for review.
//   finderscan <main.dol>... | finderscan @<file listing one path a line>
#include <cstdint>
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

namespace {

bool inside_file(const riftwii::DolSection& sec, std::size_t file_size) {
    return std::uint64_t{sec.offset} + sec.size <= file_size;
}

// One program's line: its path, then each finder's answer (0: not found).
bool scan(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string file = ss.str();
    std::vector<std::uint8_t> program(file.begin(), file.end());
    riftwii::DolHeader dol;
    std::string error, format = "DOL";
    if (riftwii::channel_program_compressed(program.data(), program.size()) &&
        !riftwii::unpack_channel_program(program, error)) {
        std::printf("%s | cannot unpack: %s\n", path.c_str(), error.c_str());
        return false;
    }
    if (!in || (!riftwii::parse_dol_header(program.data(), program.size(), dol, error) &&
                !riftwii::parse_channel_program(program.data(), program.size(), dol, format, error))) {
        std::printf("%s | unreadable: %s\n", path.c_str(), error.c_str());
        return false;
    }
    std::vector<riftwii::CodeRange> text, all;
    for (std::size_t s = 0; s < riftwii::kDolSections; ++s) {
        const riftwii::DolSection& sec = dol.sections[s];
        if (!sec.used()) continue;
        if (!inside_file(sec, program.size())) {
            // A section the file cuts short: the finders would miss code, so
            // the program counts as not scanned (CodeRabbit CLI).
            std::printf("%s | section %u runs past the end of the file\n", path.c_str(), static_cast<unsigned>(s));
            return false;
        }
        const riftwii::CodeRange range{sec.address, program.data() + sec.offset, sec.size};
        all.push_back(range);
        if (s < riftwii::kDolTextSections) text.push_back(range);
    }
    const riftwii::KpadRead kpad = riftwii::find_kpad_read(text);
    const std::uint32_t probe = riftwii::find_wpad_probe(text);
    const riftwii::KpadUnified unified = riftwii::find_kpad_unified(text);
    const riftwii::KpadConnect connect = riftwii::find_kpad_connect(text, kpad.address);
    const riftwii::WpadConnect2006 c2006 = riftwii::find_wpad_connect_2006(text);
    const riftwii::WpadConnect2006 later = riftwii::find_wpad_connect_later(text);
    const riftwii::WpadConnect2006 slot = c2006.table != 0 ? c2006 : later;
    const riftwii::WpadConnect2006 ext = riftwii::find_wpad_extension(text, slot);
    const riftwii::KpadDpd dpd = riftwii::find_kpad_enable_dpd(text);
    const std::uint32_t small_data = riftwii::find_small_data_base(all, dol.entry);
    const riftwii::HbmFunctions hbm = riftwii::find_hbm_functions(text, all, small_data);
    riftwii::PadSymbols pad;
    std::string pad_error;
    const bool pad_found = riftwii::find_pad_symbols(text, pad, pad_error);
    std::printf("%s | kpad %08x/%u/%d | probe %08x | unified %08x/%u | connect %08x %08x/%u+%u | info %08x | "
                "infolater %08x | c2006 %08x+%u | later %08x+%u | ext %08x+%u | read %08x | fmt %08x | "
                "dpd %08x %08x/%u+%u | idle %08x | sleep %08x | r13 %08x | hbm %08x %08x %08x | pad %s %08x/%u %08x %08x | infoblock %08x\n",
                path.c_str(), kpad.address, kpad.status_bytes, kpad.takes_error ? 1 : 0, probe, unified.address,
                unified.status_bytes, connect.address, connect.block, connect.stride, connect.offset,
                riftwii::find_wpad_get_info(text), riftwii::find_wpad_get_info_later(text, later.table), c2006.table,
                c2006.offset, later.table, later.offset, ext.table, ext.offset, riftwii::find_wpad_read(text),
                riftwii::find_wpad_set_format(text), dpd.enable, dpd.base, dpd.stride, dpd.offset,
                riftwii::find_wii_remote_idle_reset(text, probe), riftwii::find_os_sleep_thread(text), small_data,
                hbm.init, hbm.calc, hbm.select, pad_found ? "ok" : "none", pad.read, pad.read_sites,
                pad.control_motor, pad.idle_reset, riftwii::find_wpad_get_info_blocking(text, later.table));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: finderscan <main.dol>... | finderscan @<list>" << std::endl;
        return 2;
    }
    int failures = 0, scanned = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 1 && arg[0] == '@') {
            // A list that can't be read is an error, not an empty scan: a
            // baseline compared with nothing would look like a match.
            std::ifstream list(arg.substr(1));
            if (!list) {
                std::cerr << "finderscan: cannot read the list " << arg.substr(1) << std::endl;
                return 1;
            }
            std::string line;
            while (std::getline(list, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                ++scanned;
                if (!scan(line)) ++failures;
            }
            if (list.bad()) {
                std::cerr << "finderscan: reading the list " << arg.substr(1) << " failed" << std::endl;
                return 1;
            }
        } else {
            ++scanned;
            if (!scan(arg)) ++failures;
        }
    }
    // Every program read, or the run failed: a baseline compared against a
    // run that skipped some would look like a match (CodeRabbit CLI).
    // Nothing scanned (an empty or comment-only list) is a failure: two
    // empty runs must not compare equal (CodeRabbit, fork #24).
    if (scanned == 0) {
        std::cerr << "finderscan: no programs scanned" << std::endl;
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
