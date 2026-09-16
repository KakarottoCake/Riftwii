#include "riftwii/overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_FALSE(cond) \
    do { \
        if (cond) { \
            std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; \
            g_failures++; \
        } \
    } while (0)

class MemorySource final : public riftwii::ByteSource {
public:
    MemorySource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    std::uint64_t size() const override { return data_.size(); }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > data_.size() || static_cast<std::uint64_t>(length) > data_.size() - offset) {
            return false;
        }
        if (length > 0) {
            if (destination == nullptr) return false;
            std::memcpy(destination, data_.data() + offset, length);
        }
        return true;
    }
private:
    std::vector<std::uint8_t> data_;
};

class FailingSource final : public riftwii::ByteSource {
public:
    FailingSource(std::uint64_t size) : size_(size) {}
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t, std::uint8_t*, std::size_t) const override {
        return false;
    }
private:
    std::uint64_t size_;
};

static void test_overlap() {
    std::vector<std::uint8_t> orig_data(50, 0x11);
    MemorySource orig(orig_data);
    riftwii::ReadOverlay overlay(orig, 50);

    std::vector<std::uint8_t> ext1_data(20, 0x22);
    MemorySource src1(ext1_data);
    std::string err;
    EXPECT_TRUE(overlay.append({10, 0, 20, &src1}, err));

    std::vector<std::uint8_t> ext2_data(20, 0x33);
    MemorySource src2(ext2_data);
    EXPECT_TRUE(overlay.append({20, 0, 20, &src2}, err));

    std::vector<std::uint8_t> buf(50, 0);
    EXPECT_TRUE(overlay.read(0, buf.data(), 50));

    for (std::size_t i = 0; i < 10; ++i) EXPECT_EQ(buf[i], 0x11);
    for (std::size_t i = 10; i < 20; ++i) EXPECT_EQ(buf[i], 0x22);
    for (std::size_t i = 20; i < 40; ++i) EXPECT_EQ(buf[i], 0x33);
    for (std::size_t i = 40; i < 50; ++i) EXPECT_EQ(buf[i], 0x11);
}

static void test_zero_gaps() {
    std::vector<std::uint8_t> orig_data(20, 0xAA);
    MemorySource orig(orig_data);
    riftwii::ReadOverlay overlay(orig, 100);

    std::vector<std::uint8_t> ext_data(20, 0xBB);
    MemorySource src(ext_data);
    std::string err;
    EXPECT_TRUE(overlay.append({40, 0, 20, &src}, err));

    std::vector<std::uint8_t> buf(100, 0xFF);
    EXPECT_TRUE(overlay.read(0, buf.data(), 100));

    for (std::size_t i = 0; i < 20; ++i) EXPECT_EQ(buf[i], 0xAA);
    for (std::size_t i = 20; i < 40; ++i) EXPECT_EQ(buf[i], 0x00);
    for (std::size_t i = 40; i < 60; ++i) EXPECT_EQ(buf[i], 0xBB);
    for (std::size_t i = 60; i < 100; ++i) EXPECT_EQ(buf[i], 0x00);
}

static void test_grown_shrunk() {
    std::vector<std::uint8_t> orig_data(30, 0x77);
    MemorySource orig(orig_data);

    riftwii::ReadOverlay grown(orig, 60);
    EXPECT_EQ(grown.size(), 60ULL);
    std::vector<std::uint8_t> gbuf(60, 0xFF);
    EXPECT_TRUE(grown.read(0, gbuf.data(), 60));
    for (std::size_t i = 0; i < 30; ++i) EXPECT_EQ(gbuf[i], 0x77);
    for (std::size_t i = 30; i < 60; ++i) EXPECT_EQ(gbuf[i], 0x00);

    riftwii::ReadOverlay shrunk(orig, 15);
    EXPECT_EQ(shrunk.size(), 15ULL);
    std::vector<std::uint8_t> sbuf(15, 0);
    EXPECT_TRUE(shrunk.read(0, sbuf.data(), 15));
    for (std::size_t i = 0; i < 15; ++i) EXPECT_EQ(sbuf[i], 0x77);

    EXPECT_FALSE(shrunk.read(10, sbuf.data(), 10));
    EXPECT_FALSE(shrunk.read(16, sbuf.data(), 0));
    EXPECT_TRUE(shrunk.read(15, nullptr, 0));
}

static void test_boundary_overflow() {
    std::vector<std::uint8_t> orig_data(100, 0x12);
    MemorySource orig(orig_data);
    riftwii::ReadOverlay overlay(orig, 100);
    std::string err;

    EXPECT_FALSE(overlay.append({10, 0, 0, &orig}, err));
    EXPECT_FALSE(overlay.append({10, 0, 10, nullptr}, err));
    EXPECT_FALSE(overlay.append({95, 0, 10, &orig}, err));
    EXPECT_FALSE(overlay.append({0, 95, 10, &orig}, err));
    EXPECT_FALSE(overlay.append({std::numeric_limits<std::uint64_t>::max() - 5, 0, 10, &orig}, err));
    EXPECT_FALSE(overlay.append({0, std::numeric_limits<std::uint64_t>::max() - 5, 10, &orig}, err));

    std::vector<std::uint8_t> one_byte(1, 0xFF);
    MemorySource single(one_byte);
    riftwii::ReadOverlay cap_overlay(orig, 10000);
    for (std::size_t i = 0; i < 4096; ++i) {
        EXPECT_TRUE(cap_overlay.append({i, 0, 1, &single}, err));
    }
    EXPECT_FALSE(cap_overlay.append({4096, 0, 1, &single}, err));

    std::uint8_t b = 0;
    EXPECT_FALSE(overlay.read(101, &b, 0));
    EXPECT_TRUE(overlay.read(100, nullptr, 0));
    EXPECT_TRUE(overlay.read(100, &b, 0));
    EXPECT_FALSE(overlay.read(100, nullptr, 1));
    EXPECT_FALSE(overlay.read(50, nullptr, 10));
    EXPECT_FALSE(overlay.read(95, &b, 10));
    EXPECT_FALSE(overlay.read(std::numeric_limits<std::uint64_t>::max() - 5, &b, 10));
    EXPECT_FALSE(overlay.read(0, &b, 16 * 1024 * 1024 + 1));
}

static void test_failing_source_unchanged_destination() {
    std::vector<std::uint8_t> orig_data(100, 0x01);
    MemorySource orig(orig_data);
    riftwii::ReadOverlay overlay(orig, 100);

    FailingSource failing(50);
    std::string err;
    EXPECT_TRUE(overlay.append({20, 0, 50, &failing}, err));

    std::vector<std::uint8_t> dest(100, 0xAA);
    EXPECT_FALSE(overlay.read(0, dest.data(), 100));
    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_EQ(dest[i], 0xAA);
    }
}

static void test_fully_covered_failing_original() {
    FailingSource failing_orig(100);
    riftwii::ReadOverlay overlay(failing_orig, 100);

    std::vector<std::uint8_t> part1(60, 0x44);
    std::vector<std::uint8_t> part2(40, 0x55);
    MemorySource src1(part1);
    MemorySource src2(part2);
    std::string err;
    EXPECT_TRUE(overlay.append({0, 0, 60, &src1}, err));
    EXPECT_TRUE(overlay.append({60, 0, 40, &src2}, err));

    std::vector<std::uint8_t> buf(100, 0);
    EXPECT_TRUE(overlay.read(0, buf.data(), 100));
    for (std::size_t i = 0; i < 60; ++i) EXPECT_EQ(buf[i], 0x44);
    for (std::size_t i = 60; i < 100; ++i) EXPECT_EQ(buf[i], 0x55);

    std::vector<std::uint8_t> sub_buf(50, 0);
    EXPECT_TRUE(overlay.read(25, sub_buf.data(), 50));
    for (std::size_t i = 0; i < 35; ++i) EXPECT_EQ(sub_buf[i], 0x44);
    for (std::size_t i = 35; i < 50; ++i) EXPECT_EQ(sub_buf[i], 0x55);
}

static void test_random_deterministic_oracle() {
    std::mt19937 rng(1337);
    constexpr std::uint64_t kOrigSize = 512;
    constexpr std::uint64_t kVirtualSize = 1024;

    std::vector<std::uint8_t> orig_data(kOrigSize);
    for (std::size_t i = 0; i < kOrigSize; ++i) {
        orig_data[i] = static_cast<std::uint8_t>(rng() & 0xFF);
    }
    MemorySource orig(orig_data);
    riftwii::ReadOverlay overlay(orig, kVirtualSize);

    std::vector<std::uint8_t> oracle(kVirtualSize, 0x00);
    std::memcpy(oracle.data(), orig_data.data(), kOrigSize);

    std::vector<std::vector<std::uint8_t>> source_buffers;
    std::vector<std::unique_ptr<MemorySource>> source_objects;

    std::string err;
    for (int i = 0; i < 40; ++i) {
        std::uint64_t dest = rng() % (kVirtualSize - 10);
        std::uint64_t max_len = kVirtualSize - dest;
        std::uint64_t len = 1 + (rng() % std::min<std::uint64_t>(max_len, 100));
        std::uint64_t src_padding = rng() % 30;
        std::uint64_t src_size = len + src_padding;
        std::uint64_t src_offset = rng() % (src_padding + 1);

        std::vector<std::uint8_t> sdata(src_size);
        for (std::size_t b = 0; b < src_size; ++b) {
            sdata[b] = static_cast<std::uint8_t>(rng() & 0xFF);
        }
        source_buffers.push_back(std::move(sdata));
        source_objects.push_back(std::make_unique<MemorySource>(source_buffers.back()));

        EXPECT_TRUE(overlay.append({dest, src_offset, len, source_objects.back().get()}, err));
        std::memcpy(oracle.data() + dest, source_buffers.back().data() + src_offset, len);
    }

    for (int q = 0; q < 200; ++q) {
        std::uint64_t read_offset = rng() % kVirtualSize;
        std::uint64_t max_len = kVirtualSize - read_offset;
        std::size_t read_len = static_cast<std::size_t>(rng() % (max_len + 1));

        std::vector<std::uint8_t> read_buf(read_len, 0);
        EXPECT_TRUE(overlay.read(read_offset, read_buf.data(), read_len));
        for (std::size_t b = 0; b < read_len; ++b) {
            EXPECT_EQ(read_buf[b], oracle[read_offset + b]);
        }
    }
}

int main() {
    test_overlap();
    test_zero_gaps();
    test_grown_shrunk();
    test_boundary_overflow();
    test_failing_source_unchanged_destination();
    test_fully_covered_failing_original();
    test_random_deterministic_oracle();

    if (g_failures == 0) {
        std::cout << "ALL OVERLAY TESTS PASSED" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
        return 1;
    }
}
