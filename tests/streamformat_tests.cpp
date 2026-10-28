// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Compare buffered FILE output with libc's vfprintf, including long lines
// and bytes that cannot be checked with strlen.
#include "riftwii/streamformat.hpp"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>
#include <sys/stat.h>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

namespace {

std::string read_all(FILE* file) {
    std::fflush(file);
    std::rewind(file);
    std::string result;
    for (int byte = std::fgetc(file); byte != EOF; byte = std::fgetc(file)) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

void compare_output(const char* format, ...) {
    FILE* reference = std::tmpfile();
    FILE* actual = std::tmpfile();
    EXPECT_TRUE(reference != nullptr && actual != nullptr);
    if (reference == nullptr || actual == nullptr) {
        if (reference != nullptr) std::fclose(reference);
        if (actual != nullptr) std::fclose(actual);
        return;
    }
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int expected_count = std::vfprintf(reference, format, copy);
    va_end(copy);
    const int actual_count = riftwii::format_to_stream(actual, format, args);
    va_end(args);
    EXPECT_EQ(actual_count, expected_count);
    EXPECT_EQ(read_all(actual), read_all(reference));
    std::fclose(actual);
    std::fclose(reference);
}

int print_to(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int length = riftwii::format_to_stream(stream, format, args);
    va_end(args);
    return length;
}

void test_output() {
    compare_output("");
    compare_output("plain text\n");
    compare_output("%d %u %#08x %lld %llu %zu %%\n", -123, 456u, 0xABCDu,
                   -0x123456789LL, 0xFEDCBA987ULL, std::size_t(47));
    compare_output("[%6lu.%03lu] %.*s\n", 123ul, 4ul, 4, "longer");
    compare_output("%+.3f %e %g %a\n", -1.25, 0.000345, 123456.0, 0.125);
    compare_output("a%cb%c\n", 0, 0xFF);
    for (const std::size_t length : {510u, 511u, 512u, 513u, 1024u, 16384u}) {
        const std::string text(length, 'x');
        compare_output("%s", text.c_str());
        compare_output("<%s> %d\n", text.c_str(), 7);
        compare_output("%*s", static_cast<int>(length), "end");
    }
}

void test_buffering_and_errors() {
    FILE* buffered = std::tmpfile();
    EXPECT_TRUE(buffered != nullptr);
    if (buffered != nullptr) {
        char buffer[4096];
        EXPECT_EQ(std::setvbuf(buffered, buffer, _IOFBF, sizeof buffer), 0);
        EXPECT_EQ(print_to(buffered, "%s %d", "hello", 7), 7);
        struct stat info = {};
        EXPECT_EQ(fstat(fileno(buffered), &info), 0);
        EXPECT_EQ(info.st_size, 0);
        EXPECT_EQ(read_all(buffered), std::string("hello 7"));
        std::fclose(buffered);
    }

    // Unbuffered output makes a read-only stream fail on the write,
    // rather than deferring the error to fflush or fclose.
    FILE* readonly = std::fopen(__FILE__, "rb");
    EXPECT_TRUE(readonly != nullptr);
    if (readonly != nullptr) {
        std::setvbuf(readonly, nullptr, _IONBF, 0);
        EXPECT_EQ(print_to(readonly, "%s", "cannot write"), -1);
        EXPECT_TRUE(std::ferror(readonly) != 0);
        std::fclose(readonly);
    }
}

}  // namespace

int main() {
    test_output();
    test_buffering_and_errors();
    if (g_failures == 0) std::cout << "streamformat tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
