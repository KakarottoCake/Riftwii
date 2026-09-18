// SPDX-License-Identifier: GPL-3.0-or-later
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
#include <sys/stat.h>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

// In-memory provider: disc/sd contents keyed by the exact planner paths.
// `disc_status` injects a failure classification for a disc path.
class MapProvider final : public riftwii::ContentProvider {
public:
    std::map<std::string, std::vector<std::uint8_t>> disc;
    std::map<std::string, std::vector<std::uint8_t>> ext;
    std::map<std::string, riftwii::OpenStatus> disc_status;
    riftwii::OpenStatus open_disc(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                                  std::string& error) override {
        auto st = disc_status.find(p);
        if (st != disc_status.end()) { error = "injected failure"; return st->second; }
        auto it = disc.find(p);
        if (it == disc.end()) { error = "no such disc file"; return riftwii::OpenStatus::NotFound; }
        out.reset(new riftwii::MemorySource(it->second));
        return riftwii::OpenStatus::Ok;
    }
    riftwii::OpenStatus open_external(const std::string& p, std::unique_ptr<riftwii::ByteSource>& out,
                                      std::string& error) override {
        auto it = ext.find(p);
        if (it == ext.end()) { error = "no such external file"; return riftwii::OpenStatus::NotFound; }
        out.reset(new riftwii::MemorySource(it->second));
        return riftwii::OpenStatus::Ok;
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

// create="true" only covers genuine absence; an unreadable or oversized disc
// file must fail instead of being silently replaced by an empty original.
static void test_create_does_not_mask_io_errors() {
    for (riftwii::OpenStatus st : {riftwii::OpenStatus::IoError, riftwii::OpenStatus::TooLarge,
                                   riftwii::OpenStatus::Invalid}) {
        MapProvider p;
        p.ext["/r/m.bin"] = Seq(0xA0, 6);
        p.disc_status["/x.bin"] = st;
        auto patch = MakePatch("/x.bin", "/r/m.bin", 0, 0, 0, true, true);
        std::unique_ptr<riftwii::AppliedFile> out;
        std::string err;
        EXPECT_FALSE(riftwii::build_replacement(patch, p, out, err));
        EXPECT_EQ(err, std::string("injected failure"));
        EXPECT_TRUE(out == nullptr);
    }
    // A provider that claims success without a source is an error, not a crash.
    struct LiarProvider final : riftwii::ContentProvider {
        riftwii::OpenStatus open_disc(const std::string&, std::unique_ptr<riftwii::ByteSource>&,
                                      std::string&) override { return riftwii::OpenStatus::Ok; }
        riftwii::OpenStatus open_external(const std::string&, std::unique_ptr<riftwii::ByteSource>&,
                                          std::string&) override { return riftwii::OpenStatus::Ok; }
    } liar;
    auto patch = MakePatch("/a.bin", "/r/m.bin", 0, 0, 0, true, false);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_FALSE(riftwii::build_replacement(patch, liar, out, err));
    EXPECT_FALSE(err.empty());
}

// Two patches on one file: the second sees the bytes the first produced and
// its resize applies to the composed result. Consumed as one final file, in
// split reads.
static void test_compose_two_patches_same_file() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 32);
    p.ext["/r/a.bin"] = Seq(0xA0, 8);
    p.ext["/r/b.bin"] = Seq(0xB0, 8);
    std::vector<riftwii::FilePatch> patches;
    patches.push_back(MakePatch("/a.bin", "/r/a.bin", 8, 8, 0, false, false));   // [8,16) <- A0..
    patches.push_back(MakePatch("/a.bin", "/r/b.bin", 12, 8, 0, true, false));   // [12,20) <- B0.., truncate to 20
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::apply_patches(patches, p, out, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(out->size(), std::uint64_t(20));
    std::vector<std::uint8_t> want = Seq(0x00, 20);
    for (int i = 0; i < 4; ++i) want[8 + i] = static_cast<std::uint8_t>(0xA0 + i);
    for (int i = 0; i < 8; ++i) want[12 + i] = static_cast<std::uint8_t>(0xB0 + i);
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == want);
    // Split reads across the layer boundaries.
    std::vector<std::uint8_t> head(11, 0xFF), tail(9, 0xFF);
    EXPECT_TRUE(out->read(0, head.data(), head.size()));
    EXPECT_TRUE(out->read(11, tail.data(), tail.size()));
    head.insert(head.end(), tail.begin(), tail.end());
    EXPECT_TRUE(head == want);
    // Past-the-end reads fail like a plain source.
    std::uint8_t one = 0;
    EXPECT_FALSE(out->read(20, &one, 1));
}

static void test_compose_create_then_patch() {
    MapProvider p;
    p.ext["/r/new.bin"] = Seq(0x10, 16);
    p.ext["/r/fix.bin"] = Seq(0xF0, 4);
    std::vector<riftwii::FilePatch> patches;
    patches.push_back(MakePatch("/new.bin", "/r/new.bin", 0, 0, 0, true, true));
    patches.push_back(MakePatch("/new.bin", "/r/fix.bin", 4, 4, 0, false, false));
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    EXPECT_TRUE(riftwii::apply_patches(patches, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(16));
    std::vector<std::uint8_t> want = Seq(0x10, 16);
    for (int i = 0; i < 4; ++i) want[4 + i] = static_cast<std::uint8_t>(0xF0 + i);
    std::vector<std::uint8_t> got;
    EXPECT_TRUE(ReadAll(*out, got));
    EXPECT_TRUE(got == want);
    // Three layers: a final resize=true extension past everything zero-fills.
    patches.push_back(MakePatch("/new.bin", "/r/fix.bin", 20, 4, 0, true, false));
    EXPECT_TRUE(riftwii::apply_patches(patches, p, out, err));
    EXPECT_EQ(out->size(), std::uint64_t(24));
    EXPECT_TRUE(ReadAll(*out, got));
    want.resize(24, 0);
    for (int i = 0; i < 4; ++i) want[20 + i] = static_cast<std::uint8_t>(0xF0 + i);
    EXPECT_TRUE(got == want);
}

static void test_compose_failures_leave_output_untouched() {
    MapProvider p;
    p.disc["/a.bin"] = Seq(0x00, 8);
    p.ext["/r/ok.bin"] = Seq(0xA0, 8);
    std::unique_ptr<riftwii::AppliedFile> out;
    std::string err;
    std::vector<riftwii::FilePatch> empty;
    EXPECT_FALSE(riftwii::apply_patches(empty, p, out, err));
    std::vector<riftwii::FilePatch> mixed;
    mixed.push_back(MakePatch("/a.bin", "/r/ok.bin", 0, 0, 0, true, false));
    mixed.push_back(MakePatch("/b.bin", "/r/ok.bin", 0, 0, 0, true, false));
    EXPECT_FALSE(riftwii::apply_patches(mixed, p, out, err));
    EXPECT_TRUE(out == nullptr);
    std::vector<riftwii::FilePatch> second_missing;
    second_missing.push_back(MakePatch("/a.bin", "/r/ok.bin", 0, 0, 0, true, false));
    second_missing.push_back(MakePatch("/a.bin", "/r/gone.bin", 0, 0, 0, true, false));
    EXPECT_FALSE(riftwii::apply_patches(second_missing, p, out, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(out == nullptr);
}

// The size cap must hold for files whose size does not fit in a 32-bit long
// (4 GiB + 100 bytes used to read back as "100 bytes" via ftell). Uses a
// sparse file; skipped when the filesystem would really allocate 4 GiB.
static void test_file_source_rejects_huge_file_without_wrap() {
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "riftwii_huge_file_test";
    std::error_code ec;
    fs::create_directories(base, ec);
    fs::path huge = base / "huge.bin";
    const std::uint64_t target = (4ULL << 30) + 100;
    std::FILE* f = std::fopen(huge.generic_string().c_str(), "wb");
    EXPECT_TRUE(f != nullptr);
    if (f == nullptr) return;
    std::fclose(f);
    fs::resize_file(huge, target, ec);
    const bool created = !ec;
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    bool sparse = created && ::stat(huge.generic_string().c_str(), &st) == 0 &&
                  static_cast<std::uint64_t>(st.st_size) == target &&
                  static_cast<std::uint64_t>(st.st_blocks) * 512ULL < (target / 2);
    if (!sparse) {
        std::cout << "  (skipping huge-file check: sparse files unavailable here)" << std::endl;
        fs::remove_all(base, ec);
        return;
    }
    std::unique_ptr<riftwii::FileByteSource> src;
    std::string err;
    EXPECT_TRUE(riftwii::FileByteSource::open(huge.generic_string(), src, err) == riftwii::OpenStatus::TooLarge);
    EXPECT_TRUE(src == nullptr);
    fs::remove_all(base, ec);
}

static void test_file_source_status_classification() {
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "riftwii_status_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "dir", ec);
    std::unique_ptr<riftwii::FileByteSource> src;
    std::string err;
    EXPECT_TRUE(riftwii::FileByteSource::open((base / "missing.bin").generic_string(), src, err) == riftwii::OpenStatus::NotFound);
    EXPECT_TRUE(riftwii::FileByteSource::open((base / "dir").generic_string(), src, err) == riftwii::OpenStatus::Invalid);
    EXPECT_TRUE(riftwii::FileByteSource::open(std::string(), src, err) == riftwii::OpenStatus::Invalid);
    EXPECT_TRUE(riftwii::FileByteSource::open(std::string("a\0b", 3), src, err) == riftwii::OpenStatus::Invalid);
    riftwii::DirectoryProvider provider(base.generic_string(), base.generic_string());
    std::unique_ptr<riftwii::ByteSource> bs;
    EXPECT_TRUE(provider.open_disc("/missing.bin", bs, err) == riftwii::OpenStatus::NotFound);
    EXPECT_TRUE(provider.open_disc("relative", bs, err) == riftwii::OpenStatus::Invalid);
    EXPECT_TRUE(provider.open_external("/dir", bs, err) == riftwii::OpenStatus::Invalid);
    fs::remove_all(base, ec);
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
    test_create_does_not_mask_io_errors();
    test_compose_two_patches_same_file();
    test_compose_create_then_patch();
    test_compose_failures_leave_output_untouched();
    test_file_source_rejects_huge_file_without_wrap();
    test_file_source_status_classification();
    test_e2e_disc_to_consumed_replacement();
    if (g_failures == 0) {
        std::cout << "ALL APPLY TESTS PASSED" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
        return 1;
    }
}
