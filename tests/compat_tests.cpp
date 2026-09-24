// SPDX-License-Identifier: GPL-3.0-or-later
// Format-compatibility suite: parses the self-authored fixtures under
// tests/fixtures (every construct documented in the public patch-format
// wiki) and checks accept/reject decisions, exact warnings, and planner
// resolution. Run with the fixture directory as argv[1].
#include "riftwii/patch.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static std::string g_dir;

static std::string Load(const std::string& name) {
    std::ifstream in(g_dir + "/" + name, std::ios::binary);
    if (!in) {
        std::cerr << "FAILED: cannot open fixture " << g_dir << "/" << name << std::endl;
        g_failures++;
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static riftwii::PlanOptions AllowAll() {
    riftwii::PlanOptions o;
    o.allow_folders = true;
    o.allow_memory = true;
    o.allow_savegames = true;
    o.allow_filename_targets = true;
    return o;
}

static void test_full_featured() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(Load("full_featured.xml"), pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(pkg.warnings.empty());
    for (const auto& w : pkg.warnings) std::cerr << "  unexpected warning: " << w << std::endl;
    EXPECT_EQ(pkg.root, std::string("/riftwii-fixture"));
    EXPECT_TRUE(pkg.shift_files);
    EXPECT_EQ(pkg.filter.game, std::string("RFT"));
    EXPECT_EQ(pkg.filter.developer, std::string("01"));
    EXPECT_EQ(pkg.filter.number, 0);
    EXPECT_EQ(pkg.filter.revision, 0);
    EXPECT_EQ(pkg.filter.regions.size(), std::size_t(2));

    // The two macros' clones take the place of the option they copy (a
    // template, as in Riivolution), then the two authored options.
    EXPECT_EQ(pkg.options.size(), std::size_t(4));
    if (pkg.options.size() == 4) {
        EXPECT_EQ(pkg.options[0].name, std::string("Level pack (classic)"));
        EXPECT_EQ(pkg.options[0].id, std::string("packLevel pack (classic)"));
        EXPECT_EQ(pkg.options[0].config_id, std::string("packLevel pack (classic)"));
        EXPECT_EQ(pkg.options[0].selected, std::size_t(1));
        EXPECT_EQ(pkg.options[0].choices.size(), std::size_t(2));
        EXPECT_EQ(pkg.options[0].choices[0].params.size(), std::size_t(1));
        EXPECT_EQ(pkg.options[0].params.size(), std::size_t(0));
        EXPECT_EQ(pkg.options[1].name, std::string("Level pack (mirror)"));
        EXPECT_EQ(pkg.options[1].section, std::string("Levels"));
        EXPECT_EQ(pkg.options[1].choices.size(), std::size_t(2));
        EXPECT_EQ(pkg.options[1].id, std::string("packLevel pack (mirror)"));
        if (pkg.options[1].choices.size() == 2) {
            EXPECT_EQ(pkg.options[1].choices[0].params.size(), std::size_t(1));
        }
        // The macro's param is an option param: it wins over the choice's.
        EXPECT_EQ(pkg.options[1].params.size(), std::size_t(1));
        if (pkg.options[1].params.size() == 1) {
            EXPECT_EQ(pkg.options[1].params[0].value, std::string("mirror"));
        }
        EXPECT_EQ(pkg.options[2].name, std::string("Code patches"));
        EXPECT_EQ(pkg.options[2].config_id, std::string("ExtrasCode patches"));  // section name + option name
        EXPECT_EQ(pkg.options[3].name, std::string("Separate save"));
        EXPECT_EQ(pkg.options[3].selected, std::size_t(0));
    }

    EXPECT_EQ(pkg.patches.size(), std::size_t(3));
    const auto& levels = pkg.patches.at("levels");
    EXPECT_EQ(levels.root, std::string("/levels"));
    EXPECT_EQ(levels.folders.size(), std::size_t(1));
    EXPECT_EQ(levels.files.size(), std::size_t(1));
    EXPECT_EQ(levels.order.size(), std::size_t(2));
    if (levels.order.size() == 2) {
        EXPECT_TRUE(levels.order[0].kind == riftwii::PatchKind::Folder);
        EXPECT_TRUE(levels.order[1].kind == riftwii::PatchKind::File);
    }
    if (!levels.folders.empty()) {
        EXPECT_EQ(levels.folders[0].disc, std::string("/Stage"));
        EXPECT_TRUE(levels.folders[0].recursive);
        EXPECT_TRUE(levels.folders[0].create);
        EXPECT_TRUE(levels.folders[0].resize);
        EXPECT_FALSE(levels.folders[0].is_name);
    }
    if (!levels.files.empty()) {
        EXPECT_EQ(levels.files[0].offset, std::uint64_t(0x20));
        EXPECT_EQ(levels.files[0].length, std::uint64_t(0x100));
        EXPECT_FALSE(levels.files[0].resize);
    }

    const auto& codes = pkg.patches.at("codes");
    EXPECT_EQ(codes.files.size(), std::size_t(1));
    EXPECT_EQ(codes.memory.size(), std::size_t(4));
    EXPECT_EQ(codes.order.size(), std::size_t(5));
    if (codes.memory.size() == 4) {
        EXPECT_TRUE(codes.memory[0].has_offset);
        EXPECT_EQ(codes.memory[0].offset, std::uint64_t(0x80001800));
        EXPECT_TRUE(codes.memory[0].value == std::vector<std::uint8_t>({0x4E, 0x80, 0x00, 0x20}));
        EXPECT_EQ(codes.memory[1].valuefile, std::string("/codes/blob.bin"));
        EXPECT_TRUE(codes.memory[1].value.empty());
        EXPECT_TRUE(codes.memory[1].original == std::vector<std::uint8_t>(4, 0));
        EXPECT_TRUE(codes.memory[2].ocarina);
        EXPECT_FALSE(codes.memory[2].search);
        EXPECT_TRUE(codes.memory[3].search);
        EXPECT_FALSE(codes.memory[3].has_offset);
        EXPECT_EQ(codes.memory[3].align, std::uint64_t(4));
        EXPECT_TRUE(codes.memory[3].original == std::vector<std::uint8_t>({0x38, 0x60, 0x00, 0x01}));
    }

    const auto& save = pkg.patches.at("save");
    EXPECT_EQ(save.savegames.size(), std::size_t(1));
    if (!save.savegames.empty()) {
        EXPECT_EQ(save.savegames[0].external, std::string("/save/{$__gameid}"));
        EXPECT_TRUE(save.savegames[0].clone);
    }

    // Default planner: folders and memory are selected but not runtime
    // supported, so launching is refused with every offender named.
    const riftwii::DiscIdentity disc{"RFTE01", 0, 0};
    riftwii::Plan plan;
    EXPECT_FALSE(riftwii::plan_package(pkg, disc, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(Contains(err, "cannot launch: "));
    EXPECT_TRUE(Contains(err, "option 'Level pack (classic)' choice 'Pack A' patch 'levels': <folder> patches are not supported yet"));
    EXPECT_TRUE(Contains(err, "option 'Level pack (mirror)' choice 'Pack A' patch 'levels': <folder> patches are not supported yet"));
    EXPECT_TRUE(Contains(err, "option 'Code patches' choice 'On' patch 'codes': <memory> patches are not supported yet"));
    EXPECT_FALSE(Contains(err, "<savegame>"));  // not selected
    EXPECT_TRUE(plan.files.empty());            // output untouched on failure

    // Everything allowed: paths substitute and resolve per selection.
    EXPECT_TRUE(riftwii::plan_package(pkg, disc, AllowAll(), plan, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(plan.files.size(), std::size_t(3));
    EXPECT_EQ(plan.folders.size(), std::size_t(2));
    EXPECT_EQ(plan.memory.size(), std::size_t(4));
    EXPECT_EQ(plan.savegames.size(), std::size_t(0));
    EXPECT_EQ(plan.order.size(), std::size_t(9));
    if (plan.files.size() == 3 && plan.folders.size() == 2 && plan.memory.size() == 4) {
        EXPECT_EQ(plan.folders[0].external, std::string("/levels/a/Stage"));
        EXPECT_EQ(plan.files[0].disc, std::string("/Stage/intro.arc"));
        EXPECT_EQ(plan.files[0].external, std::string("/levels/a/intro.arc"));
        EXPECT_EQ(plan.folders[1].external, std::string("/levels/mirror/Stage"));  // macro param overrides
        EXPECT_EQ(plan.files[1].external, std::string("/levels/mirror/intro.arc"));
        EXPECT_EQ(plan.files[2].disc, std::string("/main.dol"));
        EXPECT_EQ(plan.files[2].external, std::string("/codes/main.dol"));
        EXPECT_EQ(plan.memory[1].valuefile, std::string("/codes/blob.bin"));
    }
    if (plan.order.size() == 9) {
        EXPECT_TRUE(plan.order[0].kind == riftwii::PatchKind::Folder);
        EXPECT_TRUE(plan.order[1].kind == riftwii::PatchKind::File);
        EXPECT_TRUE(plan.order[2].kind == riftwii::PatchKind::Folder);
        EXPECT_TRUE(plan.order[3].kind == riftwii::PatchKind::File);
        EXPECT_TRUE(plan.order[4].kind == riftwii::PatchKind::File);
        EXPECT_TRUE(plan.order[5].kind == riftwii::PatchKind::Memory);
    }

    // Selecting the save option resolves the built-in placeholder.
    riftwii::Package with_save = pkg;
    with_save.options[3].selected = 1;
    EXPECT_TRUE(riftwii::plan_package(with_save, disc, AllowAll(), plan, err));
    EXPECT_EQ(plan.savegames.size(), std::size_t(1));
    // Absolute externals replace the root, so the fixture's "/save/..." stays absolute.
    if (!plan.savegames.empty()) EXPECT_EQ(plan.savegames[0].external, std::string("/save/RFT"));
    EXPECT_FALSE(riftwii::plan_package(with_save, disc, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(Contains(err, "option 'Separate save' choice 'On' patch 'save': <savegame> redirection is not supported yet"));

    // A disc the filter does not match yields an empty plan, not an error.
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"XXXX01", 0, 0}, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(plan.files.empty() && plan.folders.empty() && plan.memory.empty());
    // Region J is not listed; revision 1 is not the filtered 0.
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"RFTJ01", 0, 0}, AllowAll(), plan, err));
    EXPECT_TRUE(plan.files.empty());
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"RFTE01", 1, 0}, AllowAll(), plan, err));
    EXPECT_TRUE(plan.files.empty());
}

static void test_unknown_extras() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(Load("unknown_extras.xml"), pkg, err));
    EXPECT_TRUE(err.empty());
    const std::vector<std::string> want = {
        "wiidisc: ignoring unknown attribute 'foo'",
        "id: ignoring unknown attribute 'hidden'",
        "network: ignoring protocol 'riiv' (only riifs is supported)",
        "section: ignoring unknown attribute 'icon'",
        "option: ignoring unknown attribute 'hidden'",
        "choice: ignoring unknown attribute 'tip'",
        "choice 'C': ignoring unsupported element 'extra'",
        "file: ignoring unknown attribute 'target'",
        "memory: ignoring unknown attribute 'target'",
        "patch 'p': ignoring unsupported element 'unknownop'",
        "wiidisc: ignoring unsupported element 'trailer'",
    };
    EXPECT_EQ(pkg.warnings.size(), want.size());
    for (std::size_t i = 0; i < pkg.warnings.size() && i < want.size(); ++i) {
        if (pkg.warnings[i] != want[i]) {
            std::cerr << "FAILED: warning " << i << " is '" << pkg.warnings[i] << "', want '" << want[i] << "'" << std::endl;
            g_failures++;
        }
    }
    // The recognised content still parsed.
    EXPECT_EQ(pkg.options.size(), std::size_t(1));
    EXPECT_EQ(pkg.patches.at("p").files.size(), std::size_t(1));
    EXPECT_EQ(pkg.patches.at("p").memory.size(), std::size_t(1));
    riftwii::Plan plan;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"RFTE01", 0, 0}, AllowAll(), plan, err));
    EXPECT_EQ(plan.files.size(), std::size_t(1));
}

static void test_builtin_macros() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(Load("builtin_macros.xml"), pkg, err));
    EXPECT_TRUE(pkg.warnings.empty());
    riftwii::Plan plan;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"RFTE01", 0, 0}, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(plan.files.size(), std::size_t(1));
    if (!plan.files.empty()) {
        EXPECT_EQ(plan.files[0].disc, std::string("/RFT/data.bin"));
        EXPECT_FALSE(plan.files[0].is_filename);
        EXPECT_EQ(plan.files[0].external, std::string("/RFTE/01/data.bin"));
    }
    // Direct API checks.
    std::string out;
    std::vector<riftwii::Param> params{{"a", "1"}, {"a", "2"}};
    EXPECT_TRUE(riftwii::substitute_params("x{$a}y{$__region}", params, riftwii::DiscIdentity{"RFTP01", 0, 0}, out, err));
    EXPECT_EQ(out, std::string("x2yP"));
    EXPECT_FALSE(riftwii::substitute_params("{$a", params, riftwii::DiscIdentity{"RFTP01", 0, 0}, out, err));
    EXPECT_FALSE(riftwii::substitute_params("{$}", params, riftwii::DiscIdentity{"RFTP01", 0, 0}, out, err));
    // An unknown name becomes empty, as in Riivolution.
    EXPECT_TRUE(riftwii::substitute_params("a{$zzz}b", params, riftwii::DiscIdentity{"RFTP01", 0, 0}, out, err));
    EXPECT_EQ(out, std::string("ab"));
    riftwii::DiscIdentity console{"RFTP01", 0, 0};
    console.console_id = 0x0403AC68;
    EXPECT_TRUE(riftwii::substitute_params("/saves/{$__ngid}", params, console, out, err));
    EXPECT_EQ(out, std::string("/saves/0403AC68"));
    EXPECT_TRUE(riftwii::substitute_params("plain/{x}/$y", params, riftwii::DiscIdentity{"RFTP01", 0, 0}, out, err));
    EXPECT_EQ(out, std::string("plain/{x}/$y"));
}

static void test_filename_target() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(Load("filename_target.xml"), pkg, err));
    EXPECT_TRUE(pkg.patches.at("p").files[0].is_filename);
    riftwii::Plan plan;
    EXPECT_FALSE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(Contains(err, "<file disc=\"anywhere.arc\"> file-name lookup is not supported yet"));
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, AllowAll(), plan, err));
    EXPECT_EQ(plan.files.size(), std::size_t(1));
    if (!plan.files.empty()) {
        EXPECT_EQ(plan.files[0].disc, std::string("anywhere.arc"));
        EXPECT_TRUE(plan.files[0].is_filename);
        EXPECT_EQ(plan.files[0].external, std::string("/riivolution/anywhere.arc"));
    }
}

static void test_params_override() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(Load("params_override.xml"), pkg, err));
    EXPECT_TRUE(pkg.warnings.empty());
    EXPECT_EQ(pkg.options.size(), std::size_t(1));  // the clone replaces its template
    riftwii::Plan plan;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(plan.files.size(), std::size_t(1));
    if (plan.files.size() == 1) {
        // As in Riivolution, an option param wins over the choice's and
        // over a macro's; the patch root is substituted too.
        EXPECT_EQ(plan.files[0].external, std::string("/base/opt/opt.bin"));
    }
}

static void test_rejections() {
    riftwii::Package pkg;
    std::string err;
    // Documents the parser refuses outright (XML it will not process).
    const char* bad[] = {"bad_doctype.xml", "bad_pi.xml"};
    for (const char* name : bad) {
        riftwii::Package out;
        std::string e;
        if (riftwii::parse_package(Load(name), out, e)) {
            std::cerr << "FAILED: " << name << " was accepted" << std::endl;
            g_failures++;
        } else if (e.empty()) {
            std::cerr << "FAILED: " << name << " rejected without a message" << std::endl;
            g_failures++;
        }
    }
    // Malformed elements in otherwise valid documents are dropped with a
    // warning, as Riivolution skips what it cannot read; the rest loads.
    const char* dropped[] = {"bad_hex.xml", "bad_memory_both.xml", "bad_macro_ref.xml",
                             "bad_disc_relative_dir.xml", "bad_traversal.xml"};
    for (const char* name : dropped) {
        riftwii::Package out;
        std::string e;
        if (!riftwii::parse_package(Load(name), out, e)) {
            std::cerr << "FAILED: " << name << " was refused: " << e << std::endl;
            g_failures++;
        } else if (out.warnings.empty()) {
            std::cerr << "FAILED: " << name << " dropped something without a warning" << std::endl;
            g_failures++;
        }
    }
    EXPECT_TRUE(riftwii::parse_package(Load("plan_unknown_param.xml"), pkg, err));
    riftwii::Plan plan;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, riftwii::PlanOptions{}, plan, err));
}

static void test_read_package_from_file() {
    std::ifstream in(g_dir + "/full_featured.xml", std::ios::binary);
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::read_package(in, pkg, err));
    EXPECT_EQ(pkg.patches.size(), std::size_t(3));
}

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : "tests/fixtures";
    test_full_featured();
    test_unknown_extras();
    test_builtin_macros();
    test_filename_target();
    test_params_override();
    test_rejections();
    test_read_package_from_file();
    if (g_failures == 0) {
        std::cout << "ALL COMPAT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
