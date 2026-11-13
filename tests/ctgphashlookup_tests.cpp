// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// One file's digest from a CTGP-R hash list (riftwii/ctgphashlookup.hpp),
// with a small list built here: a header, then a file table whose one file
// entry points at its 20-byte digest.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/ctgphashlookup.hpp"
#include "riftwii/ctgppackwrite.hpp"
#include "riftwii/ctgpupdater.hpp"
#include "riftwii/sha1.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(std::uint8_t(value >> 24));
    out.push_back(std::uint8_t(value >> 16));
    out.push_back(std::uint8_t(value >> 8));
    out.push_back(std::uint8_t(value));
}

void set32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = std::uint8_t(value >> 24);
    out[at + 1] = std::uint8_t(value >> 16);
    out[at + 2] = std::uint8_t(value >> 8);
    out[at + 3] = std::uint8_t(value);
}

std::array<std::uint8_t, 20> sample_digest() {
    std::array<std::uint8_t, 20> d{};
    for (std::size_t i = 0; i < d.size(); ++i) d[i] = std::uint8_t(0xA0 + i);
    return d;
}

// /Race/Course/X.szs with sample_digest().
std::vector<std::uint8_t> make_list(const char* package) {
    std::vector<std::uint8_t> body;
    const char strings[] = "Race\0Course\0X.szs";
    const std::uint32_t entries = 4;
    const std::uint32_t string_table = entries * 12;
    const std::uint32_t digest_at = string_table + sizeof(strings);
    // root, Race (parent 0), Course (parent 1), X.szs
    put32(body, 0x01000000u); put32(body, 0); put32(body, entries);
    put32(body, 0x01000000u | 0); put32(body, 0); put32(body, entries);
    put32(body, 0x01000000u | 5); put32(body, 1); put32(body, entries);
    put32(body, 0x00000000u | 12); put32(body, digest_at); put32(body, 20);
    body.insert(body.end(), strings, strings + sizeof(strings));
    const auto digest = sample_digest();
    body.insert(body.end(), digest.begin(), digest.end());

    std::vector<std::uint8_t> list(0x60, 0);
    list.insert(list.end(), body.begin(), body.end());
    list[0] = 'H'; list[1] = 'a'; list[2] = 'h'; list[3] = '0';
    set32(list, 4, std::uint32_t(list.size()));
    const Sha1Digest body_digest = sha1(body.data(), body.size());
    std::memcpy(list.data() + 0x0C, body_digest.data(), 20);
    std::memcpy(list.data() + 0x20, package, std::strlen(package));
    set32(list, 0x28, 0x01031194u);
    return list;
}

void test_lookup() {
    std::array<std::uint8_t, 20> digest{};
    std::string error;
    auto list = make_list("CTGPR");
    EXPECT_TRUE(ctgp_hash_list_digest(list.data(), list.size(), "CTGPR", "/Race/Course/X.szs", digest, error));
    EXPECT_TRUE(digest == sample_digest());

    // A path the list does not name, another package, a damaged body or header.
    EXPECT_TRUE(!ctgp_hash_list_digest(list.data(), list.size(), "CTGPR", "/Race/Course/Y.szs", digest, error));
    EXPECT_TRUE(!ctgp_hash_list_digest(list.data(), list.size(), "CORE", "/Race/Course/X.szs", digest, error));
    auto damaged = list;
    damaged.back() ^= 1;
    EXPECT_TRUE(!ctgp_hash_list_digest(damaged.data(), damaged.size(), "CTGPR", "/Race/Course/X.szs", digest, error));
    damaged = list;
    damaged[0] = 'X';
    EXPECT_TRUE(!ctgp_hash_list_digest(damaged.data(), damaged.size(), "CTGPR", "/Race/Course/X.szs", digest, error));
    EXPECT_TRUE(!ctgp_hash_list_digest(list.data(), list.size() - 1, "CTGPR", "/Race/Course/X.szs", digest, error));
    EXPECT_TRUE(!ctgp_hash_list_digest(nullptr, 0, "CTGPR", "/Race/Course/X.szs", digest, error));

    // Only a good list without the path says "unlisted".
    bool unlisted = false;
    EXPECT_TRUE(!ctgp_hash_list_digest(list.data(), list.size(), "CTGPR", "/changelog.bin", digest, error, &unlisted));
    EXPECT_TRUE(unlisted);
    EXPECT_TRUE(ctgp_hash_list_digest(list.data(), list.size(), "CTGPR", "/Race/Course/X.szs", digest, error,
                                      &unlisted));
    EXPECT_TRUE(!unlisted);
    damaged = list;
    damaged.back() ^= 1;
    EXPECT_TRUE(!ctgp_hash_list_digest(damaged.data(), damaged.size(), "CTGPR", "/changelog.bin", digest, error,
                                       &unlisted));
    EXPECT_TRUE(!unlisted);
}

// The pack target: a packed file needs its entry and must match it; an
// unpacked file the list doesn't name (CTGP-R lists only the courses) is
// taken; an unpacked file it does name must match; a damaged list checks
// nothing.
void test_pack_target_check() {
    riftwii::CtgpPackWriter pack;
    const auto list = make_list("CTGPR");
    riftwii::CtgpPackUpdateTarget target(pack, riftwii::CtgpDiscBase(), list, "ctgphashlookup_unused_");
    std::string error;
    std::array<std::uint8_t, 20> other{};
    EXPECT_TRUE(target.check_file("/Race/Course/X.szs", true, sample_digest(), error));
    EXPECT_TRUE(!target.check_file("/Race/Course/X.szs", true, other, error));
    EXPECT_TRUE(!target.check_file("/Race/Course/Y.szs", true, other, error));
    EXPECT_TRUE(target.check_file("/changelog.bin", false, other, error));
    EXPECT_TRUE(target.check_file("/Race/Course/X.szs", false, sample_digest(), error));
    EXPECT_TRUE(!target.check_file("/Race/Course/X.szs", false, other, error));
    // An unlisted unpacked file rebuilt from the game disc is refused (the
    // disc could be another revision); the next file starts clean.
    {
        std::unique_ptr<riftwii::CtgpBaseSource> base;
        riftwii::CtgpUpdateFailure why = riftwii::CtgpUpdateFailure::Temporary;
        EXPECT_TRUE(!target.load_base("RMC", "/x.bmg", false, base, riftwii::CtgpProgress(), why, error));
        EXPECT_TRUE(!target.check_file("/changelog.bin", false, other, error));
        EXPECT_TRUE(error.find("game disc") != std::string::npos);
        EXPECT_TRUE(target.check_file("/changelog.bin", false, other, error));
    }
    // Base sizes for the bar, from the disc: a Yaz0 file by its header, a
    // plain one by its length; nothing for a missing file, no disc or a
    // package the target can't read.
    {
        const auto disc = [](const std::string& path, const riftwii::CtgpDiscSink& sink, std::uint32_t* size,
                             std::string& why) {
            std::vector<std::uint8_t> file;
            if (path == "/y.szs") {
                file = {'Y', 'a', 'z', '0', 0, 0x01, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 1, 2};
            } else if (path == "/huge.szs") {
                file = {'Y', 'a', 'z', '0', 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF};
            } else if (path == "/p.bin") {
                file.assign(40, 7);
            } else if (path == "/s.bin") {
                file = {1, 2, 3};
            } else {
                why = "no such file";
                return false;
            }
            if (size) *size = std::uint32_t(file.size());
            // Two pieces, as a disc read may hand them over.
            if (!sink(file.data(), 5)) return false;
            return sink(file.data() + 5, file.size() - 5);
        };
        riftwii::CtgpPackUpdateTarget with_disc(pack, disc, list, "ctgphashlookup_unused_");
        std::uint32_t size = 1;
        EXPECT_TRUE(with_disc.base_unpacked_size("RMC", "/y.szs", true, size) && size == 0x10000);
        EXPECT_TRUE(with_disc.base_unpacked_size("RMC", "/p.bin", true, size) && size == 40);
        EXPECT_TRUE(with_disc.base_unpacked_size("RMC", "/s.bin", false, size) && size == 3);
        EXPECT_TRUE(!with_disc.base_unpacked_size("RMC", "/huge.szs", true, size));
        EXPECT_TRUE(!with_disc.base_unpacked_size("RMC", "/none.szs", true, size));
        EXPECT_TRUE(!with_disc.base_unpacked_size("MKWII", "/y.szs", true, size));
        // The pack isn't open here: its files can't be sized.
        EXPECT_TRUE(!with_disc.base_unpacked_size("CTGPR", "/y.szs", true, size));
        EXPECT_TRUE(!target.base_unpacked_size("RMC", "/y.szs", true, size));  // no disc
        // Sizing leaves nothing behind: an unlisted unpacked file is still
        // taken (no disc base was counted).
        EXPECT_TRUE(with_disc.check_file("/changelog.bin", false, other, error));
    }
    auto damaged = list;
    damaged.back() ^= 1;
    riftwii::CtgpPackUpdateTarget broken(pack, riftwii::CtgpDiscBase(), damaged, "ctgphashlookup_unused_");
    EXPECT_TRUE(!broken.check_file("/changelog.bin", false, other, error));
}

}  // namespace

int main() {
    test_lookup();
    test_pack_target_check();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "ctgphashlookup: all tests passed" << std::endl;
    return 0;
}
