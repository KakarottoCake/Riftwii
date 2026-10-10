// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/imagevolume.hpp"
#include "riftwii/vsdbuild.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

class MemorySink final : public VsdSink {
public:
    std::vector<std::uint8_t> bytes;
    bool write(const std::uint8_t* data, std::size_t length) override {
        bytes.insert(bytes.end(), data, data + length);
        return true;
    }
};

// A file's bytes: a pattern from its path, so a mix-up shows.
std::uint8_t pattern(const std::string& path, std::uint64_t at) {
    std::uint32_t h = 2166136261u;
    for (char c : path) h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    return static_cast<std::uint8_t>(h + at * 7 + (at >> 9));
}

bool read_pattern(const std::string& path, std::uint64_t offset, std::uint8_t* out, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) out[i] = pattern(path, offset + i);
    return true;
}

struct Built {
    VsdPlan plan;
    MemorySink sink;
    std::unique_ptr<ImageVolume> volume;
    Fat32Volume fat;
};

bool build(const std::vector<VsdItem>& items, std::uint64_t spare, Built& b) {
    std::string error;
    if (!plan_vsd_image(items, spare, b.plan, error)) {
        std::cerr << "plan: " << error << std::endl;
        return false;
    }
    std::vector<std::uint8_t> buffer;
    if (!write_vsd_image(b.plan, b.sink, read_pattern, nullptr, buffer, error)) {
        std::cerr << "write: " << error << std::endl;
        return false;
    }
    if (b.sink.bytes.size() != b.plan.image_bytes) {
        std::cerr << "size " << b.sink.bytes.size() << " != " << b.plan.image_bytes << std::endl;
        return false;
    }
    const std::vector<std::uint8_t>* bytes = &b.sink.bytes;
    BlockReader reader = [bytes](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
        if ((lba + count) * 512 > bytes->size()) return false;
        std::memcpy(out, bytes->data() + lba * 512, std::size_t(count) * 512);
        return true;
    };
    if (!mount_image_volume(reader, b.volume, error)) {
        std::cerr << "mount: " << error << std::endl;
        return false;
    }
    if (!Fat32Volume::mount(reader, b.fat, error)) {
        std::cerr << "fat mount: " << error << std::endl;
        return false;
    }
    return true;
}

bool same_file(const Built& b, const std::string& path, std::uint64_t size) {
    VolumeFile f;
    std::string error;
    if (!b.volume->lookup(path, f, error)) {
        std::cerr << "lookup " << path << ": " << error << std::endl;
        return false;
    }
    if (f.entry.size != size || f.entry.is_directory) return false;
    std::vector<std::uint8_t> got(static_cast<std::size_t>(size));
    if (size && !b.volume->read(f, 0, got.data(), got.size())) return false;
    for (std::uint64_t i = 0; i < size; ++i)
        if (got[static_cast<std::size_t>(i)] != pattern(path, i)) return false;
    return true;
}

}  // namespace

static void test_short_names() {
    bool lng = false;
    EXPECT_EQ(vsd_short_name("RSBE01.GCT", {}, lng), std::string("RSBE01  GCT"));
    EXPECT_FALSE(lng);
    EXPECT_EQ(vsd_short_name("RSBE01.gct", {}, lng), std::string("RSBE01  GCT"));
    EXPECT_TRUE(lng);  // the case is kept in the long name
    EXPECT_EQ(vsd_short_name("Project+", {}, lng), std::string("PROJEC~1   "));
    EXPECT_TRUE(lng);
    EXPECT_EQ(vsd_short_name("gameconfig.txt", {}, lng), std::string("GAMECO~1TXT"));
    EXPECT_TRUE(lng);
    EXPECT_EQ(vsd_short_name("gameconfig.txt", {"GAMECO~1TXT"}, lng), std::string("GAMECO~2TXT"));
    EXPECT_EQ(vsd_short_name("info.json.bak", {}, lng), std::string("INFOJS~1BAK"));
    EXPECT_EQ(vsd_short_name(".hidden", {}, lng), std::string("HIDDEN~1   "));
    EXPECT_TRUE(lng);
    EXPECT_EQ(vsd_short_name("A", {"A          "}, lng), std::string("A~1        "));
    EXPECT_TRUE(lng);
}

static void test_small_card() {
    std::vector<VsdItem> items = {
        {"/Project+/codes/RSBE01.gct", false, 70000},
        {"/Project+/pf/sound/strm/Menu Theme (Melee).brstm", false, 5 * 1024 * 1024 + 3},
        {"/Project+/pf/stage", true, 0},
        {"/Project+/empty.txt", false, 0},
        {"/gameconfig.txt", false, 812},
        {"/private/wii/app/RSBE/st/st_custom.rel", false, 4096},
        {"/Project+/Ünïcode.bin", false, 100},
    };
    Built b;
    EXPECT_TRUE(build(items, 64ull << 20, b));
    if (!b.volume) return;
    EXPECT_EQ(std::string(b.volume->kind()), std::string("FAT32"));
    EXPECT_EQ(b.plan.files, 6u);
    EXPECT_EQ(b.plan.folders, 11u);  // Project+, codes, pf, sound, strm, stage, private, wii, app, RSBE, st
    EXPECT_EQ(b.plan.image_bytes % (4u << 20), 0u);
    EXPECT_TRUE(b.plan.cluster_count >= 65525u);
    EXPECT_EQ(b.fat.geometry().cluster_count, b.plan.cluster_count);
    EXPECT_EQ(b.fat.geometry().fat_count, 2u);
    for (const VsdItem& i : items)
        if (!i.directory) EXPECT_TRUE(same_file(b, i.path, i.size));
    VolumeFile f;
    std::string error;
    EXPECT_TRUE(b.volume->lookup("/project+/CODES/rsbe01.GCT", f, error));

    std::vector<VolumeEntry> top;
    EXPECT_TRUE(b.volume->list("/", top, error));
    EXPECT_EQ(top.size(), std::size_t(3));
    std::vector<VolumeEntry> stage;
    EXPECT_TRUE(b.volume->list("/Project+/pf/stage", stage, error));
    EXPECT_EQ(stage.size(), std::size_t(0));
    std::vector<Fat32Entry> pp;
    EXPECT_TRUE(b.fat.list("/Project+", pp, error));
    bool unicode = false;
    for (const Fat32Entry& e : pp) unicode = unicode || e.name == "Ünïcode.bin";
    EXPECT_TRUE(unicode);

    // Every file is one run of clusters, as planned.
    Fat32File gct;
    EXPECT_TRUE(b.fat.lookup("/Project+/pf/sound/strm/Menu Theme (Melee).brstm", gct, error));
    EXPECT_EQ(gct.fragments.size(), std::size_t(1));

    // The free count in FSInfo matches the FAT.
    const std::uint8_t* fsinfo = b.sink.bytes.data() + 512;
    std::uint32_t free_count = 0;
    std::memcpy(&free_count, fsinfo + 488, 4);  // little-endian host
    EXPECT_EQ(free_count, b.plan.cluster_count - b.plan.used_clusters);
    EXPECT_TRUE(std::uint64_t(free_count) * b.plan.cluster_bytes >= (64ull << 20));
    // The backup boot sector.
    EXPECT_TRUE(std::memcmp(b.sink.bytes.data(), b.sink.bytes.data() + 6 * 512, 512) == 0);
}

static void test_big_folder() {
    // REX's biggest folder holds about 3,000 files.
    std::vector<VsdItem> items;
    for (int i = 0; i < 3000; ++i) {
        const std::string n = std::to_string(i);
        items.push_back({"/rex_/pf/fighter/costume_" + n + ".pac", false, std::uint64_t(100 + i)});
    }
    Built b;
    EXPECT_TRUE(build(items, 0, b));
    if (!b.volume) return;
    std::vector<VolumeEntry> all;
    std::string error;
    EXPECT_TRUE(b.volume->list("/rex_/pf/fighter", all, error));
    EXPECT_EQ(all.size(), std::size_t(3000));
    EXPECT_TRUE(same_file(b, "/rex_/pf/fighter/costume_0.pac", 100));
    EXPECT_TRUE(same_file(b, "/rex_/pf/fighter/costume_1234.pac", 1334));
    EXPECT_TRUE(same_file(b, "/rex_/pf/fighter/costume_2999.pac", 3099));
}

// Thousands of long names with one stem: every "~n" is unique, and the
// plan takes well under a second (it was cubic: 4000 took 40 s on a PC).
static void test_similar_names() {
    std::vector<VsdItem> items;
    for (int i = 0; i < 4000; ++i)
        items.push_back({"/rex_/Costume Long Name " + std::to_string(i) + ".pac", false, 10});
    VsdPlan p;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(plan_vsd_image(items, 0, p, error));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_TRUE(s < 2.0);
    std::set<std::string> shorts;
    for (const VsdPlan::Node& n : p.nodes)
        if (n.parent >= 0 && !n.directory) shorts.insert(n.short_name);
    EXPECT_EQ(shorts.size(), std::size_t(4000));
    EXPECT_TRUE(shorts.count("COSTU~10PAC") == 1);
    EXPECT_TRUE(shorts.count("COST~100PAC") == 1);
    // The incremental picker gives what the one-off one does.
    VsdShortNames names;
    std::vector<std::string> taken;
    bool a = false, b = false;
    for (const char* n : {"gameconfig.txt", "GameConfig.txt", "gameconfig.tx", "A", "a", "gameconfig.txt"}) {
        const std::string one = vsd_short_name(n, taken, a);
        EXPECT_EQ(names.add(n, b), one);
        EXPECT_EQ(a, b);
        taken.push_back(one);
    }
    // A folder past FAT32's 65,536 entries (each name here takes three).
    std::vector<VsdItem> crowded;
    for (int i = 0; i < 22000; ++i) crowded.push_back({"/f/long file name " + std::to_string(i), false, 1});
    EXPECT_FALSE(plan_vsd_image(crowded, 0, p, error));
    EXPECT_TRUE(error.find("more names") != std::string::npos);
}

static void test_plans() {
    VsdPlan p;
    std::string error;
    // A 3 GiB build: bigger clusters keep the FAT small.
    std::vector<VsdItem> big;
    for (int i = 0; i < 12; ++i) big.push_back({"/rex_/b" + std::to_string(i) + ".bin", false, 256ull << 20});
    EXPECT_TRUE(plan_vsd_image(big, 512ull << 20, p, error));
    EXPECT_TRUE(p.cluster_bytes >= 4096u);
    EXPECT_TRUE(p.cluster_count <= (1u << 20) + 8192u);
    EXPECT_TRUE(p.image_bytes >= (3584ull << 20));
    EXPECT_TRUE(std::uint64_t(p.cluster_count) * p.cluster_bytes >= (3584ull << 20));

    // Nothing at all still makes a card.
    EXPECT_TRUE(plan_vsd_image({}, 0, p, error));
    EXPECT_TRUE(p.cluster_count >= 65525u);

    EXPECT_FALSE(plan_vsd_image({{"/a.iso", false, 5ull << 30}}, 0, p, error));
    EXPECT_FALSE(plan_vsd_image({{"/a/b", false, 1}, {"/A/B", false, 2}}, 0, p, error));
    EXPECT_FALSE(plan_vsd_image({{"/a", false, 1}, {"/a/b", false, 2}}, 0, p, error));
    EXPECT_FALSE(plan_vsd_image({{"relative", false, 1}}, 0, p, error));
    EXPECT_FALSE(plan_vsd_image({{"/" + std::string(256, 'x'), false, 1}}, 0, p, error));
    // A folder listed twice is fine.
    EXPECT_TRUE(plan_vsd_image({{"/a", true, 0}, {"/a", true, 0}, {"/a/b", false, 1}}, 0, p, error));
}

static void test_progress_stops() {
    VsdPlan p;
    std::string error;
    EXPECT_TRUE(plan_vsd_image({{"/x.bin", false, 10ull << 20}}, 0, p, error));
    MemorySink sink;
    std::vector<std::uint8_t> buffer;
    int calls = 0;
    const bool ok = write_vsd_image(p, sink, read_pattern, [&calls](std::uint64_t, const std::string&) {
        return ++calls < 3;
    }, buffer, error);
    EXPECT_FALSE(ok);
    EXPECT_EQ(error, std::string("stopped"));
    EXPECT_TRUE(sink.bytes.size() < p.image_bytes);
}

static void test_gct_folders() {
    const std::string text = std::string("\x00\xC6\x1C\x00/Project+/pf/sound/\x00\x00", 22) + "sd:/rex_/x.bin" +
                             std::string("\x00", 1) + "/project+/rp/" + std::string("\x01", 1) + "/../" + "a/b/c" +
                             std::string("\x00", 1) + "/ex_remix/";
    const std::vector<std::uint8_t> gct(text.begin(), text.end());
    const std::vector<std::string> f = vsd_gct_folders(gct);
    EXPECT_EQ(f.size(), std::size_t(3));
    if (f.size() == 3) {
        EXPECT_EQ(f[0], std::string("Project+"));
        EXPECT_EQ(f[1], std::string("rex_"));
        EXPECT_EQ(f[2], std::string("ex_remix"));
    }
}

int main() {
    test_gct_folders();
    test_short_names();
    test_small_card();
    test_big_folder();
    test_similar_names();
    test_plans();
    test_progress_stops();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "vsdbuild tests passed" << std::endl;
    return 0;
}
