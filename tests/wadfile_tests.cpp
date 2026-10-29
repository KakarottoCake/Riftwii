// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// .wad files: the layout, each content decrypted in pieces and checked
// against the TMD's hash, and /shared1/content.map. The keys are made up
// here: RiftWii carries no Nintendo key, and these tests need none.
#include "riftwii/wadfile.hpp"

#include <cstring>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = std::uint8_t(v >> 8);
    p[1] = std::uint8_t(v);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    put16(p, std::uint16_t(v >> 16));
    put16(p + 2, std::uint16_t(v));
}
void put64(std::uint8_t* p, std::uint64_t v) {
    put32(p, std::uint32_t(v >> 32));
    put32(p + 4, std::uint32_t(v));
}
void pad_to_64(std::vector<std::uint8_t>& v) { v.resize((v.size() + 63) & ~std::size_t(63), 0); }

const std::uint8_t kCommonKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const std::uint8_t kTitleKey[16] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                                    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
constexpr std::uint64_t kTitle = 0x0001000157424D45ull;  // WBME

struct Built {
    std::vector<std::uint8_t> file;
    std::vector<std::vector<std::uint8_t>> plain;  // each content's own bytes
};

// A WAD of two contents (a 0x30-byte banner and a 0x123-byte program).
// `first_index` other than 0 gives the small content another index, as in
// a channel WAD with no banner.
Built build_wad(std::uint16_t first_index = 0) {
    Built b;
    b.plain.push_back(std::vector<std::uint8_t>(0x30, 0x11));
    std::vector<std::uint8_t> program(0x123);
    for (std::size_t i = 0; i < program.size(); ++i) program[i] = static_cast<std::uint8_t>(i * 7);
    b.plain.push_back(program);

    std::vector<std::uint8_t> certs(0x100, 0xCE);
    std::vector<std::uint8_t> ticket(kTicketBytes, 0);
    put32(ticket.data(), 0x10001);
    put64(ticket.data() + 0x1DC, kTitle);
    std::uint8_t iv[16] = {};
    std::memcpy(iv, ticket.data() + 0x1DC, 8);
    Aes128(kCommonKey).cbc_encrypt(iv, kTitleKey, ticket.data() + 0x1BF, 16);

    std::vector<std::uint8_t> tmd(0x1E4 + 2 * 36, 0);
    put32(tmd.data(), 0x10001);
    put64(tmd.data() + 0x18C, kTitle);
    put16(tmd.data() + 0x1DE, 2);
    put16(tmd.data() + 0x1E0, 1);
    for (std::size_t i = 0; i < 2; ++i) {
        std::uint8_t* r = tmd.data() + 0x1E4 + i * 36;
        put32(r, static_cast<std::uint32_t>(i));
        put16(r + 4, i == 0 ? first_index : static_cast<std::uint16_t>(i));
        put16(r + 6, 1);
        put64(r + 8, b.plain[i].size());
        const Sha1Digest h = sha1(b.plain[i].data(), b.plain[i].size());
        std::memcpy(r + 16, h.data(), 20);
    }

    std::vector<std::uint8_t> data;
    for (std::size_t i = 0; i < 2; ++i) {
        std::vector<std::uint8_t> c = b.plain[i];
        c.resize((c.size() + 15) & ~std::size_t(15), 0);
        std::uint8_t civ[16] = {};
        put16(civ, i == 0 ? first_index : static_cast<std::uint16_t>(i));
        Aes128(kTitleKey).cbc_encrypt(civ, c.data(), c.data(), c.size());
        data.insert(data.end(), c.begin(), c.end());
        pad_to_64(data);
    }

    std::vector<std::uint8_t> header(0x20, 0);
    put32(header.data(), 0x20);
    header[4] = 'I';
    header[5] = 's';
    put32(header.data() + 0x08, static_cast<std::uint32_t>(certs.size()));
    put32(header.data() + 0x10, static_cast<std::uint32_t>(ticket.size()));
    put32(header.data() + 0x14, static_cast<std::uint32_t>(tmd.size()));
    put32(header.data() + 0x18, static_cast<std::uint32_t>(data.size()));
    for (const auto* part : {&header, &certs, &ticket, &tmd, &data}) {
        b.file.insert(b.file.end(), part->begin(), part->end());
        pad_to_64(b.file);
    }
    return b;
}

void test_layout_and_contents() {
    const Built b = build_wad();
    WadLayout layout;
    std::string error;
    EXPECT_TRUE(parse_wad_header(b.file.data(), b.file.size(), b.file.size(), layout, error));
    EXPECT_EQ(layout.certs_offset, 0x40u);
    EXPECT_EQ(layout.ticket_offset, 0x140u);
    EXPECT_EQ(layout.tmd_offset, 0x400u);

    std::uint8_t title_key[16];
    EXPECT_TRUE(decrypt_title_key(b.file.data() + layout.ticket_offset, layout.ticket_size, kCommonKey, title_key, error));
    EXPECT_TRUE(std::memcmp(title_key, kTitleKey, 16) == 0);

    TmdContents tmd;
    EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, tmd, error));
    std::vector<WadContentPlace> places;
    EXPECT_TRUE(wad_content_places(layout, tmd, places, error));
    EXPECT_EQ(places.size(), std::size_t(2));
    for (std::size_t i = 0; i < places.size() && i < 2; ++i) {
        std::vector<std::uint8_t> bytes(b.file.begin() + static_cast<long>(places[i].offset),
                                        b.file.begin() + static_cast<long>(places[i].offset + places[i].stored));
        WadContentDecrypter decrypter(title_key, places[i].content);
        // In pieces of 32 bytes, as a card is read in chunks.
        std::size_t own = 0;
        for (std::size_t at = 0; at < bytes.size(); at += 32) {
            const std::size_t piece = bytes.size() - at < 32 ? bytes.size() - at : 32;
            own += decrypter.decrypt(bytes.data() + at, piece);
        }
        EXPECT_EQ(own, b.plain[i].size());
        EXPECT_TRUE(std::memcmp(bytes.data(), b.plain[i].data(), b.plain[i].size()) == 0);
        EXPECT_TRUE(decrypter.matches());
    }

    // A wrong key: the hash does not match.
    std::uint8_t wrong[16] = {};
    std::vector<std::uint8_t> bytes(b.file.begin() + static_cast<long>(places[1].offset),
                                    b.file.begin() + static_cast<long>(places[1].offset + places[1].stored));
    WadContentDecrypter bad(wrong, places[1].content);
    bad.decrypt(bytes.data(), bytes.size());
    EXPECT_TRUE(!bad.matches());
}

void test_bad_files() {
    Built b = build_wad();
    WadLayout layout;
    std::string error;
    // Cut short.
    EXPECT_TRUE(!parse_wad_header(b.file.data(), b.file.size(), b.file.size() - 64, layout, error));
    // Not a channel's WAD (boot2's type).
    std::vector<std::uint8_t> boot2 = b.file;
    boot2[4] = 'i';
    boot2[5] = 'b';
    EXPECT_TRUE(!parse_wad_header(boot2.data(), boot2.size(), boot2.size(), layout, error));
    // Not a WAD at all.
    const std::uint8_t text[32] = "this is not a wad file at all..";
    EXPECT_TRUE(!parse_wad_header(text, sizeof text, sizeof text, layout, error));
    EXPECT_TRUE(!parse_wad_header(nullptr, 0, 0, layout, error));

    // A TMD whose content is bigger than the data.
    EXPECT_TRUE(parse_wad_header(b.file.data(), b.file.size(), b.file.size(), layout, error));
    TmdContents tmd;
    EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, tmd, error));
    tmd.contents[1].size = 0x100000;
    std::vector<WadContentPlace> places;
    EXPECT_TRUE(!wad_content_places(layout, tmd, places, error));
    // A size so large that rounding it up to 16 leaves it as it is and
    // adding it to the offset wraps (BUG-12): refused all the same.
    tmd.contents[1].size = 0xFFFFFFFFFFFFFFF0ull;
    EXPECT_TRUE(!wad_content_places(layout, tmd, places, error));
    tmd.contents[1].size = 0xFFFFFFFFFFFFFFFFull;
    EXPECT_TRUE(!wad_content_places(layout, tmd, places, error));
}

void test_content_map() {
    std::vector<SharedContent> entries;
    EXPECT_EQ(next_shared_name(entries), std::string("00000000"));
    SharedContent a;
    a.name = "00000000";
    a.sha1.fill(0x11);
    SharedContent b;
    b.name = "0000000a";
    b.sha1.fill(0x22);
    entries = {a, b};
    EXPECT_EQ(next_shared_name(entries), std::string("0000000b"));
    const std::vector<std::uint8_t> bytes = serialize_content_map(entries);
    EXPECT_EQ(bytes.size(), std::size_t(2 * 0x1C));
    const std::vector<SharedContent> back = parse_content_map(bytes.data(), bytes.size());
    EXPECT_EQ(back.size(), std::size_t(2));
    Sha1Digest want;
    want.fill(0x22);
    const SharedContent* found = find_shared_content(back, want);
    EXPECT_TRUE(found != nullptr && found->name == "0000000a");
    want.fill(0x33);
    EXPECT_TRUE(find_shared_content(back, want) == nullptr);
    // A torn last entry is left out.
    EXPECT_EQ(parse_content_map(bytes.data(), bytes.size() - 1).size(), std::size_t(1));
}

class MemoryWad final : public ByteSource {
public:
    explicit MemoryWad(std::vector<std::uint8_t> bytes) : bytes(std::move(bytes)) {}
    std::uint64_t size() const override { return bytes.size(); }
    bool read(std::uint64_t offset, std::uint8_t* out, std::size_t length) const override {
        if (offset > bytes.size() || length > bytes.size() - offset) return false;
        std::memcpy(out, bytes.data() + offset, length);
        return true;
    }
    std::vector<std::uint8_t> bytes;
};

void test_metadata_and_banner() {
    MemoryWad source(build_wad().file);
    WadMetadata metadata;
    std::string error;
    EXPECT_TRUE(read_wad_metadata(source, metadata, error));
    EXPECT_EQ(metadata.tmd.title_id, kTitle);
    std::vector<std::uint8_t> banner;
    EXPECT_TRUE(read_wad_banner(source, metadata, kCommonKey, banner, error));
    EXPECT_TRUE(banner == build_wad().plain[0]);
    const Sha1Digest original_stamp = metadata.fingerprint;
    const std::size_t ticket = static_cast<std::size_t>(metadata.layout.ticket_offset);
    const std::size_t tmd = static_cast<std::size_t>(metadata.layout.tmd_offset);
    source.bytes[ticket + 0x1DC] ^= 1;
    EXPECT_TRUE(!read_wad_metadata(source, metadata, error));
    source.bytes = build_wad().file;
    source.bytes[tmd] = 0xFF;
    EXPECT_TRUE(!read_wad_metadata(source, metadata, error));
    source.bytes = build_wad().file;
    put16(source.bytes.data() + tmd + 0x1E0, 7);
    EXPECT_TRUE(!read_wad_metadata(source, metadata, error));
    source.bytes = build_wad().file;
    put16(source.bytes.data() + tmd + 0x1E4 + 36 + 4, 0);
    EXPECT_TRUE(!read_wad_metadata(source, metadata, error));
    source.bytes = build_wad().file;
    put32(source.bytes.data() + tmd + 0x1E4 + 36, 0);
    EXPECT_TRUE(!read_wad_metadata(source, metadata, error));
    source.bytes = build_wad().file;
    source.bytes[tmd + 0x1DC] = 1;  // a title-version change invalidates the cache
    EXPECT_TRUE(read_wad_metadata(source, metadata, error));
    EXPECT_TRUE(metadata.fingerprint != original_stamp);
    source.bytes[static_cast<std::size_t>(metadata.places[0].offset)] ^= 1;
    EXPECT_TRUE(!read_wad_banner(source, metadata, kCommonKey, banner, error));
}

void test_title_key_sources() {
    MemoryWad source(build_wad().file);
    WadMetadata metadata;
    std::string error;
    EXPECT_TRUE(read_wad_metadata(source, metadata, error));

    // -1 is unavailable (and may still dirty its output), 0 is a wrong
    // nonzero key, 1 is the invented title key used to encrypt this WAD.
    struct Case {
        int keys[3];
        int calls;
        int selected;
    };
    const Case cases[] = {
        {{1, 1, 1}, 1, 0},
        {{0, 1, 1}, 2, 1},  // Dolphin: OTP returns nonzero but incorrect bytes
        {{-1, 1, 1}, 2, 1},
        {{-1, -1, 1}, 3, 2},  // no hardware access and IOS denies the call
        {{0, 0, 1}, 3, 2},
        {{0, -1, 0}, 3, -1},
        {{-1, -1, -1}, 3, -1},
    };
    for (const Case& test : cases) {
        std::vector<WadKeySource> calls;
        std::uint8_t key[16];
        std::memset(key, 0xCC, sizeof key);
        std::vector<std::uint8_t> banner = {0xCC};
        WadKeySource selected = WadKeySource::Otp;
        error = "an old error";
        const bool found = select_wad_title_key(source, metadata,
            [&](WadKeySource candidate, std::uint8_t* out) {
                for (unsigned i = 0; i < 16; ++i) EXPECT_EQ(out[i], 0);
                calls.push_back(candidate);
                const int choice = test.keys[static_cast<unsigned>(candidate)];
                std::memset(out, 0x55, 16);
                if (choice == 1) std::memcpy(out, kTitleKey, 16);
                return choice >= 0;
            }, key, selected, banner, error);
        EXPECT_TRUE(found == (test.selected >= 0));
        EXPECT_EQ(calls.size(), static_cast<std::size_t>(test.calls));
        for (std::size_t i = 0; i < calls.size(); ++i) EXPECT_EQ(static_cast<unsigned>(calls[i]), i);
        if (found) {
            EXPECT_EQ(static_cast<int>(selected), test.selected);
            EXPECT_TRUE(std::memcmp(key, kTitleKey, 16) == 0);
            EXPECT_TRUE(banner == build_wad().plain[0]);
            EXPECT_TRUE(error.empty());
        } else {
            for (std::uint8_t byte : key) EXPECT_EQ(byte, 0);
            EXPECT_TRUE(banner.empty());
            EXPECT_TRUE(!error.empty());
        }
        clear_wad_key(key, sizeof key);
    }

    // Correct keys cannot turn damaged content into an accepted banner.
    source.bytes[static_cast<std::size_t>(metadata.places[0].offset)] ^= 1;
    unsigned calls = 0;
    std::uint8_t key[16] = {};
    std::vector<std::uint8_t> banner;
    WadKeySource selected = WadKeySource::Otp;
    EXPECT_TRUE(!select_wad_title_key(source, metadata,
        [&](WadKeySource, std::uint8_t* out) {
            ++calls;
            std::memcpy(out, kTitleKey, 16);
            return true;
        }, key, selected, banner, error));
    EXPECT_EQ(calls, 3u);
    for (std::uint8_t byte : key) EXPECT_EQ(byte, 0);
    EXPECT_TRUE(banner.empty());
}

// Installing proves the key on a content, so a channel WAD without a
// banner (no content at index 0) still finds its key, in the same order.
void test_install_key_without_banner() {
    MemoryWad source(build_wad(2).file);
    WadMetadata metadata;
    std::string error;
    EXPECT_TRUE(read_wad_metadata(source, metadata, error));
    const auto keys = [](int wrong_until) {
        return [wrong_until](WadKeySource candidate, std::uint8_t* out) {
            std::memset(out, 0x55, 16);
            if (static_cast<int>(candidate) >= wrong_until) std::memcpy(out, kTitleKey, 16);
            return true;
        };
    };
    std::uint8_t key[16] = {};
    std::vector<std::uint8_t> banner;
    WadKeySource selected = WadKeySource::Otp;
    // The banner route has nothing to check against.
    EXPECT_TRUE(!select_wad_title_key(source, metadata, keys(0), key, selected, banner, error));
    // The install route: OTP gives a wrong key, IOS the right one.
    EXPECT_TRUE(select_wad_install_key(source, metadata, keys(1), key, selected, error));
    EXPECT_EQ(static_cast<int>(selected), 1);
    EXPECT_TRUE(std::memcmp(key, kTitleKey, 16) == 0);
    clear_wad_key(key, sizeof key);
    // Every source wrong: refused, the key cleared.
    EXPECT_TRUE(!select_wad_install_key(source, metadata, keys(3), key, selected, error));
    for (std::uint8_t byte : key) EXPECT_EQ(byte, 0);
    EXPECT_TRUE(!error.empty());
    // A damaged content is refused even with the right key.
    source.bytes[static_cast<std::size_t>(metadata.places[0].offset)] ^= 1;
    EXPECT_TRUE(!verify_wad_title_key(source, metadata, kTitleKey, error));
}

void test_cache_and_preparation() {
    WadCacheRecord record;
    record.title_id = kTitle;
    record.fingerprint.fill(0x31);
    record.modified = 0x123456789ABCDEFull;
    record.banner_size = 0x12345;
    record.banner_head.assign(0x700, 0x42);
    std::vector<std::uint8_t> bytes;
    EXPECT_TRUE(encode_wad_cache(record, bytes));
    WadCacheRecord decoded;
    EXPECT_TRUE(decode_wad_cache(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.title_id, record.title_id);
    EXPECT_EQ(decoded.modified, record.modified);
    EXPECT_TRUE(decoded.fingerprint == record.fingerprint);
    EXPECT_TRUE(decoded.banner_head == record.banner_head);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
        EXPECT_TRUE(!decode_wad_cache(bytes.data(), cut, decoded));
    bytes[50] ^= 1;
    EXPECT_TRUE(!decode_wad_cache(bytes.data(), bytes.size(), decoded));
    EXPECT_TRUE(!decode_wad_cache(nullptr, 100, decoded));
    record.banner_head.resize(0x701);
    EXPECT_TRUE(!encode_wad_cache(record, bytes));

    Sha1Digest source{};
    Sha1Digest changed{};
    changed[0] = 1;
    EXPECT_TRUE(!wad_needs_preparation(source, &source, true, true));
    EXPECT_TRUE(wad_needs_preparation(changed, &source, true, true));
    EXPECT_TRUE(wad_needs_preparation(source, &source, false, true));
    EXPECT_TRUE(!wad_needs_preparation(source, nullptr, true, true));
    EXPECT_TRUE(wad_needs_preparation(source, nullptr, true, false));
    EXPECT_TRUE(wad_needs_preparation(source, nullptr, false, false));

    std::vector<std::uint8_t> owners;
    std::string error;
    EXPECT_TRUE(add_wad_title_uid(owners, kTitle, error));
    EXPECT_EQ(owners.size(), std::size_t(24));
    EXPECT_EQ(owners[3], 1);
    EXPECT_EQ(owners[7], 2);
    EXPECT_EQ(owners[10], 0x10);
    EXPECT_EQ(owners[23], 1);
    const auto original = owners;
    EXPECT_TRUE(add_wad_title_uid(owners, kTitle, error));
    EXPECT_TRUE(owners == original);
    EXPECT_TRUE(add_wad_title_uid(owners, kTitle + 1, error));
    EXPECT_EQ(owners.size(), std::size_t(36));
    EXPECT_EQ(owners[35], 2);
    owners.push_back(0);
    EXPECT_TRUE(!add_wad_title_uid(owners, kTitle, error));
    owners = original;
    put32(owners.data() + 8, UINT32_MAX);
    EXPECT_TRUE(!add_wad_title_uid(owners, kTitle + 1, error));
}

}  // namespace

int main() {
    test_layout_and_contents();
    test_bad_files();
    test_content_map();
    test_metadata_and_banner();
    test_title_key_sources();
    test_install_key_without_banner();
    test_cache_and_preparation();
    if (g_failures == 0) std::cout << "wadfile tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
