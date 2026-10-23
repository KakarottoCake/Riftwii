// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

#include "riftwii/gametdb.hpp"
#include "riftwii/zipstream.hpp"
#include "zipbuild.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

const char* const kPath = "zipstream-test.zip";
constexpr std::size_t kDataOffset = 30 + 10;  // local header and "wiitdb.xml"
constexpr std::uint64_t kMaxZip = 24u << 20;
constexpr std::uint64_t kMaxXml = 160u << 20;

void Set32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::vector<std::uint8_t> Archive(const std::string& text, int method, bool bad_crc = false) {
    return ziptest::make_zip({{"wiitdb.xml", text, method, bad_crc}});
}

bool Read(const std::vector<std::uint8_t>& bytes, const ZipSink& sink, std::string& error,
          const std::function<bool(double)>& progress = {}, std::uint64_t max_zip = kMaxZip,
          std::uint64_t max_xml = kMaxXml) {
    FILE* file = std::fopen(kPath, "wb");
    if (!file) {
        EXPECT_TRUE(file != nullptr);
        return false;
    }
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    EXPECT_TRUE(written);
    const bool accepted = zip_stream_first(kPath, max_zip, max_xml, sink, error, progress);
    std::remove(kPath);
    return accepted;
}

bool Read(const std::vector<std::uint8_t>& bytes) {
    std::string error;
    const bool accepted = Read(bytes, [](const std::uint8_t*, std::size_t) { return true; }, error);
    if (!accepted) EXPECT_FALSE(error.empty());
    return accepted;
}

void TestStoredTruncation() {
    const std::string xml = "<game><id>RMCE01</id><developer>Example</developer></game>";
    auto bytes = Archive(xml + std::string(300u << 10, ' '), 0);
    bytes.resize(kDataOffset + xml.size() + (300u << 10));
    Set32(bytes, 18, 1u << 20);
    Set32(bytes, 22, 1u << 20);
    WiitdbScanner scanner({"RMCE01", "RSBE01"}, "EN");
    std::string error;
    const ZipSink feed = [&](const std::uint8_t* data, std::size_t count) {
        scanner.feed(reinterpret_cast<const char*>(data), count);
        return true;
    };
    // A full HTTP response, with a usable game early in its partial XML,
    // must fail the scan: the caller must not cache RSBE01 as absent.
    EXPECT_FALSE(Read(bytes, feed, error));
    EXPECT_FALSE(error.empty());
}

void TestLengthsAndCrc(int method) {
    const std::string text = "<datafile><game><id>RMCE01</id></game></datafile>";
    const auto good = Archive(text, method);
    EXPECT_TRUE(Read(good));

    auto bytes = good;
    bytes.resize(kDataOffset + text.size() + (method == 8 ? 5 : 0) - 1);
    EXPECT_FALSE(Read(bytes));

    bytes = good;
    Set32(bytes, 22, static_cast<std::uint32_t>(text.size() + 1));
    EXPECT_FALSE(Read(bytes));
    bytes = good;
    Set32(bytes, 22, static_cast<std::uint32_t>(text.size() - 1));
    EXPECT_FALSE(Read(bytes));

    const std::uint32_t packed = static_cast<std::uint32_t>(text.size() + (method == 8 ? 5 : 0));
    bytes = good;
    Set32(bytes, 18, packed - 1);
    EXPECT_FALSE(Read(bytes));
    bytes = good;
    // The following central-directory bytes exist, but are not part of
    // the deflate stream. Reading them must not hide a length mismatch.
    Set32(bytes, 18, packed + 1);
    EXPECT_FALSE(Read(bytes));
    EXPECT_FALSE(Read(Archive(text, method, true)));

    bytes = good;
    bytes[kDataOffset + (method == 8 ? 5 : 0)] ^= 1;
    EXPECT_FALSE(Read(bytes));

    std::string extracted;
    std::string error;
    const ZipSink sink = [&](const std::uint8_t* data, std::size_t size) {
        extracted.append(reinterpret_cast<const char*>(data), size);
        return true;
    };
    double last_progress = -1;
    unsigned progress_calls = 0;
    EXPECT_TRUE(Read(good, sink, error, [&](double done) {
        EXPECT_TRUE(done >= last_progress && done <= 1.0);
        last_progress = done;
        ++progress_calls;
        return true;
    }));
    EXPECT_EQ(extracted, text);  // never feed the central directory to XML
    EXPECT_EQ(last_progress, 1.0);
    EXPECT_TRUE(progress_calls >= 2);

    unsigned deliveries = 0;
    const ZipSink count = [&](const std::uint8_t*, std::size_t) {
        ++deliveries;
        return true;
    };
    EXPECT_FALSE(Read(good, count, error, [](double) { return false; }));
    EXPECT_EQ(error, "stopped");
    EXPECT_EQ(deliveries, 0u);
    unsigned callbacks = 0;
    EXPECT_FALSE(Read(good, count, error, [&](double) { return ++callbacks < 2; }));
    EXPECT_EQ(error, "stopped");
    EXPECT_EQ(callbacks, 2u);
    EXPECT_FALSE(Read(good, [](const std::uint8_t*, std::size_t) { return false; }, error));
    EXPECT_EQ(error, "stopped");

    EXPECT_FALSE(Read(good, count, error, {}, good.size() - 1));
    EXPECT_EQ(deliveries, 1u);
    EXPECT_FALSE(Read(good, count, error, {}, kMaxZip, text.size() - 1));
    EXPECT_EQ(deliveries, 1u);
    EXPECT_TRUE(Read(Archive("", method)));
}

void TestLargeDeflate(bool compressible) {
    // Use real compression to span several input/output chunks, including
    // cancellation while one small input expands across many output chunks.
    std::string text(300u << 10, 'x');
    if (!compressible) {
        std::uint32_t random = 12345;
        for (char& byte : text) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = static_cast<char>(random & 0xFF);
        }
    }
    std::vector<std::uint8_t> packed(compressBound(static_cast<uLong>(text.size())));
    z_stream stream = {};
    EXPECT_EQ(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    stream.avail_in = static_cast<uInt>(text.size());
    stream.next_out = packed.data();
    stream.avail_out = static_cast<uInt>(packed.size());
    EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    packed.resize(stream.total_out);
    deflateEnd(&stream);

    auto bytes = Archive("", 0);
    bytes.resize(kDataOffset);
    bytes[8] = 8;
    Set32(bytes, 14, zip_crc32_update(0, reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    Set32(bytes, 18, static_cast<std::uint32_t>(packed.size()));
    Set32(bytes, 22, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), packed.begin(), packed.end());
    std::size_t produced = 0;
    const ZipSink count = [&](const std::uint8_t*, std::size_t size) {
        produced += size;
        return true;
    };
    std::string error;
    EXPECT_TRUE(Read(bytes, count, error));
    EXPECT_EQ(produced, text.size());
    unsigned calls = 0;
    EXPECT_FALSE(Read(bytes, count, error, [&](double) { return ++calls < 3; }));
    EXPECT_EQ(error, "stopped");
    EXPECT_EQ(calls, 3u);
    // End the entry before the final deflate byte, even though physical
    // bytes remain in the file. It must not borrow those bytes to finish.
    Set32(bytes, 18, static_cast<std::uint32_t>(packed.size() - 1));
    EXPECT_FALSE(Read(bytes));
}

void TestLargeStored() {
    const std::string text(300u << 10, 'x');
    std::string output;
    std::string error;
    unsigned calls = 0;
    EXPECT_TRUE(Read(Archive(text, 0), [&](const std::uint8_t* data, std::size_t size) {
        output.append(reinterpret_cast<const char*>(data), size);
        return true;
    }, error, [&](double) {
        ++calls;
        return true;
    }));
    EXPECT_EQ(output, text);
    EXPECT_TRUE(calls > 3);
}

void TestHeaderBounds() {
    const auto good = Archive("some XML", 0);
    EXPECT_FALSE(Read({}));
    auto bytes = good;
    bytes[0] = 0;
    EXPECT_FALSE(Read(bytes));
    for (unsigned flag : {1u, 8u, 0x40u, 0x2000u}) {
        bytes = good;
        bytes[6] = static_cast<std::uint8_t>(flag);
        bytes[7] = static_cast<std::uint8_t>(flag >> 8);
        EXPECT_FALSE(Read(bytes));
    }
    bytes = good;
    bytes[8] = 12;
    EXPECT_FALSE(Read(bytes));
    bytes = good;
    Set32(bytes, 18, 0xFFFFFFFFu);
    Set32(bytes, 22, 0xFFFFFFFFu);
    EXPECT_FALSE(Read(bytes));
    bytes = good;
    Set32(bytes, 26, 0xFFFFFFFFu);  // name and extra fields extend past EOF
    EXPECT_FALSE(Read(bytes));
}

}  // namespace

int main() {
    TestStoredTruncation();
    TestLengthsAndCrc(0);
    TestLengthsAndCrc(8);
    TestLargeDeflate(true);
    TestLargeDeflate(false);
    TestLargeStored();
    TestHeaderBounds();
    if (g_failures) return 1;
    std::cout << "ZIP streaming: lengths, CRC, truncation, bounds and cancellation passed\n";
    return 0;
}
