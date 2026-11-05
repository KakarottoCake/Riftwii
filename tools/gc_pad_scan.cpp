// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Host diagnostic for GameCube controller reports: runs the loader's pad
// finders on a disc's main.dol, read-only, and prints their addresses. It
// reads ISO and RVZ images and writes nothing.
//   gc_pad_scan <game.iso|game.rvz> [...]
// Built by CMake with the host tests (build-host/gc_pad_scan). Its output is
// addresses only; keep the images and anything read from them out of the
// repository.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii/rvz.hpp"

using namespace riftwii;

namespace {

class StreamSource final : public ByteSource {
public:
    explicit StreamSource(const char* path) : in_(path, std::ios::binary) {
        in_.seekg(0, std::ios::end);
        size_ = in_ ? static_cast<std::uint64_t>(in_.tellg()) : 0;
    }
    bool ok() const { return static_cast<bool>(in_); }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset));
        in_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(length));
        return static_cast<std::size_t>(in_.gcount()) == length;
    }

private:
    mutable std::ifstream in_;
    std::uint64_t size_ = 0;
};

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// These two searches mirror plugins/gamecube/gcnative.cpp, where the
// GameCube loader keeps them. The Wii adapter finder is reported separately.
class Code {
public:
    explicit Code(const std::vector<CodeRange>& ranges) : ranges_(ranges) {}
    bool has(std::uint32_t address) const {
        for (const CodeRange& range : ranges_) {
            if (address >= range.address && std::uint64_t(address - range.address) + 4 <= range.size) return true;
        }
        return false;
    }
    std::uint32_t at(std::uint32_t address) const {
        for (const CodeRange& range : ranges_) {
            if (address >= range.address && std::uint64_t(address - range.address) + 4 <= range.size)
                return be32(range.bytes + (address - range.address));
        }
        return 0;
    }
    template <class F>
    void each(F function) const {
        for (const CodeRange& range : ranges_) {
            for (std::size_t offset = 0; offset + 4 <= range.size; offset += 4)
                function(range.address + static_cast<std::uint32_t>(offset), be32(range.bytes + offset));
        }
    }
private:
    const std::vector<CodeRange>& ranges_;
};

std::uint32_t function_start(const Code& code, std::uint32_t at) {
    for (std::uint32_t address = at - 4; code.has(address) && at - address < 0x400; address -= 4) {
        if (code.at(address) == 0x4E800020) return address + 4;
    }
    return 0;
}

// PADRead(PADStatus status[4]): for each port it cannot read, it stores
// the error into the status's `err` byte (offset 10) and clears the 10
// bytes before it (`stb rE,10(rS)`, then `li r5,10` and a call): the
// function with the most such places, at least three.
std::uint32_t find_pad_read(const Code& code) {
    std::map<std::uint32_t, unsigned> sites;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w >> 26) != 38 || (w & 0xFFFF) != 10) return;  // stb rX,10(rY)
        for (std::uint32_t b = a + 4; b <= a + 16; b += 4)
            if (code.at(b) == 0x38A0000A) {  // li r5,10
                if (std::uint32_t f = function_start(code, a)) sites[f]++;
                break;
            }
    });
    std::uint32_t best = 0;
    unsigned most = 2;
    for (auto& [f, n] : sites)
        if (n > most) {
            most = n;
            best = f;
        }
    return best;
}

// The start of the function holding `at` in GCC's code (gc_rt.h,
// "libogc"), which puts a function's epilogue and its blr in the middle:
// the nearest "stwu r1,-N(r1)" before `at` that follows a blr or a b. 0
// for none within 4 KB (a leaf function without a frame).
std::uint32_t gcc_function_start(const Code& code, std::uint32_t at) {
    for (std::uint32_t a = at; code.has(a) && at - a < 0x1000; a -= 4) {
        if ((code.at(a) & 0xFFFF8000) != 0x94218000) continue;  // stwu r1,-N(r1)
        const std::uint32_t before = code.at(a - 4);
        if (before == 0x4E800020 || (before & 0xFC000003) == 0x48000000) return a;
    }
    return 0;
}

// PADRead as GCC builds it (Sonic Riders Tournament Edition): the
// PADStatus of a port it cannot read is cleared by three word stores (0,
// 4, 8) and its err byte (10) written, where the SDK's calls memset with
// 10; the function with the most such places, at least three.
std::uint32_t find_pad_read_gcc(const Code& code) {
    std::map<std::uint32_t, unsigned> sites;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w >> 26) != 38 || (w & 0xFFFF) != 10) return;  // stb rE,10(rS)
        const unsigned status = (w >> 16) & 31;
        unsigned cleared = 0;
        for (std::uint32_t b = a - 12; b < a; b += 4) {
            const std::uint32_t x = code.at(b);
            const std::uint32_t disp = x & 0xFFFF;
            if ((x >> 26) == 36 && ((x >> 16) & 31) == status && (disp == 0 || disp == 4 || disp == 8)) ++cleared;
        }
        if (cleared == 3)
            if (std::uint32_t f = gcc_function_start(code, a)) sites[f]++;
    });
    std::uint32_t best = 0;
    unsigned most = 2;
    for (auto& [f, n] : sites)
        if (n > most) {
            most = n;
            best = f;
        }
    return best;
}

int scan(const char* path) {
    auto file = std::make_shared<StreamSource>(path);
    if (!file->ok()) {
        std::printf("%s: cannot open\n", path);
        return 1;
    }
    std::uint8_t magic[4] = {};
    file->read(0, magic, sizeof magic);
    std::unique_ptr<RvzDiscSource> rvz;
    const ByteSource* disc = file.get();
    std::string error;
    if (magic[0] == 'R' && magic[1] == 'V' && magic[2] == 'Z') {
        if (!RvzDiscSource::open(file, rvz, error)) {
            std::printf("%s: cannot open the RVZ: %s\n", path, error.c_str());
            return 1;
        }
        disc = rvz.get();
    }
    std::uint8_t header[0x440] = {};
    if (!disc->read(0, header, sizeof header)) {
        std::printf("%s: cannot read the disc header\n", path);
        return 1;
    }
    const std::string game_id(reinterpret_cast<const char*>(header), 6);
    // The header is the image's, not ours: every size it gives is checked
    // against the disc before anything is allocated or read.
    const std::uint64_t disc_bytes = disc->size();
    const std::uint32_t dol_at = be32(header + 0x420);
    std::vector<std::uint8_t> head(0x100);
    if (std::uint64_t(dol_at) + head.size() > disc_bytes) {
        std::printf("%s: the DOL offset 0x%08x is past the disc's end\n", path, dol_at);
        return 1;
    }
    DolHeader dol;
    if (!disc->read(dol_at, head.data(), head.size()) || !parse_dol_header(head.data(), head.size(), dol, error)) {
        std::printf("%s: cannot parse the DOL header: %s\n", path, error.c_str());
        return 1;
    }
    // The DOL's size: the end of its furthest section, in 64 bits so that an
    // offset and a size near 4 GB cannot wrap, and at most kLargestDol.
    constexpr std::uint64_t kLargestDol = 64u << 20;
    std::uint64_t total = 0;
    for (std::size_t s = 0; s < kDolSections; ++s) {
        if (dol.sections[s].used()) {
            total = std::max<std::uint64_t>(total, std::uint64_t(dol.sections[s].offset) + dol.sections[s].size);
        }
    }
    if (total > kLargestDol || dol_at + total > disc_bytes) {
        std::printf("%s: the DOL's sections end 0x%llx bytes in, past the disc or the 64 MB limit\n", path,
                    static_cast<unsigned long long>(total));
        return 1;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(total));
    if (!disc->read(dol_at, bytes.data(), bytes.size())) {
        std::printf("%s: cannot read the DOL\n", path);
        return 1;
    }
    std::vector<CodeRange> text;
    for (std::size_t s = 0; s < kDolTextSections; ++s) {
        const DolSection& section = dol.sections[s];
        if (!section.used() || std::uint64_t(section.offset) + section.size > bytes.size()) continue;
        text.push_back({section.address, bytes.data() + section.offset, section.size});
    }
    PadSymbols pad;
    if (!find_pad_symbols(text, pad, error)) {
        std::printf("%s %s: PAD search ambiguous: %s\n", game_id.c_str(), path, error.c_str());
        return 1;
    }
    const Code code(text);
    std::uint32_t pad_read = find_pad_read(code);
    if (pad_read == 0) pad_read = find_pad_read_gcc(code);
    const std::uint32_t probe = find_gc_si_probe(text, pad_read);
    const std::uint32_t sampling = find_gc_pad_sampling(text);
    const std::uint32_t cache = find_gc_si_type_cache(text, pad_read);
    const std::int32_t enabled = find_gc_pad_enabled_offset(text, pad_read);
    std::printf("%s PADRead=0x%08x WiiPADRead=0x%08x SIProbe=0x%08x SamplingHandler=0x%08x "
                "SITypeCache=0x%08x EnabledOffset=%d %s\n",
                game_id.c_str(), pad_read, pad.read, probe, sampling, cache, enabled, path);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gc_pad_scan <game.iso|game.rvz> [...]\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) status |= scan(argv[i]);
    return status;
}
