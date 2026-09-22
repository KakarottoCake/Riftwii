// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/expand.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

// A provider that only lists: the expansion never opens anything.
class ListingProvider final : public riftwii::ContentProvider {
public:
    std::map<std::string, std::vector<riftwii::ExternalEntry>> dirs;
    riftwii::OpenStatus open_disc(const std::string&, std::unique_ptr<riftwii::ByteSource>&, std::string& error) override {
        error = "not expected";
        return riftwii::OpenStatus::IoError;
    }
    riftwii::OpenStatus open_external(const std::string&, std::unique_ptr<riftwii::ByteSource>&, std::string& error) override {
        error = "not expected";
        return riftwii::OpenStatus::IoError;
    }
    bool list_external(const std::string& sd_dir, std::vector<riftwii::ExternalEntry>& out, std::string& error) override {
        auto it = dirs.find(sd_dir);
        if (it == dirs.end()) {
            error = "no such directory";
            return false;
        }
        out = it->second;
        return true;
    }
    void add(const std::string& dir, const std::string& name, bool is_directory) {
        riftwii::ExternalEntry e;
        e.name = name;
        e.is_directory = is_directory;
        dirs[dir].push_back(e);
    }
};

// /Stage/a.arc /Stage/b.arc /Stage/sub/c.bin /sys.bin /Boot/x.bin /Boot/a.arc
static riftwii::Fst Disc() {
    std::vector<std::uint8_t> root(12, 0);
    root[0] = 1;
    root[11] = 1;  // one entry: the root
    riftwii::Fst fst;
    std::string err;
    if (!riftwii::Fst::parse(root.data(), root.size(), true, fst, err)) std::abort();
    std::uint32_t idx = 0;
    if (!fst.add_directory(0, "Stage", idx, err)) std::abort();
    const std::uint32_t stage = idx;
    if (!fst.add_file(stage, "a.arc", 0x1000, 0x40, idx, err)) std::abort();
    if (!fst.add_file(stage, "b.arc", 0x2000, 0x40, idx, err)) std::abort();
    if (!fst.add_directory(stage, "sub", idx, err)) std::abort();
    if (!fst.add_file(idx, "c.bin", 0x3000, 0x40, idx, err)) std::abort();
    if (!fst.add_file(0, "sys.bin", 0x4000, 0x40, idx, err)) std::abort();
    if (!fst.add_directory(0, "Boot", idx, err)) std::abort();
    const std::uint32_t boot = idx;
    if (!fst.add_file(boot, "x.bin", 0x5000, 0x40, idx, err)) std::abort();
    if (!fst.add_file(boot, "a.arc", 0x6000, 0x40, idx, err)) std::abort();
    return fst;
}

static ListingProvider Card() {
    ListingProvider p;
    // Deliberately unsorted and oddly cased.
    p.add("/mod/Stage", "sub", true);
    p.add("/mod/Stage", "new.arc", false);
    p.add("/mod/Stage", "B.ARC", false);
    p.add("/mod/Stage", "a.arc", false);
    p.add("/mod/Stage", "extra", true);
    p.add("/mod/Stage/sub", "d.bin", false);
    p.add("/mod/Stage/sub", "C.BIN", false);
    p.add("/mod/Stage/extra", "e.bin", false);
    p.add("/mod/loose", "SYS.BIN", false);
    p.add("/mod/loose", "a.arc", false);
    p.add("/mod/loose", "other.txt", false);
    p.add("/mod/loose", "nested", true);
    p.add("/mod/loose/nested", "x.bin", false);
    return p;
}

static riftwii::FolderPatch Folder(const std::string& disc, const std::string& external, bool recursive, bool create) {
    riftwii::FolderPatch f;
    f.disc = disc;
    f.external = external;
    f.recursive = recursive;
    f.create = create;
    return f;
}

static std::string Describe(const riftwii::FilePatch& f) {
    return f.disc + "<-" + f.external + (f.create ? " create" : "");
}

static void test_rooted() {
    riftwii::Fst fst = Disc();
    ListingProvider card = Card();
    riftwii::Plan plan;
    plan.folders.push_back(Folder("/stage", "/mod/Stage", true, false));
    std::vector<riftwii::FilePatch> out;
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(3));
    if (out.size() == 3) {
        // Name order with case folded; the disc's spelling; "new.arc" and
        // "extra/" (not on the disc) skipped; sub/ walked.
        EXPECT_EQ(Describe(out[0]), std::string("/Stage/a.arc<-/mod/Stage/a.arc"));
        EXPECT_EQ(Describe(out[1]), std::string("/Stage/b.arc<-/mod/Stage/B.ARC"));
        EXPECT_EQ(Describe(out[2]), std::string("/Stage/sub/c.bin<-/mod/Stage/sub/C.BIN"));
        EXPECT_TRUE(out[0].resize);
        EXPECT_EQ(out[0].length, std::uint64_t(0));
        EXPECT_FALSE(out[0].is_filename);
    }
    EXPECT_EQ(notes.size(), std::size_t(1));
    if (!notes.empty()) EXPECT_EQ(notes[0], std::string("<folder /mod/Stage -> /stage>: 3 replaced, 0 created, 3 skipped"));

    // Not recursive: the subfolders are not entered.
    plan.folders[0].recursive = false;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(2));

    // create: the missing file and the missing folder's file are created,
    // with the patch's spelling for what does not exist yet.
    plan.folders[0].recursive = true;
    plan.folders[0].create = true;
    plan.folders[0].resize = false;
    plan.folders[0].length = 0x20;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(6));
    if (out.size() == 6) {
        EXPECT_EQ(Describe(out[0]), std::string("/Stage/a.arc<-/mod/Stage/a.arc"));
        EXPECT_EQ(Describe(out[1]), std::string("/Stage/b.arc<-/mod/Stage/B.ARC"));
        EXPECT_EQ(Describe(out[2]), std::string("/Stage/extra/e.bin<-/mod/Stage/extra/e.bin create"));
        EXPECT_EQ(Describe(out[3]), std::string("/Stage/new.arc<-/mod/Stage/new.arc create"));
        EXPECT_EQ(Describe(out[4]), std::string("/Stage/sub/c.bin<-/mod/Stage/sub/C.BIN"));
        EXPECT_EQ(Describe(out[5]), std::string("/Stage/sub/d.bin<-/mod/Stage/sub/d.bin create"));
        EXPECT_FALSE(out[5].resize);
        EXPECT_EQ(out[5].length, std::uint64_t(0x20));
    }

    // A rooted folder that is not on this disc is skipped without create.
    // Multi-region packages rely on this for their other regions' folders.
    // With create, the complete tree is created.
    plan.folders[0] = Folder("/Missing", "/mod/Stage", true, false);
    notes.clear();
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(notes.size(), std::size_t(1));
    if (!notes.empty()) {
        EXPECT_EQ(notes[0], std::string("<folder /mod/Stage -> /Missing>: 0 replaced, 0 created, 5 skipped"));
    }
    plan.folders[0].create = true;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(6));
    if (out.size() == 6) EXPECT_EQ(Describe(out[0]), std::string("/Missing/a.arc<-/mod/Stage/a.arc create"));

    // A rooted target that is a file, a card folder that cannot be listed.
    plan.folders[0] = Folder("/sys.bin", "/mod/Stage", true, false);
    EXPECT_FALSE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    plan.folders[0] = Folder("/Stage", "/mod/nowhere", true, false);
    EXPECT_FALSE(riftwii::expand_plan(plan, fst, card, out, notes, err));

    // The root itself.
    plan.folders[0] = Folder("/", "/mod/loose", false, false);
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(1));
    if (out.size() == 1) EXPECT_EQ(Describe(out[0]), std::string("/sys.bin<-/mod/loose/SYS.BIN"));

    // Trailing slashes (the parser rejects them on disc paths, but a plan
    // built in code may carry them) and the card's root as the external
    // folder never produce "//".
    plan.folders[0] = Folder("/Stage/", "/mod/Stage/", false, false);
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(2));
    if (out.size() == 2) EXPECT_EQ(Describe(out[1]), std::string("/Stage/b.arc<-/mod/Stage/B.ARC"));
    card.add("/", "sys.bin", false);
    plan.folders[0] = Folder("/", "/", false, false);
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(1));
    if (out.size() == 1) EXPECT_EQ(Describe(out[0]), std::string("/sys.bin<-/sys.bin"));

    // A card tree deeper than the walk allows is refused, not recursed.
    ListingProvider deep;
    std::string dir = "/deep";
    for (int level = 0; level < 40; ++level) {
        deep.add(dir, "d", true);
        dir += "/d";
    }
    deep.add(dir, "leaf.bin", false);
    plan.folders[0] = Folder("/Extra", "/deep", true, true);
    EXPECT_FALSE(riftwii::expand_plan(plan, fst, deep, out, notes, err));
}

static void test_by_name() {
    riftwii::Fst fst = Disc();
    ListingProvider card = Card();
    riftwii::Plan plan;
    plan.folders.push_back(Folder("", "/mod/loose", true, true));
    std::vector<riftwii::FilePatch> out;
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    // a.arc matches two disc files; other.txt none; nested/ is not entered
    // and create does not apply to a search.
    EXPECT_EQ(out.size(), std::size_t(3));
    if (out.size() == 3) {
        EXPECT_EQ(Describe(out[0]), std::string("/Stage/a.arc<-/mod/loose/a.arc"));
        EXPECT_EQ(Describe(out[1]), std::string("/Boot/a.arc<-/mod/loose/a.arc"));
        EXPECT_EQ(Describe(out[2]), std::string("/sys.bin<-/mod/loose/SYS.BIN"));
    }
    if (!notes.empty()) EXPECT_EQ(notes[0], std::string("<folder /mod/loose -> by name>: 3 replaced, 0 created, 1 skipped"));

    // A bare name is a search too.
    plan.folders[0].disc = "loose";
    plan.folders[0].is_name = true;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(3));
}

static void test_order_and_files() {
    riftwii::Fst fst = Disc();
    ListingProvider card = Card();
    riftwii::Plan plan;
    riftwii::FilePatch first;
    first.disc = "/Boot/x.bin";
    first.external = "/mod/x.bin";
    riftwii::FilePatch last;
    last.disc = "sys.bin";
    last.is_filename = true;
    last.external = "/mod/s.bin";
    plan.files.push_back(first);
    plan.files.push_back(last);
    plan.folders.push_back(Folder("/Stage/sub", "/mod/Stage/sub", true, false));
    riftwii::MemoryPatch mem;
    plan.memory.push_back(mem);
    plan.order = {riftwii::PatchStep{riftwii::PatchKind::File, 0}, riftwii::PatchStep{riftwii::PatchKind::Memory, 0},
                  riftwii::PatchStep{riftwii::PatchKind::Folder, 0}, riftwii::PatchStep{riftwii::PatchKind::File, 1}};
    std::vector<riftwii::FilePatch> out;
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(3));
    if (out.size() == 3) {
        EXPECT_EQ(Describe(out[0]), std::string("/Boot/x.bin<-/mod/x.bin"));
        EXPECT_EQ(Describe(out[1]), std::string("/Stage/sub/c.bin<-/mod/Stage/sub/C.BIN"));
        EXPECT_EQ(Describe(out[2]), std::string("sys.bin<-/mod/s.bin"));
        EXPECT_TRUE(out[2].is_filename);
    }

    // A corrupt order fails; an empty order means files then folders.
    plan.order = {riftwii::PatchStep{riftwii::PatchKind::Folder, 5}};
    EXPECT_FALSE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    plan.order.clear();
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(3));
    if (out.size() == 3) EXPECT_EQ(Describe(out[2]), std::string("/Stage/sub/c.bin<-/mod/Stage/sub/C.BIN"));

    // A provider that cannot list.
    class NoList final : public riftwii::ContentProvider {
        riftwii::OpenStatus open_disc(const std::string&, std::unique_ptr<riftwii::ByteSource>&, std::string&) override {
            return riftwii::OpenStatus::IoError;
        }
        riftwii::OpenStatus open_external(const std::string&, std::unique_ptr<riftwii::ByteSource>&, std::string&) override {
            return riftwii::OpenStatus::IoError;
        }
    } nolist;
    EXPECT_FALSE(riftwii::expand_plan(plan, fst, nolist, out, notes, err));
}

// The native listing through DirectoryProvider, on a directory made here.
static void test_native_listing() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "riftwii_expand_test";
    fs::remove_all(root);
    fs::create_directories(root / "sd" / "mod" / "Stage" / "sub");
    std::ofstream(root / "sd" / "mod" / "Stage" / "a.arc") << "a";
    std::ofstream(root / "sd" / "mod" / "Stage" / "sub" / "c.bin") << "c";
    riftwii::DirectoryProvider provider((root / "disc").string(), (root / "sd").string());
    std::vector<riftwii::ExternalEntry> entries;
    std::string err;
    EXPECT_TRUE(provider.list_external("/mod/Stage", entries, err));
    EXPECT_EQ(entries.size(), std::size_t(2));
    bool saw_file = false, saw_dir = false;
    for (const auto& e : entries) {
        if (e.name == "a.arc" && !e.is_directory) saw_file = true;
        if (e.name == "sub" && e.is_directory) saw_dir = true;
    }
    EXPECT_TRUE(saw_file);
    EXPECT_TRUE(saw_dir);
    EXPECT_FALSE(provider.list_external("/mod/nowhere", entries, err));

    riftwii::Fst fst = Disc();
    riftwii::Plan plan;
    plan.folders.push_back(Folder("/Stage", "/mod/Stage", true, false));
    std::vector<riftwii::FilePatch> out;
    std::vector<std::string> notes;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, provider, out, notes, err));
    EXPECT_EQ(out.size(), std::size_t(2));
    if (out.size() == 2) {
        EXPECT_EQ(Describe(out[0]), std::string("/Stage/a.arc<-/mod/Stage/a.arc"));
        EXPECT_EQ(Describe(out[1]), std::string("/Stage/sub/c.bin<-/mod/Stage/sub/c.bin"));
    }
    fs::remove_all(root);
}

static void test_empty_plan_message() {
    riftwii::Fst fst = Disc();
    ListingProvider card = Card();
    // A folder whose files are all skipped (unknown names, create off, and
    // subfolders with recursive off) expands cleanly to nothing.
    riftwii::Plan plan;
    plan.folders.push_back(Folder("/Stage", "/mod/Stage", false, false));
    card.dirs["/mod/Stage"].clear();
    card.add("/mod/Stage", "unknown.bin", false);
    card.add("/mod/Stage", "sub", true);
    std::vector<riftwii::FilePatch> out;
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::expand_plan(plan, fst, card, out, notes, err));
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(notes.size(), std::size_t(1));
    const std::string msg = riftwii::describe_empty_plan("sd:/riivolution/mod.xml", "RZDE01", plan, notes);
    EXPECT_TRUE(msg.find("sd:/riivolution/mod.xml") != std::string::npos);
    EXPECT_TRUE(msg.find("RZDE01") != std::string::npos);
    EXPECT_TRUE(msg.find("0 replaced") != std::string::npos);
    EXPECT_TRUE(msg.find("create=") != std::string::npos);
    // A plan with no steps at all is a different, precise message.
    riftwii::Plan bare;
    const std::string bare_msg =
        riftwii::describe_empty_plan("sd:/riivolution/mod.xml", "RZDE01", bare, std::vector<std::string>());
    EXPECT_TRUE(bare_msg.find("no patches selected") != std::string::npos);
    EXPECT_TRUE(bare_msg.find("RZDE01") != std::string::npos);
}

int main() {
    test_rooted();
    test_by_name();
    test_order_and_files();
    test_native_listing();
    test_empty_plan_message();
    if (g_failures == 0) {
        std::cout << "ALL EXPAND TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
