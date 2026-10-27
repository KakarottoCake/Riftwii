// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// NAND list buffer sizes and parsing (riftwii/nandlist.hpp), against
// mocked ISFS counts and errors.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/nandlist.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

// A zeroed list buffer sized for `asked` entries, like the Wii side.
std::vector<char> sized_buffer(std::uint32_t asked, std::size_t& bytes) {
    EXPECT_TRUE(nand_list_bytes(asked, bytes));
    return std::vector<char>(bytes, '\0');
}

// Packs `entries` as NUL-terminated strings from the start of `list`.
void pack_entries(std::vector<char>& list, const std::vector<std::string>& entries) {
    std::size_t at = 0;
    for (const std::string& e : entries) {
        std::memcpy(list.data() + at, e.c_str(), e.size() + 1);
        at += e.size() + 1;
    }
}

void test_counts() {
    std::size_t bytes = 0;
    // Zero names need no buffer.
    EXPECT_FALSE(nand_list_bytes(0, bytes));
    EXPECT_TRUE(nand_list_bytes(1, bytes));
    EXPECT_TRUE(nand_list_bytes(kNandMaxEntries, bytes));
    // More than the whole file system is damage.
    EXPECT_FALSE(nand_list_bytes(kNandMaxEntries + 1, bytes));
    // Must not overflow into a small buffer size.
    EXPECT_FALSE(nand_list_bytes(0xFFFFFFFFu, bytes));
}

void test_rounding() {
    std::size_t bytes = 0;
    EXPECT_TRUE(nand_list_bytes(1, bytes));
    EXPECT_EQ(bytes, std::size_t(32));
    EXPECT_TRUE(nand_list_bytes(3, bytes));
    EXPECT_EQ(bytes, std::size_t(64));
    EXPECT_TRUE(nand_list_bytes(kNandMaxEntries, bytes));
    EXPECT_EQ(bytes, std::size_t(79872));
}

void test_normal_list() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(3, bytes);
    pack_entries(list, {"alpha", "beta", "gamma"});
    std::vector<std::string> names;
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, 3, 3, names));
    EXPECT_EQ(names.size(), std::size_t(3));
    EXPECT_EQ(names[0], std::string("alpha"));
    EXPECT_EQ(names[1], std::string("beta"));
    EXPECT_EQ(names[2], std::string("gamma"));
}

void test_empty_skipped() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(3, bytes);
    pack_entries(list, {"a", "", "b"});
    std::vector<std::string> names;
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, 3, 3, names));
    EXPECT_EQ(names.size(), std::size_t(2));
    EXPECT_EQ(names[0], std::string("a"));
    EXPECT_EQ(names[1], std::string("b"));
}

void test_got_asked() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(3, bytes);
    pack_entries(list, {"one", "two"});
    std::vector<std::string> names;
    // Fewer back than asked for is fine.
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, 3, 2, names));
    EXPECT_EQ(names.size(), std::size_t(2));
    EXPECT_EQ(names[0], std::string("one"));
    EXPECT_EQ(names[1], std::string("two"));
    // More back than the buffer was sized for is refused.
    names.assign(1, std::string("old"));
    EXPECT_FALSE(parse_nand_list(list.data(), bytes, 2, 3, names));
    EXPECT_TRUE(names.empty());
}

void test_name_lengths() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(1, bytes);
    std::vector<std::string> names;
    // Twelve characters fit.
    pack_entries(list, {"123456789012"});
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, 1, 1, names));
    EXPECT_EQ(names.size(), std::size_t(1));
    EXPECT_EQ(names[0], std::string("123456789012"));
    // Thirteen do not.
    std::fill(list.begin(), list.end(), '\0');
    pack_entries(list, {"1234567890123"});
    names.assign(1, std::string("old"));
    EXPECT_FALSE(parse_nand_list(list.data(), bytes, 1, 1, names));
    EXPECT_TRUE(names.empty());
}

void test_missing_nul() {
    // The last entry runs into the end with no NUL.
    const char raw[] = {'a', 'b', '\0', 'c', 'd'};
    std::vector<std::string> names;
    EXPECT_FALSE(parse_nand_list(raw, sizeof(raw), 2, 2, names));
    EXPECT_TRUE(names.empty());
}

struct FakeDir {
    std::int32_t first_result = 0;
    std::uint32_t first_count = 0;
    std::int32_t second_result = 0;
    std::uint32_t second_count = 0;
    std::vector<std::string> entries;
    int calls = 0;
    std::uint32_t seen_in = 0;
};

std::int32_t fake_read_dir(void* ctx, char* list, std::uint32_t* count) {
    FakeDir* f = static_cast<FakeDir*>(ctx);
    if (count == nullptr) return -1;
    if (list == nullptr) {
        ++f->calls;
        *count = f->first_count;
        return f->first_result;
    }
    ++f->calls;
    f->seen_in = *count;
    if (f->second_result < 0) {
        *count = f->second_count;
        return f->second_result;
    }
    std::size_t at = 0;
    for (std::size_t i = 0; i < f->entries.size(); ++i) {
        const std::string& e = f->entries[i];
        std::memcpy(list + at, e.c_str(), e.size() + 1);
        at += e.size() + 1;
    }
    *count = f->second_count;
    return f->second_result;
}

int g_allocs = 0;
int g_frees = 0;
bool g_alloc_fail = false;

void* counting_alloc(std::size_t bytes) {
    ++g_allocs;
    if (g_alloc_fail) return nullptr;
    return std::malloc(bytes);
}

void counting_free(void* block) {
    ++g_frees;
    std::free(block);
}

void reset_alloc_counts() {
    g_allocs = 0;
    g_frees = 0;
    g_alloc_fail = false;
}

void test_with_first_fails() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = -1;
    f.first_count = 0u;
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 99u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::NotThere);
    EXPECT_TRUE(names.empty());
    EXPECT_EQ(g_allocs, 0);
    EXPECT_EQ(g_frees, 0);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_with_empty() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = 0;
    f.first_count = 0u;
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 99u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::Ok);
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(count == 0u);
    EXPECT_EQ(g_allocs, 0);
    EXPECT_EQ(g_frees, 0);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_with_too_many() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = 0;
    f.first_count = kNandMaxEntries + std::uint32_t(1);
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 0u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::Damaged);
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(count == kNandMaxEntries + std::uint32_t(1));
    EXPECT_EQ(g_allocs, 0);
    EXPECT_EQ(g_frees, 0);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_with_alloc_fails() {
    reset_alloc_counts();
    g_alloc_fail = true;
    FakeDir f;
    f.first_result = 0;
    f.first_count = 2u;
    f.second_result = 0;
    f.second_count = 2u;
    f.entries.push_back("a");
    f.entries.push_back("b");
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 0u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::NoRoom);
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(count == 2u);
    // No buffer to release, so no free is the balanced result.
    EXPECT_EQ(g_allocs, 1);
    EXPECT_EQ(g_frees, 0);
}

void test_with_second_fails() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = 0;
    f.first_count = 2u;
    f.second_result = -1;
    f.second_count = 0u;
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 0u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::Damaged);
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(count == 2u);
    EXPECT_EQ(g_allocs, 1);
    EXPECT_EQ(g_frees, 1);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_with_second_too_many() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = 0;
    f.first_count = 2u;
    f.second_result = 0;
    f.second_count = 3u;
    f.entries.push_back("a");
    f.entries.push_back("b");
    std::vector<std::string> names(1, std::string("old"));
    std::uint32_t count = 0u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::Damaged);
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(count == 2u);
    EXPECT_EQ(g_allocs, 1);
    EXPECT_EQ(g_frees, 1);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_with_normal() {
    reset_alloc_counts();
    FakeDir f;
    f.first_result = 0;
    f.first_count = 3u;
    f.second_result = 0;
    f.second_count = 3u;
    f.entries.push_back("alpha");
    f.entries.push_back("beta");
    f.entries.push_back("gamma");
    std::vector<std::string> names;
    std::uint32_t count = 0u;
    const NandList r = list_nand_with(fake_read_dir, &f, counting_alloc, counting_free, names, count);
    EXPECT_TRUE(r == NandList::Ok);
    EXPECT_TRUE(count == 3u);
    EXPECT_EQ(f.seen_in, 3u);
    EXPECT_EQ(names.size(), std::size_t(3));
    if (names.size() == 3) {
        EXPECT_EQ(names[0], std::string("alpha"));
        EXPECT_EQ(names[1], std::string("beta"));
        EXPECT_EQ(names[2], std::string("gamma"));
    }
    EXPECT_EQ(g_allocs, 1);
    EXPECT_EQ(g_frees, 1);
    EXPECT_EQ(g_allocs, g_frees);
}

void test_got_zero() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(3, bytes);
    pack_entries(list, {"a", "b"});
    std::vector<std::string> names(1, std::string("old"));
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, 3u, 0u, names));
    EXPECT_TRUE(names.empty());
}

void test_null_list() {
    std::vector<std::string> names(1, std::string("old"));
    EXPECT_FALSE(parse_nand_list(nullptr, 32, 1u, 1u, names));
    EXPECT_TRUE(names.empty());
}

void test_full_buffer() {
    std::size_t bytes = 0;
    EXPECT_TRUE(nand_list_bytes(kNandMaxEntries, bytes));
    std::vector<char> list(bytes, '\0');
    const std::string entry(kNandNameMax, 'a');
    std::size_t at = 0;
    for (std::uint32_t i = 0; i < kNandMaxEntries; ++i) {
        std::memcpy(list.data() + at, entry.c_str(), entry.size() + 1);
        at += entry.size() + 1;
    }
    std::vector<std::string> names;
    EXPECT_TRUE(parse_nand_list(list.data(), bytes, kNandMaxEntries, kNandMaxEntries, names));
    EXPECT_EQ(names.size(), static_cast<std::size_t>(kNandMaxEntries));
    if (!names.empty()) {
        EXPECT_EQ(names[0], entry);
        EXPECT_EQ(names[names.size() - 1], entry);
    }
}

void test_nul_at_end() {
    const char raw[] = {'a', '\0', 'b', 'c', '\0'};
    std::vector<std::string> names;
    EXPECT_TRUE(parse_nand_list(raw, sizeof(raw), 2u, 2u, names));
    EXPECT_EQ(names.size(), std::size_t(2));
    if (names.size() == 2) {
        EXPECT_EQ(names[0], std::string("a"));
        EXPECT_EQ(names[1], std::string("bc"));
    }
}

void test_long_name_no_overread() {
    std::size_t bytes = 0;
    EXPECT_TRUE(nand_list_bytes(1u, bytes));
    EXPECT_EQ(bytes, std::size_t(32));
    std::vector<char> list(bytes, 'x');
    std::vector<std::string> names(1, std::string("old"));
    EXPECT_FALSE(parse_nand_list(list.data(), bytes, 1u, 1u, names));
    EXPECT_TRUE(names.empty());
}

}  // namespace

void test_parse_checked() {
    std::size_t bytes = 0;
    std::vector<char> list = sized_buffer(3, bytes);
    pack_entries(list, {"alpha", "beta", "gamma"});
    std::vector<std::string> names;
    EXPECT_TRUE(parse_nand_list_checked(list.data(), bytes, 3, 3, names) == NandParse::Ok);
    EXPECT_EQ(names.size(), std::size_t(3));
    EXPECT_EQ(names[0], std::string("alpha"));
    EXPECT_EQ(names[1], std::string("beta"));
    EXPECT_EQ(names[2], std::string("gamma"));
    // More back than the buffer was sized for is Bad.
    names.assign(1, std::string("old"));
    EXPECT_TRUE(parse_nand_list_checked(list.data(), bytes, 2, 3, names) == NandParse::Bad);
    EXPECT_TRUE(names.empty());
    // The last entry running into the end with no NUL is Bad.
    const char raw[] = {'a', 'b', '\0', 'c', 'd'};
    names.assign(1, std::string("old"));
    EXPECT_TRUE(parse_nand_list_checked(raw, sizeof(raw), 2, 2, names) == NandParse::Bad);
    EXPECT_TRUE(names.empty());
    // Thirteen characters are Bad.
    std::size_t one_bytes = 0;
    std::vector<char> one = sized_buffer(1, one_bytes);
    pack_entries(one, {"1234567890123"});
    names.assign(1, std::string("old"));
    EXPECT_TRUE(parse_nand_list_checked(one.data(), one_bytes, 1, 1, names) == NandParse::Bad);
    EXPECT_TRUE(names.empty());
}

int main() {
    test_parse_checked();
    test_counts();
    test_rounding();
    test_normal_list();
    test_empty_skipped();
    test_got_asked();
    test_name_lengths();
    test_missing_nul();
    test_with_first_fails();
    test_with_empty();
    test_with_too_many();
    test_with_alloc_fails();
    test_with_second_fails();
    test_with_second_too_many();
    test_with_normal();
    test_got_zero();
    test_null_list();
    test_full_buffer();
    test_nul_at_end();
    test_long_name_no_overread();
    return g_failures == 0 ? 0 : 1;
}
