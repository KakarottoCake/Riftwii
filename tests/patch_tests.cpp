// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/patch.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)
// File-only planning, as the current runtime supports it.
static bool PlanFiles(const riftwii::Package& pkg, const riftwii::DiscIdentity& disc,
                      std::vector<riftwii::FilePatch>& out, std::string& err) {
    riftwii::Plan plan;
    if (!riftwii::plan_package(pkg, disc, riftwii::PlanOptions{}, plan, err)) return false;
    out = plan.files;
    return true;
}
static std::string GoodXml() {
    return std::string("<wiidisc version=\"1\" root=\"/riivolution\">") +
        "<id game=\"RSBE\" developer=\"01\" disc=\"0\" revision=\"0\"><region type=\"E\"/></id>" +
        "<options><section name=\"Mods\">" +
        "<option name=\"Opt1\" id=\"o1\" default=\"1\">" +
        "<choice name=\"A\"><patch id=\"p1\"/><patch id=\"p2\"/></choice>" +
        "<choice name=\"B\"><patch id=\"p2\"/></choice>" +
        "</option>" +
        "<option name=\"Opt2\" default=\"0\"><choice name=\"On\"><patch id=\"p1\"/></choice></option>" +
        "</section></options>" +
        "<patch id=\"p1\" root=\"/p1root\"><file disc=\"/a.bin\" external=\"a.bin\" offset=\"16\" length=\"32\" fileoffset=\"0\" resize=\"true\" create=\"false\"/></patch>" +
        "<patch id=\"p2\"><file disc=\"/b.bin\" external=\"/abs/b.bin\" offset=\"0x10\" length=\"0x20\" fileoffset=\"4\" resize=\"no\" create=\"yes\"/></patch>" +
        "</wiidisc>";
}
static void test_successful() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(pkg.root, std::string("/riivolution"));
    EXPECT_EQ(pkg.filter.game, std::string("RSBE"));
    EXPECT_EQ(pkg.filter.developer, std::string("01"));
    EXPECT_EQ(pkg.options.size(), std::size_t(2));
    EXPECT_EQ(pkg.patches.size(), std::size_t(2));
    EXPECT_EQ(pkg.patches.at("p1").files.size(), std::size_t(1));
    EXPECT_EQ(pkg.patches.at("p2").files[0].offset, std::uint64_t(16));
    riftwii::DiscIdentity disc{"RSBE01", 0, 0};
    std::vector<riftwii::FilePatch> out;
    EXPECT_TRUE(PlanFiles(pkg, disc, out, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(out.size(), std::size_t(2));
    if (out.size() == 2) {
        EXPECT_EQ(out[0].disc, std::string("/a.bin"));
        EXPECT_EQ(out[0].external, std::string("/p1root/a.bin"));
        EXPECT_EQ(out[1].disc, std::string("/b.bin"));
        EXPECT_EQ(out[1].external, std::string("/abs/b.bin"));
    }
}
static void test_hex_overflow() {
    riftwii::Package pkg;
    std::string err;
    std::string xml = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" offset=\"0x10\" length=\"20\"/></patch></wiidisc>";
    EXPECT_TRUE(riftwii::parse_package(xml, pkg, err));
    std::string bad = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" offset=\"18446744073709551616\"/></patch></wiidisc>";
    riftwii::Package before = pkg;
    EXPECT_FALSE(riftwii::parse_package(bad, pkg, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(pkg.patches.size() == before.patches.size());
    std::string bad2 = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" offset=\"0xFFFFFFFFFFFFFFFFFF\"/></patch></wiidisc>";
    EXPECT_FALSE(riftwii::parse_package(bad2, pkg, err));
    std::string bad3 = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" offset=\"-1\"/></patch></wiidisc>";
    EXPECT_FALSE(riftwii::parse_package(bad3, pkg, err));
    std::string okBool = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" resize=\"1\" create=\"0\"/></patch></wiidisc>";
    EXPECT_TRUE(riftwii::parse_package(okBool, pkg, err));
    EXPECT_EQ(pkg.patches.at("p").files[0].resize, true);
    EXPECT_EQ(pkg.patches.at("p").files[0].create, false);
    std::string badBool = "<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a.bin\" external=\"a.bin\" resize=\"maybe\"/></patch></wiidisc>";
    EXPECT_FALSE(riftwii::parse_package(badBool, pkg, err));
}
static void test_unsupported() {
    riftwii::Package pkg;
    std::string err;
    // Unknown elements and attributes are ignored with a warning, never fatal.
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><params/></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.warnings.size(), std::size_t(1));
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><macros/></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.warnings.size(), std::size_t(1));
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\" foo=\"bar\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.warnings.size(), std::size_t(1));
    if (!pkg.warnings.empty()) EXPECT_EQ(pkg.warnings[0], std::string("wiidisc: ignoring unknown attribute 'foo'"));
    // Known elements with malformed content still fail.
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><memory address=\"0\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><savegame/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><folder disc=\"/y\"/></patch></wiidisc>", pkg, err));
    // Documented patch kinds parse into the model.
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><folder external=\"x\" disc=\"/y\"/></patch></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.patches.at("p").folders.size(), std::size_t(1));
    EXPECT_TRUE(pkg.warnings.empty());
}
static void test_duplicates() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"a\" id=\"b\"><file disc=\"/x\" external=\"y\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"dup\"><file disc=\"/a\" external=\"b\"/></patch><patch id=\"dup\"><file disc=\"/c\" external=\"d\"/></patch></wiidisc>", pkg, err));
}
static void test_dtd_pi() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package("<?xml version=\"1.0\"?><wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(riftwii::parse_package("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<!-- c --><wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc><?xml version=\"1.0\"?>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<!DOCTYPE wiidisc><wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><?foo bar?><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc><extra/>", pkg, err));
}
static void test_empty() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><options><section name=\"\"><option name=\"o\" default=\"0\"><choice name=\"c\"/></option></section></options></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\" offset=\"\"/></patch></wiidisc>", pkg, err));
}
static void test_refs_choices() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><options><section name=\"s\"><option name=\"o\" default=\"1\"><choice name=\"c\"><patch id=\"missing\"/></choice></option></section></options><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><options><section name=\"s\"><option name=\"o\" default=\"5\"><choice name=\"c\"/><choice name=\"d\"/></option></section></options></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.options[0].selected, std::size_t(0));
    EXPECT_EQ(pkg.warnings.size(), std::size_t(1));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><options><section name=\"s\"><option name=\"o\" default=\"yes\"><choice name=\"c\"/></option></section></options></wiidisc>", pkg, err));
    riftwii::Package ok;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), ok, err));
    ok.options[0].selected = 99;
    std::vector<riftwii::FilePatch> out;
    EXPECT_FALSE(PlanFiles(ok, riftwii::DiscIdentity{"RSBE01", 0, 0}, out, err));
}
static void test_mismatch() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), pkg, err));
    std::vector<riftwii::FilePatch> out;
    EXPECT_TRUE(PlanFiles(pkg, riftwii::DiscIdentity{"XXXX00", 0, 0}, out, err));
    EXPECT_EQ(out.size(), std::size_t(0));
    EXPECT_TRUE(PlanFiles(pkg, riftwii::DiscIdentity{"RSBE01", 9, 0}, out, err));
    EXPECT_EQ(out.size(), std::size_t(0));
}
static void test_paths() {
    std::string o;
    EXPECT_TRUE(riftwii::resolve_path("/a/b", "c/d", o));
    EXPECT_EQ(o, std::string("/a/b/c/d"));
    EXPECT_TRUE(riftwii::resolve_path("/a/b", "/x/y", o));
    EXPECT_EQ(o, std::string("/x/y"));
    EXPECT_TRUE(riftwii::resolve_path("/a/b", "./c", o));
    EXPECT_EQ(o, std::string("/a/b/c"));
    EXPECT_TRUE(riftwii::resolve_path("/a/b", "c/../d", o));
    EXPECT_EQ(o, std::string("/a/b/d"));
    EXPECT_FALSE(riftwii::resolve_path("/a", "../../..", o));
    EXPECT_TRUE(riftwii::resolve_path("/a", "../escape", o));
    EXPECT_EQ(o, std::string("/escape"));
    EXPECT_FALSE(riftwii::resolve_path("/", "..", o));
    EXPECT_FALSE(riftwii::resolve_path("relative", "x", o));
    EXPECT_FALSE(riftwii::resolve_path("/a", "x:y", o));
    EXPECT_FALSE(riftwii::resolve_path("/a", "x\\y", o));
    EXPECT_FALSE(riftwii::resolve_path("/a", "x{y}", o));
    EXPECT_FALSE(riftwii::resolve_path("/a", "x$y", o));
    std::string bad = std::string("x") + char(1) + "y";
    EXPECT_FALSE(riftwii::resolve_path("/a", bad, o));
    EXPECT_FALSE(riftwii::resolve_path("/a", "", o));
}
static void test_atomic() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), pkg, err));
    riftwii::Package snap = pkg;
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\" resize=\"maybe\"/></patch></wiidisc>", pkg, err));
    EXPECT_EQ(pkg.patches.size(), snap.patches.size());
    EXPECT_EQ(pkg.options.size(), snap.options.size());
    EXPECT_EQ(pkg.root, snap.root);
    riftwii::DiscIdentity disc{"RSBE01", 0, 0};
    std::vector<riftwii::FilePatch> out;
    EXPECT_TRUE(PlanFiles(snap, disc, out, err));
    std::vector<riftwii::FilePatch> snapOut = out;
    riftwii::Package bad = snap;
    bad.options[0].selected = 50;
    EXPECT_FALSE(PlanFiles(bad, disc, out, err));
    EXPECT_EQ(out.size(), snapOut.size());
    riftwii::Package rel2 = snap;
    rel2.patches["p1"].files[0].disc = "relative.bin";
    EXPECT_FALSE(PlanFiles(rel2, disc, out, err));
    EXPECT_EQ(out.size(), snapOut.size());
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"relative.bin\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(pkg.patches.at("p").files[0].is_filename);
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"dir/relative.bin\" external=\"b\"/></patch></wiidisc>", pkg, err));
}
static void test_ordered() {
    riftwii::Package pkg;
    std::string err;
    std::string xml = std::string("<wiidisc version=\"1\" root=\"/r\">") +
        "<options><section name=\"s\">" +
        "<option name=\"o1\" default=\"2\"><choice name=\"c1\"><patch id=\"p1\"/></choice><choice name=\"c2\"><patch id=\"p2\"/><patch id=\"p1\"/></choice></option>" +
        "</section></options>" +
        "<patch id=\"p1\"><file disc=\"/1a\" external=\"e1a\"/><file disc=\"/1b\" external=\"e1b\"/></patch>" +
        "<patch id=\"p2\"><file disc=\"/2a\" external=\"e2a\"/></patch>" +
        "</wiidisc>";
    EXPECT_TRUE(riftwii::parse_package(xml, pkg, err));
    std::vector<riftwii::FilePatch> out;
    EXPECT_TRUE(PlanFiles(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, out, err));
    EXPECT_EQ(out.size(), std::size_t(3));
    if (out.size() == 3) {
        EXPECT_EQ(out[0].disc, std::string("/2a"));
        EXPECT_EQ(out[0].external, std::string("/r/e2a"));
        EXPECT_EQ(out[1].disc, std::string("/1a"));
        EXPECT_EQ(out[1].external, std::string("/r/e1a"));
        EXPECT_EQ(out[2].disc, std::string("/1b"));
    }
    EXPECT_FALSE(PlanFiles(pkg, riftwii::DiscIdentity{"abc123", 0, 0}, out, err));
    EXPECT_FALSE(PlanFiles(pkg, riftwii::DiscIdentity{"SHORT", 0, 0}, out, err));
    EXPECT_FALSE(PlanFiles(pkg, riftwii::DiscIdentity{"ABCDEF!", 0, 0}, out, err));
}
static void test_limits() {
    riftwii::Package pkg;
    std::string err;
    std::string big(5000, 'A');
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"" + big + "\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    std::string over(2 * 1024 * 1024, 'x');
    EXPECT_FALSE(riftwii::parse_package(over, pkg, err));
    std::string withNull = std::string("<wiidisc version=\"1\"><patch id=\"p\">") + char(0) + "</patch></wiidisc>";
    EXPECT_FALSE(riftwii::parse_package(withNull, pkg, err));
}
static void test_comment_cdata_isolation() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><!-- <?xml x?><!DOCTYPE d><?foo?> --><patch id=\"p\"><file disc=\"/a\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a\" external=\"b\"/><!-- <memory offset=\"0\"/> --></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><!-- unterminated doctype <!DOCTYPE", pkg, err));
}

static void test_disc_path_validation() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a/../..\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/bad:name\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a//b\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a/\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_FALSE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a/./b\" external=\"b\"/></patch></wiidisc>", pkg, err));
    EXPECT_TRUE(riftwii::parse_package("<wiidisc version=\"1\"><patch id=\"p\"><file disc=\"/a/b..c/d...bin\" external=\"b\"/></patch></wiidisc>", pkg, err));
    riftwii::Package ok;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), ok, err));
    ok.patches["p1"].files[0].disc = "/../escape.bin";
    std::vector<riftwii::FilePatch> out;
    EXPECT_FALSE(PlanFiles(ok, riftwii::DiscIdentity{"RSBE01", 0, 0}, out, err));
}

static void test_file_field_preservation() {
    riftwii::Package pkg;
    std::string err;
    EXPECT_TRUE(riftwii::parse_package(GoodXml(), pkg, err));
    EXPECT_EQ(pkg.patches.at("p1").files[0].resize, true);
    EXPECT_EQ(pkg.patches.at("p1").files[0].create, false);
    EXPECT_EQ(pkg.patches.at("p1").files[0].file_offset, std::uint64_t(0));
    EXPECT_EQ(pkg.patches.at("p2").files[0].resize, false);
    EXPECT_EQ(pkg.patches.at("p2").files[0].create, true);
    EXPECT_EQ(pkg.patches.at("p2").files[0].file_offset, std::uint64_t(4));
    std::vector<riftwii::FilePatch> out;
    EXPECT_TRUE(PlanFiles(pkg, riftwii::DiscIdentity{"RSBE01", 0, 0}, out, err));
    EXPECT_EQ(out.size(), std::size_t(2));
    if (out.size() == 2) {
        EXPECT_EQ(out[0].offset, std::uint64_t(16));
        EXPECT_EQ(out[0].length, std::uint64_t(32));
        EXPECT_EQ(out[1].offset, std::uint64_t(16));
        EXPECT_EQ(out[1].length, std::uint64_t(32));
        EXPECT_EQ(out[1].resize, false);
        EXPECT_EQ(out[1].create, true);
        EXPECT_EQ(out[1].file_offset, std::uint64_t(4));
    }
}

// A Patch assembled in code (no `order`) still plans every entry.
static void test_plan_without_order() {
    riftwii::Package pkg;
    pkg.filter = riftwii::DiscFilter();
    riftwii::Option opt;
    opt.name = "o";
    opt.selected = 1;
    riftwii::Choice ch;
    ch.name = "c";
    ch.patches.push_back("p");
    opt.choices.push_back(ch);
    pkg.options.push_back(opt);
    riftwii::Patch patch;
    riftwii::FilePatch f;
    f.disc = "/a.bin";
    f.external = "a.bin";
    patch.files.push_back(f);
    riftwii::MemoryPatch m;
    m.has_offset = true;
    m.offset = 0x80001800;
    m.value = {0x00};
    patch.memory.push_back(m);
    pkg.patches["p"] = patch;
    riftwii::Plan plan;
    std::string err;
    EXPECT_FALSE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, riftwii::PlanOptions{}, plan, err));
    EXPECT_TRUE(err.find("<memory>") != std::string::npos);
    riftwii::PlanOptions all;
    all.allow_memory = true;
    EXPECT_TRUE(riftwii::plan_package(pkg, riftwii::DiscIdentity{"ABCDEF", 0, 0}, all, plan, err));
    EXPECT_EQ(plan.files.size(), std::size_t(1));
    EXPECT_EQ(plan.memory.size(), std::size_t(1));
    EXPECT_EQ(plan.order.size(), std::size_t(2));
    if (!plan.files.empty()) EXPECT_EQ(plan.files[0].external, std::string("/riivolution/a.bin"));
}

static void test_read_package() {
    riftwii::Package pkg;
    std::string err;
    std::istringstream good(GoodXml());
    EXPECT_TRUE(riftwii::read_package(good, pkg, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(pkg.options.size(), std::size_t(2));

    std::istringstream bad("<wiidisc version=\"2\"></wiidisc>");
    riftwii::Package pkg2;
    EXPECT_FALSE(riftwii::read_package(bad, pkg2, err));
    EXPECT_FALSE(err.empty());

    std::istringstream big(std::string(2 * 1024 * 1024, 'x'));
    riftwii::Package pkg3;
    EXPECT_FALSE(riftwii::read_package(big, pkg3, err));
    EXPECT_EQ(err, std::string("xml too large"));

    const std::string xml = GoodXml();
    for (std::size_t size : {std::size_t(4096), std::size_t(20000), std::size_t(1048576)}) {
        std::istringstream padded(xml + std::string(size - xml.size(), ' '));
        EXPECT_TRUE(riftwii::read_package(padded, pkg, err));
        EXPECT_TRUE(err.empty());
    }
    pkg.root = "/preserved";
    std::istringstream overLimit(xml + std::string(1048577 - xml.size(), ' '));
    EXPECT_FALSE(riftwii::read_package(overLimit, pkg, err));
    EXPECT_EQ(pkg.root, std::string("/preserved"));

    std::istringstream embeddedNull(xml + std::string(1, '\0') + "ignored");
    EXPECT_FALSE(riftwii::read_package(embeddedNull, pkg, err));
    EXPECT_EQ(err, std::string("embedded null not allowed"));
    EXPECT_EQ(pkg.root, std::string("/preserved"));

    std::istringstream invalidTail(xml + std::string(20000, ' ') + "<extra/>");
    EXPECT_FALSE(riftwii::read_package(invalidTail, pkg, err));
    EXPECT_EQ(pkg.root, std::string("/preserved"));

    std::istringstream unreadable(xml);
    unreadable.setstate(std::ios::badbit);
    EXPECT_FALSE(riftwii::read_package(unreadable, pkg, err));
    EXPECT_EQ(err, std::string("read error"));
    EXPECT_EQ(pkg.root, std::string("/preserved"));

    std::istringstream failed(xml);
    failed.setstate(std::ios::failbit);
    EXPECT_FALSE(riftwii::read_package(failed, pkg, err));
    EXPECT_EQ(err, std::string("read error"));

    std::istringstream empty;
    EXPECT_FALSE(riftwii::read_package(empty, pkg, err));
    EXPECT_EQ(pkg.root, std::string("/preserved"));
}

int main() {
    test_successful();
    test_hex_overflow();
    test_unsupported();
    test_duplicates();
    test_dtd_pi();
    test_empty();
    test_refs_choices();
    test_mismatch();
    test_paths();
    test_atomic();
    test_ordered();
    test_limits();
    test_comment_cdata_isolation();
    test_disc_path_validation();
    test_file_field_preservation();
    test_plan_without_order();
    test_read_package();
    if (g_failures == 0) {
        std::cout << "ALL PATCH TESTS PASSED" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
        return 1;
    }
}
