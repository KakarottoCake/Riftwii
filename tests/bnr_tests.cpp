// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The banner chain: AES and Wii partition decryption, LZ77, U8, IMET,
// TPL, BRLYT and BRLAN, on small files built here.
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/bnr.hpp"
#include "riftwii/brlan.hpp"
#include "riftwii/brlyt.hpp"
#include "riftwii/source.hpp"
#include "riftwii/tpl.hpp"
#include "riftwii/wiicrypt.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

namespace {

void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}
void put16(Bytes& b, std::size_t at, std::uint16_t v) {
    if (b.size() < at + 2) b.resize(at + 2);
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}
void putf(Bytes& b, std::size_t at, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, at, v);
}
void puts_(Bytes& b, std::size_t at, const std::string& s) {
    if (b.size() < at + s.size()) b.resize(at + s.size());
    std::memcpy(b.data() + at, s.data(), s.size());
}
std::string hex(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

// A U8 archive of files under one directory each: {"dir/name", bytes}.
Bytes make_u8(const std::vector<std::pair<std::string, Bytes>>& files) {
    // Nodes: root, then for each file its directory (if new) and itself.
    struct Node {
        bool dir;
        std::string name;
        std::size_t file = 0;
        std::uint32_t next = 0;
    };
    std::vector<Node> nodes = {{true, "", 0, 0}};
    std::string last_dir;
    std::size_t dir_index = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string& path = files[i].first;
        const std::size_t slash = path.find('/');
        const std::string dir = path.substr(0, slash);
        if (dir != last_dir) {
            if (dir_index) nodes[dir_index].next = static_cast<std::uint32_t>(nodes.size());
            dir_index = nodes.size();
            nodes.push_back({true, dir, 0, 0});
            last_dir = dir;
        }
        nodes.push_back({false, path.substr(slash + 1), i, 0});
    }
    if (dir_index) nodes[dir_index].next = static_cast<std::uint32_t>(nodes.size());
    nodes[0].next = static_cast<std::uint32_t>(nodes.size());
    std::string strings(1, '\0');
    std::vector<std::uint32_t> name_offsets;
    for (const Node& n : nodes) {
        name_offsets.push_back(n.name.empty() ? 0 : static_cast<std::uint32_t>(strings.size()));
        if (!n.name.empty()) strings += n.name + '\0';
    }
    const std::uint32_t header = static_cast<std::uint32_t>(nodes.size() * 12 + strings.size());
    std::uint32_t data = (0x20 + header + 0x3F) & ~0x3Fu;
    Bytes out(data, 0);
    put32(out, 0, 0x55AA382D);
    put32(out, 4, 0x20);
    put32(out, 8, header);
    put32(out, 12, data);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t at = 0x20 + i * 12;
        put32(out, at, (nodes[i].dir ? 0x01000000u : 0) | name_offsets[i]);
        if (nodes[i].dir) {
            put32(out, at + 4, 0);
            put32(out, at + 8, nodes[i].next);
        } else {
            const Bytes& f = files[nodes[i].file].second;
            put32(out, at + 4, static_cast<std::uint32_t>(out.size()));
            put32(out, at + 8, static_cast<std::uint32_t>(f.size()));
            out.insert(out.end(), f.begin(), f.end());
            while (out.size() % 32) out.push_back(0);
        }
    }
    std::memcpy(out.data() + 0x20 + nodes.size() * 12, strings.data(), strings.size());
    return out;
}

}  // namespace

static void test_aes() {
    // FIPS-197 appendix C.1.
    std::uint8_t key[16], pt[16], ct[16], back[16];
    for (int i = 0; i < 16; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
        pt[i] = static_cast<std::uint8_t>(i * 0x11);
    }
    const Aes128 aes(key);
    aes.encrypt_block(pt, ct);
    EXPECT_EQ(hex(ct, 16), std::string("69c4e0d86a7b0430d8cdb78070b4c55a"));
    aes.decrypt_block(ct, back);
    EXPECT_TRUE(std::memcmp(back, pt, 16) == 0);
    // CBC both ways, in place.
    std::uint8_t iv[16] = {9, 8, 7};
    Bytes data(64);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 7);
    const Bytes plain = data;
    aes.cbc_encrypt(iv, data.data(), data.data(), data.size());
    EXPECT_TRUE(data != plain);
    aes.cbc_decrypt(iv, data.data(), data.data(), data.size());
    EXPECT_TRUE(data == plain);
}

static void test_partition() {
    // A disc with one game partition at 0x50000: ticket, TMD, then two
    // clusters of data encrypted as a real disc's.
    std::uint8_t common[16], title_key[16];
    for (int i = 0; i < 16; ++i) {
        common[i] = static_cast<std::uint8_t>(0xA0 + i);
        title_key[i] = static_cast<std::uint8_t>(0x30 + 3 * i);
    }
    const std::uint64_t part = 0x50000, data_rel = 0x20000;
    Bytes disc(part + data_rel + 2 * kClusterBytes, 0);
    put32(disc, 0x40000, 1);
    put32(disc, 0x40004, 0x40020 >> 2);
    put32(disc, 0x40020, static_cast<std::uint32_t>(part >> 2));
    put32(disc, 0x40024, 0);
    // Ticket: the title key encrypted with the common key, the title ID as IV.
    const std::uint8_t tid[8] = {0, 1, 0, 0, 'R', 'T', 'S', 'T'};
    std::memcpy(disc.data() + part + 0x1DC, tid, 8);
    std::uint8_t iv[16] = {};
    std::memcpy(iv, tid, 8);
    Aes128(common).cbc_encrypt(iv, title_key, disc.data() + part + 0x1BF, 16);
    put32(disc, part + 0x2A4, 0x208);
    put32(disc, part + 0x2A8, 0x2C0 >> 2);
    put32(disc, part + 0x2B8, static_cast<std::uint32_t>(data_rel >> 2));
    put32(disc, part + 0x2BC, static_cast<std::uint32_t>((2 * kClusterBytes) >> 2));
    Bytes plain(2 * kClusterDataBytes);
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<std::uint8_t>((i * 31) ^ (i >> 9));
    const Aes128 tk(title_key);
    for (int c = 0; c < 2; ++c) {
        std::uint8_t* cl = disc.data() + part + data_rel + c * kClusterBytes;
        for (int i = 0; i < 0x400; ++i) cl[i] = static_cast<std::uint8_t>(i * 13 + c);
        tk.cbc_encrypt(cl + 0x3D0, plain.data() + c * kClusterDataBytes, cl + 0x400, kClusterDataBytes);
    }
    MemorySource src(disc);
    std::unique_ptr<WiiPartitionSource> p;
    std::string error;
    EXPECT_TRUE(open_game_partition(src, common, p, error));
    if (!p) {
        std::cerr << error << "\n";
        return;
    }
    EXPECT_EQ(p->size(), static_cast<std::uint64_t>(2 * kClusterDataBytes));
    // Across the clusters' seam.
    Bytes got(0x200);
    EXPECT_TRUE(p->read(kClusterDataBytes - 0x100, got.data(), got.size()));
    EXPECT_TRUE(std::memcmp(got.data(), plain.data() + kClusterDataBytes - 0x100, got.size()) == 0);
    EXPECT_TRUE(!p->read(2 * kClusterDataBytes - 4, got.data(), 8));
    // A Korean ticket is refused.
    disc[part + 0x1F1] = 1;
    MemorySource korean(disc);
    EXPECT_TRUE(!open_game_partition(korean, common, p, error));
}

static void test_lz77_u8() {
    // "A", then six more copied from one back, then "B".
    const Bytes lz = {'L', 'Z', '7', '7', 0x10, 8, 0, 0, 0x40, 'A', 0x30, 0x00, 'B'};
    Bytes out;
    std::string error;
    EXPECT_TRUE(lz77_decompress(lz.data(), lz.size(), out, error));
    EXPECT_EQ(std::string(out.begin(), out.end()), std::string("AAAAAAAB"));
    const Bytes bad = {'L', 'Z', '7', '7', 0x10, 8, 0, 0, 0x40, 'A', 0x30, 0x05};
    EXPECT_TRUE(!lz77_decompress(bad.data(), bad.size(), out, error));

    const Bytes u8 = make_u8({{"arc/one.bin", {1, 2, 3}}, {"arc/two.tpl", {4}}, {"meta/x", {5, 6}}});
    U8Archive a;
    EXPECT_TRUE(U8Archive::parse(u8.data(), u8.size(), a, error));
    EXPECT_EQ(a.files().size(), std::size_t(3));
    const std::uint8_t* d = nullptr;
    std::size_t n = 0;
    EXPECT_TRUE(a.find("/ARC/two.TPL", d, n));
    EXPECT_EQ(n, std::size_t(1));
    if (d) EXPECT_EQ(static_cast<int>(d[0]), 4);
    EXPECT_TRUE(a.find("/./meta/x", d, n));
    EXPECT_EQ(a.files_in("/arc", ".tpl").size(), std::size_t(1));
    EXPECT_TRUE(!a.find("/arc/three", d, n));
}

static void test_opening() {
    // An IMET with an English name, then meta/icon.bin: IMD5 over LZ77 over U8.
    const Bytes inner = make_u8({{"arc/a.tpl", {7}}});
    Bytes lz = {'L', 'Z', '7', '7', 0x10, 0, 0, 0};
    const std::uint32_t size = static_cast<std::uint32_t>(inner.size());
    lz[5] = static_cast<std::uint8_t>(size);
    lz[6] = static_cast<std::uint8_t>(size >> 8);
    lz[7] = static_cast<std::uint8_t>(size >> 16);
    for (std::size_t i = 0; i < inner.size(); i += 8) {
        lz.push_back(0);  // eight literals
        for (std::size_t k = i; k < i + 8 && k < inner.size(); ++k) lz.push_back(inner[k]);
    }
    Bytes icon(0x20, 0);
    puts_(icon, 0, "IMD5");
    icon.insert(icon.end(), lz.begin(), lz.end());
    Bytes bnr(0x600, 0);
    puts_(bnr, 0x40, "IMET");
    const std::string name = "Hi\xC3\xA9";  // "Hié"
    const std::size_t en = 0x40 + 0x1C + 84;
    put16(bnr, en, 'H');
    put16(bnr, en + 2, 'i');
    put16(bnr, en + 4, 0xE9);
    const Bytes outer = make_u8({{"meta/icon.bin", icon}});
    bnr.insert(bnr.end(), outer.begin(), outer.end());
    OpeningBanner b;
    std::string error;
    EXPECT_TRUE(parse_opening_bnr(bnr.data(), bnr.size(), b, error));
    EXPECT_EQ(b.names[kBnrEnglish], name);
    EXPECT_TRUE(b.icon == inner);
    EXPECT_TRUE(b.banner.empty());
    bnr[0x40] = 'X';
    EXPECT_TRUE(!parse_opening_bnr(bnr.data(), bnr.size(), b, error));
}

static void test_tpl() {
    // One 4x4 RGB5A3 image: opaque red, then a half-transparent 4443 pixel.
    Bytes t(0x40 + 32, 0);
    put32(t, 0, 0x0020AF30);
    put32(t, 4, 1);
    put32(t, 8, 0xC);
    put32(t, 0xC, 0x14);
    put32(t, 0x10, 0);
    put16(t, 0x14, 4);
    put16(t, 0x16, 4);
    put32(t, 0x18, kTplRGB5A3);
    put32(t, 0x1C, 0x40);
    put16(t, 0x40, 0xFC00);  // 1 11111 00000 00000
    put16(t, 0x42, 0x4F00);  // 0 100 1111 0000 0000: alpha 4/7, red
    std::vector<TplImage> images;
    std::string error;
    EXPECT_TRUE(parse_tpl(t.data(), t.size(), images, error));
    EXPECT_EQ(images.size(), std::size_t(1));
    if (images.empty()) return;
    EXPECT_EQ(images[0].data_size, std::size_t(32));
    Bytes rgba;
    EXPECT_TRUE(tpl_to_rgba(t.data(), t.size(), images[0], rgba, error));
    EXPECT_EQ(static_cast<int>(rgba[0]), 255);
    EXPECT_EQ(static_cast<int>(rgba[1]), 0);
    EXPECT_EQ(static_cast<int>(rgba[3]), 255);
    EXPECT_EQ(static_cast<int>(rgba[4]), 255);
    EXPECT_EQ(static_cast<int>(rgba[7]), 146);  // 100 -> 0b10010010
    EXPECT_EQ(gx_texture_size(kTplI4, 9, 8), std::size_t(64));
    EXPECT_EQ(gx_texture_size(kTplRGBA8, 4, 4), std::size_t(64));
    EXPECT_EQ(gx_texture_size(kTplCMPR, 16, 8), std::size_t(64));
    put32(t, 0x18, 7);
    EXPECT_TRUE(!parse_tpl(t.data(), t.size(), images, error));
}

// A layout: a root pane, under it a 20x10 picture centred at (10, 0) and
// two language groups.
static Bytes make_layout() {
    Bytes b(0x10, 0);
    puts_(b, 0, "RLYT");
    put16(b, 4, 0xFEFF);
    put16(b, 6, 10);
    put16(b, 0xC, 0x10);
    const auto section = [&](const std::string& magic, const Bytes& body) {
        const std::size_t at = b.size();
        puts_(b, at, magic);
        put32(b, at + 4, static_cast<std::uint32_t>(8 + body.size()));
        b.insert(b.end(), body.begin(), body.end());
    };
    Bytes lyt(12, 0);
    lyt[0] = 1;
    putf(lyt, 4, 608);
    putf(lyt, 8, 456);
    section("lyt1", lyt);
    Bytes txl(4, 0);
    put16(txl, 0, 1);
    put32(txl, 4, 8);
    put32(txl, 8, 0);
    puts_(txl, 12, std::string("a.tpl") + '\0');
    while (txl.size() % 4) txl.push_back(0);
    section("txl1", txl);
    // One material: black 0, white 255, one texture map.
    Bytes mat(8, 0);
    put16(mat, 0, 1);
    put32(mat, 4, 0x10);  // counted from the section start
    Bytes m(0x44, 0);
    puts_(m, 0, "M_pic");
    for (int i = 0; i < 4; ++i) put16(m, 0x1C + 2 * i, 255);
    put32(m, 0x3C, 1);
    put16(m, 0x40, 0);
    mat.insert(mat.end(), m.begin(), m.end());
    section("mat1", mat);
    const auto pane = [](const std::string& name, float tx, float w, float h) {
        Bytes p(0x44, 0);
        p[0] = 1;
        p[1] = 4;
        p[2] = 255;
        puts_(p, 4, name);
        putf(p, 0x1C, tx);
        putf(p, 0x34, 1);
        putf(p, 0x38, 1);
        putf(p, 0x3C, w);
        putf(p, 0x40, h);
        return p;
    };
    section("pan1", pane("RootPane", 0, 608, 456));
    section("pas1", {});
    Bytes pic = pane("P_pic", 10, 20, 10);
    Bytes content(0x14, 0xFF);
    put16(content, 0x10, 0);
    content[0x12] = 0;
    content[0x13] = 0;
    pic.insert(pic.end(), content.begin(), content.end());
    section("pic1", pic);
    Bytes pic2 = pane("P_jpn", -50, 4, 4);
    pic2.insert(pic2.end(), content.begin(), content.end());
    section("pic1", pic2);
    section("pae1", {});
    for (const auto& g : std::vector<std::pair<std::string, std::string>>{{"ENG", "P_pic"}, {"JPN", "P_jpn"}}) {
        Bytes grp(0x14 + 16, 0);
        puts_(grp, 0, g.first);
        put16(grp, 0x10, 1);
        puts_(grp, 0x14, g.second);
        section("grp1", grp);
    }
    return b;
}

static void test_layout() {
    const Bytes b = make_layout();
    Layout lyt;
    std::string error;
    EXPECT_TRUE(parse_brlyt(b.data(), b.size(), lyt, error));
    if (!error.empty()) std::cerr << error << "\n";
    EXPECT_EQ(lyt.panes.size(), std::size_t(3));
    EXPECT_EQ(lyt.textures.size(), std::size_t(1));
    EXPECT_EQ(lyt.materials.size(), std::size_t(1));
    EXPECT_EQ(lyt.groups.size(), std::size_t(2));
    if (lyt.panes.size() != 3 || lyt.materials.empty()) return;
    EXPECT_EQ(lyt.panes[1].parent, 0);
    EXPECT_EQ(lyt.materials[0].maps.size(), std::size_t(1));
    EXPECT_EQ(lyt.materials[0].white[0], 255);
    show_language(lyt, "GER");  // no German: English
    EXPECT_TRUE(lyt.panes[1].visible());
    EXPECT_TRUE(!lyt.panes[2].visible());
    std::vector<LytQuad> q = layout_quads(lyt);
    EXPECT_EQ(q.size(), std::size_t(1));
    if (!q.empty()) {
        EXPECT_EQ(q[0].x[0], 0.0f);
        EXPECT_EQ(q[0].x[1], 20.0f);
        EXPECT_EQ(q[0].y[0], 5.0f);
        EXPECT_EQ(q[0].y[2], -5.0f);
    }
    // 16:9: a pane flagged to keep its shape is narrowed, once, even when
    // its parent is flagged too.
    {
        Layout wide = lyt;
        wide.panes[1].flags = static_cast<std::uint8_t>(wide.panes[1].flags | 4);
        std::vector<LytQuad> n;
        LytScratch scratch;
        layout_quads(wide, n, scratch, 0.5f);
        EXPECT_EQ(n.size(), std::size_t(1));
        if (!n.empty()) {
            EXPECT_EQ(n[0].x[0], 5.0f);  // about the pane's place (x 10)
            EXPECT_EQ(n[0].x[1], 15.0f);
            EXPECT_EQ(n[0].y[0], 5.0f);
        }
        wide.panes[0].flags = static_cast<std::uint8_t>(wide.panes[0].flags | 4);
        layout_quads(wide, n, scratch, 0.5f);
        if (!n.empty()) EXPECT_EQ(n[0].x[1], 10.0f);  // about the root's place, once
        layout_quads(wide, n, scratch);  // 4:3: as laid out
        if (!n.empty()) EXPECT_EQ(n[0].x[1], 20.0f);
    }
    // A texel through the material: grey stays grey, halved by the vertex alpha.
    const LytColor grey{128, 128, 128, 255};
    const LytColor half{255, 255, 255, 128};
    const LytColor c = shade_texel(lyt.materials[0], &grey, half);
    EXPECT_EQ(static_cast<int>(c.r), 128);
    EXPECT_EQ(static_cast<int>(c.a), 128);

    // An animation sliding P_pic from x 0 to x 100 over 10 frames.
    Bytes a(0x10, 0);
    puts_(a, 0, "RLAN");
    put16(a, 4, 0xFEFF);
    put16(a, 0xC, 0x10);
    Bytes pai(0x18, 0);
    puts_(pai, 0, "pai1");
    put16(pai, 8, 10);
    put16(pai, 0xE, 1);
    put32(pai, 0x10, 0x14);
    put32(pai, 0x14, 0x18);
    Bytes an(0x1C, 0);
    puts_(an, 0, "P_pic");
    an[0x14] = 1;
    put32(an, 0x18, 0x1C);
    Bytes tag(0xC, 0);
    puts_(tag, 0, "RLPA");
    tag[4] = 1;
    put32(tag, 8, 0xC);
    Bytes entry(0xC, 0);
    entry[1] = 0;  // translate X
    entry[2] = 2;
    put16(entry, 4, 2);
    put32(entry, 8, 0xC);
    for (const auto& k : std::vector<std::pair<float, float>>{{0, 0}, {10, 100}}) {
        const std::size_t at = entry.size();
        putf(entry, at, k.first);
        putf(entry, at + 4, k.second);
        putf(entry, at + 8, 0);
    }
    tag.insert(tag.end(), entry.begin(), entry.end());
    an.insert(an.end(), tag.begin(), tag.end());
    pai.insert(pai.end(), an.begin(), an.end());
    put32(pai, 4, static_cast<std::uint32_t>(pai.size()));
    a.insert(a.end(), pai.begin(), pai.end());
    Animation anim;
    EXPECT_TRUE(parse_brlan(a.data(), a.size(), anim, error));
    if (!error.empty()) std::cerr << error << "\n";
    EXPECT_EQ(anim.targets.size(), std::size_t(1));
    if (anim.targets.empty() || anim.targets[0].tracks.empty()) return;
    EXPECT_TRUE(std::fabs(track_value(anim.targets[0].tracks[0], 5) - 50.0f) < 0.01f);
    EXPECT_EQ(track_value(anim.targets[0].tracks[0], -3), 0.0f);
    EXPECT_EQ(track_value(anim.targets[0].tracks[0], 30), 100.0f);
    const Layout base = lyt;
    apply_animation(anim, 10, lyt);
    EXPECT_EQ(lyt.panes[1].tx, 100.0f);
    reset_animated(base, lyt);
    EXPECT_EQ(lyt.panes[1].tx, 10.0f);

    // A kind RiftWii does not apply (indirect matrices): parsed, skipped and named.
    Bytes other = a;
    for (std::size_t i = 0; i + 4 <= other.size(); ++i)
        if (std::memcmp(other.data() + i, "RLPA", 4) == 0) std::memcpy(other.data() + i, "RLIM", 4);
    Animation skipping;
    EXPECT_TRUE(parse_brlan(other.data(), other.size(), skipping, error));
    EXPECT_EQ(skipping.skipped.size(), std::size_t(1));
    if (!skipping.skipped.empty()) EXPECT_EQ(skipping.skipped[0], std::string("RLIM"));
    const std::string said = describe_banner(lyt, nullptr, &anim);
    EXPECT_TRUE(said.find("no start") != std::string::npos);
    EXPECT_TRUE(said.find("loop 10 frames (pane 1)") != std::string::npos);
    EXPECT_TRUE(describe_banner(lyt, &skipping, nullptr).find("skipped kinds: RLIM") != std::string::npos);
}

int main() {
    test_aes();
    test_partition();
    test_lz77_u8();
    test_opening();
    test_tpl();
    test_layout();
    if (g_failures == 0) {
        std::cout << "ALL BNR TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
