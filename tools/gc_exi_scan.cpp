// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Host diagnostic for GameCube memory-card reports: runs the loader's EXI
// finder (riftwii/gcexi.hpp) on a GameCube disc's main.dol, read-only, and
// prints the control blocks, each hook and what a rejection did place, as
// the launch log would. It reads ISO and RVZ images and writes nothing.
//   gc_exi_scan <game.iso|game.rvz> [...]
// Built by CMake with the host tests (build-host/gc_exi_scan). Its output is
// addresses only; keep the images and anything read from them out of the
// repository.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/gcexi.hpp"
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
    const GcExiFunctions found = find_gc_exi_functions(text, game_id);
    const char* absent = !found.probe_ex_absent ? ""
                         : found.old_sdk       ? ", no EXIProbeEx found (the 0x38-byte layout)"
                                               : ", no EXIProbeEx found (a listed game)";
    std::printf("%s %s: blocks 0x%08x%s, %u table(s) tried, %u resolve%s\n", game_id.c_str(), path, found.blocks,
                found.blocks ? "" : " (REJECTED)", found.tables_tried, found.resolving_tables, absent);
    // No partial table when no table had enough users to be tried.
    if (found.blocks == 0 && found.partial_blocks != 0) {
        std::printf("  partial list from the table at 0x%08x\n", found.partial_blocks);
    }
    if (found.listed_refusal != GcExiFunctions::ListedRefusal::None) {
        static const char* const kReasons[] = {"none", "EXIProbe not alone", "EXIAttach does not follow EXIProbe",
                                               "a branch to the probe outside the EXI functions"};
        std::printf("  listed-game check refused: %s at 0x%08x\n",
                    kReasons[static_cast<unsigned>(found.listed_refusal)], found.listed_refusal_at);
    }
    const GcExiDecision decision = decide_gc_exi_hooks(found, text);
    if (decision.verdict != GcExiVerdict::Hook && found.blocks != 0) {
        std::printf("  the loader's gate would refuse it (verdict %u)\n", static_cast<unsigned>(decision.verdict));
    }
    for (unsigned i = 0; i < kGcExiHooks; ++i) {
        std::printf("  %-11s ", gc_exi_function_name(i));
        if (found.hooks[i] != 0) {
            std::printf("hook 0x%08x\n", found.hooks[i]);
        } else if (found.partial[i] != 0) {
            std::printf("not hooked, placed at 0x%08x\n", found.partial[i]);
        } else if (found.blocks != 0 && found.probe_ex_absent &&
                   std::string(gc_exi_function_name(i)) == "EXIProbeEx") {  // gc_card.h is the runtime's, not ours
            std::printf("absent (accepted: no EXIProbeEx in this %s)\n", found.old_sdk ? "layout" : "SDK");
        } else {
            std::printf("MISSING\n");
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gc_exi_scan <game.iso|game.rvz> [...]\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) status |= scan(argv[i]);
    return status;
}
