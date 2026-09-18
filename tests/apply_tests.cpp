#include "riftwii/apply.hpp"
#include "riftwii/source.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

// In-memory provider: disc/sd contents keyed by the exact planner paths.
class MapProvider final : public riftwii::ContentProvider {
public:
    std::map<std::string, std::vector<std::uint8_t>> disc;
    std::map<std::string, std::vector<std::uint8_t>> ext;
    bool open_disc(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                   std::string& error) override {
        auto it = disc.find(p);
        if (it == disc.end()) { error = "no such disc file"; return false; }
        out.reset(new riftwii::MemorySource(it->second));
        return true;
    }
    bool open_external(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                       std::string& error) override {
        auto it = ext.find(p);
        if (it == ext.end()) { error = "no such external file"; return false; }
        out.reset(new riftwii::MemorySource(it->second));
        return true;
    }
};

static riftwii::FilePatch MakePatch(const std::string& disc, const std::string& external,
                                    std::uint64_t offset, std::uint64_t length,
                                    std::uint64_t fileoffset, bool resize, bool create) {
    riftwii::FilePatch f;
    f.disc = disc;
    f.external = external;
    f.offset = offset;
    f.length = length;
    f.file_offset = fileoffset;
    f.resize = resize;
    f.create = create;
    return f;
}

static std::vector<std::uint8_t> Seq(std::uint8_t base, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(base + i);
    return v;
}

static bool ReadAll(const riftwii::AppliedFile& a, std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(a.size()), 0);
    if (a.size() > 64 * 1024 * 1024) return false;  // unit tests stay small.
    return a.read(0, out.data(), static_cast<std::size_t>(a.size()));
}

static void test_full_replace() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 16);
    p.ext["/r/b.bin"] = Seq(0xA0, 16);
    auto patch = MakePatch("/a.bin", "/r/b.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(out->size(), std::uint64_t(16));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == Seq(0xA0, 16));
}

static void test_partial_no_resize_keeps_tail() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 64);
    p.ext["/r/m.bin"] = Seq(0xA0, 16);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 8, 16, 0, false, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(64));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    std::vector<std::uint8_t> want = Seq(0x00, 64);
    std::vector<std::uint8_t> mod = Seq(0xA0, 16);
    std::copy(mod.begin(), mod.end(), want.begin() + 8);
    EXPECT_TRUE(got == want);
}

static void test_resize_truncate() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 64);
    p.ext["/r/m.bin"] = Seq(0xA0, 8);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(8));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == Seq(0xA0, 8));
}

static void test_resize_extend() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 10);
    p.ext["/r/m.bin"] = Seq(0xA0, 20);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(20));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == Seq(0xA0, 20));
}

static void test_short_external_zero_pads_inside_original() {
    MapProvider p;
    std::vector<std::uint8_t> orig(10, 0x41);  // "AAAAAAAAAA"
    p.disc["/a.bin"] = orig;
    p.ext["/r/m.bin"] = std::vector<std::uint8_t>{0x58, 0x59};  // "XY"
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 6, 0, false, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(10));  // resize=false keeps size.
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    // [0,2) external, [2,6) zeroes (NOT original), [6,10) original tail.
    std::vector<std::uint8_t> want{0x58, 0x59, 0, 0, 0, 0, 0x41, 0x41, 0x41, 0x41};
    EXPECT_TRUE(got == want);
}

static void test_fileoffset() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    p.ext["/r/m.bin"] = Seq(0x30, 10);  // 0x30..0x39
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 4, 4, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(4));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    std::vector<std::uint8_t> want{0x34, 0x35, 0x36, 0x37};
    EXPECT_TRUE(got == want);
}

static void test_fileoffset_past_end_zero_fills() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    p.ext["/r/m.bin"] = Seq(0x30, 10);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 4, 100, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(4));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == std::vector<std::uint8_t>(4, 0));
}

static void test_length_zero_uses_external_rest() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    p.ext["/r/m.bin"] = Seq(0x30, 10);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 0, 6, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(4));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    std::vector<std::uint8_t> want{0x36, 0x37, 0x38, 0x39};
    EXPECT_TRUE(got == want);
}

static void test_gap_past_eof() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 4);
    p.ext["/r/m.bin"] = Seq(0xA0, 4);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 8, 4, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(12));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    std::vector<std::uint8_t> want{0x00, 0x01, 0x02, 0x03, 0, 0, 0, 0, 0xA0, 0xA1, 0xA2, 0xA3};
    EXPECT_TRUE(got == want);
}

static void test_create_missing_disc() {
    MapProvider p;
    p.ext["/r/m.bin"] = Seq(0xA0, 6);
    auto patch = MakePatch("/new.bin", "/r/m.bin", 0, 0, 0, true, true);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(6));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == Seq(0xA0, 6));
}

static void test_no_create_missing_disc_fails() {
    MapProvider p;
    p.ext["/r/m.bin"] = Seq(0xA0, 6);
    auto patch = MakePatch("/new.bin", "/r/m.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_FALSE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(out == nullptr);
}

static void test_missing_external_fails() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    auto patch = MakePatch("/a.bin", "/r/gone.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_FALSE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(out == nullptr);
}

static void test_offset_low_bits_ignored_like_hardware() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 16);
    p.ext["/r/m.bin"] = Seq(0xA0, 4);
    // Offset 5 must behave as 4.
    auto patch = MakePatch("/a.bin", "/r/m.bin", 5, 4, 0, false, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::build_replacement(patch, p, out, err));
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    std::vector<std::uint8_t> want = Seq(0x00, 16);
    std::vector<std::uint8_t> mod = Seq(0xA0, 4);
    std::copy(mod.begin(), mod.end(), want.begin() + 4);
    EXPECT_TRUE(got == want);
}

static void test_range_overflow_fails() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    p.ext["/r/m.bin"] = Seq(0xA0, 8);
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0xFFFFFFFFFFFFFFFCULL, 8, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_FALSE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(out == nullptr);
}

static void test_output_untouched_on_failure() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    auto patch = MakePatch("/a.bin", "/r/gone.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_FALSE(riftwii::build_replacement(patch, p, out, err));
    EXPECT_TRUE(out == nullptr);
}

static bool WriteFile(const std::filesystem::path& p, const std::vector<std::uint8_t>& data) {
    std::FILE* f = std::fopen(p.generic_string().c_str(), "wb");
    if (f == nullptr) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

// One complete path: real files on disk -> XML text -> parse -> plan ->
// provider -> consumed replacement bytes.
static void test_e2e_disc_to_consumed_replacement() {
    namespace fs = std::filesystem;
    std::string err;
    fs::path base = fs::temp_directory_path() / "riftwii_e2e_apply";
    fs::path disc_root = base / "disc";
    fs::path sd_root = base / "sd";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(disc_root / "DATA", ec);
    fs::create_directories(sd_root / "riivolution", ec);
    EXPECT_TRUE(ec.value() == 0);

    std::vector<std::uint8_t> original = Seq(0x00, 64);
    std::vector<std::uint8_t> mod = Seq(0xA0, 16);
    EXPECT_TRUE(WriteFile(disc_root / "DATA" / "sys.bin", original));
    EXPECT_TRUE(WriteFile(sd_root / "riivolution" / "mod.bin", mod));

    const std::string xml =
        std::string("<wiidisc version=\"1\" root=\"/riivolution\">") +
        "<options><section name=\"Mods\">"
        "<option name=\"Mod\" default=\"1\">"
        "<choice name=\"On\"><patch id=\"p1\"/></choice>"
        "</option></section></options>"
        "<patch id=\"p1\"><file disc=\"/DATA/sys.bin\" external=\"mod.bin\""
        " offset=\"8\" length=\"16\" resize=\"false\" create=\"false\"/></patch>"
        "</wiidisc>";

    riftwii::Package pkg;
    EXPECT_TRUE(riftwii::parse_package(xml, pkg, err));
    riftwii::Plan plan;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, riftwii::PlanOptions{}, plan, err));
    const std::vector<riftwii::FilePatch>& planned = plan.files;
    EXPECT_EQ(planned.size(), std::size_t(1));

    riftwii::DirectoryProvider provider(disc_root.generic_string(), sd_root.generic_string());
    std::unique_ptr<riftwii::AppliedFile> applied;
    EXPECT_TRUE(riftwii::build_replacement(planned[0], provider, applied, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(applied->size(), std::uint64_t(64));

    // Consume in two split reads to prove the view streams, not just dumps.
    std::vector<std::uint8_t> first(10, 0xFF), rest(54, 0xFF);
    EXPECT_TRUE(applied->read(0, first.data(), first.size()));
    EXPECT_TRUE(applied->read(10, rest.data(), rest.size()));
    std::vector<std::uint8_t> got = first;
    got.insert(got.end(), rest.begin(), rest.end());
    std::vector<std::uint8_t> want = original;
    std::copy(mod.begin(), mod.end(), want.begin() + 8);
    EXPECT_TRUE(got == want);

    fs::remove_all(base, ec);
}

int main() {
    test_full_replace();
    test_partial_no_resize_keeps_tail();
    test_resize_truncate();
    test_resize_extend();
    test_short_external_zero_pads_inside_original();
    test_fileoffset();
    test_fileoffset_past_end_zero_fills();
    test_length_zero_uses_external_rest();
    test_gap_past_eof();
    test_create_missing_disc();
    test_no_create_missing_disc_fails();
    test_missing_external_fails();
    test_offset_low_bits_ignored_like_hardware();
    test_range_overflow_fails();
    test_output_untouched_on_failure();
    test_e2e_disc_to_consumed_replacement();
    if (g_failures == 0) {
        std::cout << "ALL APPLY TESTS PASSED" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
        return 1;
    }
}
