// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/source.hpp"
#include "riftwii/usbgame.hpp"
#include "riftwii/apply.hpp"
#include "riftwii/patch.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>

using namespace riftwii;

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (va_ != vb_) { std::cerr << "FAILED: " #a " == " #b " (" << va_ << " != " << vb_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {
constexpr std::size_t kWbfsBlock = 1u << 15;
constexpr std::size_t kDiscInfo = 512;
constexpr std::size_t kWlba = kDiscInfo + 0x100;

void be16(std::vector<std::uint8_t>& v, std::size_t at, std::uint16_t x) { v[at] = x >> 8; v[at + 1] = x; }
void be32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = x >> 24; v[at + 1] = x >> 16; v[at + 2] = x >> 8; v[at + 3] = x;
}

UsbImagePiece piece(std::vector<std::uint8_t> bytes, std::uint64_t sector) {
    UsbImagePiece p;
    p.source = std::make_shared<MemorySource>(std::move(bytes));
    p.file.entry.size = static_cast<std::uint32_t>(p.source->size());
    p.file.fragments.push_back({sector, (p.source->size() + 511) / 512});
    return p;
}

UsbImage iso_image(bool contiguous = false) {
    std::vector<std::uint8_t> a(1024), b(1024);
    for (unsigned i = 0; i < a.size(); ++i) a[i] = static_cast<std::uint8_t>(i);
    for (unsigned i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>(i + 7);
    UsbImage x; x.format = UsbImageFormat::Iso;
    x.pieces.push_back(piece(std::move(a), 100));
    x.pieces.push_back(piece(std::move(b), contiguous ? 102 : 200));
    return x;
}

UsbImage wbfs_image(bool active = true, std::uint16_t wlba = 20, bool split = true, bool truncate = false,
                    std::uint8_t shift = 15) {
    constexpr std::size_t kEntries = 143432u * 2;
    const std::size_t table_bytes = kEntries * 2;
    const std::size_t data = static_cast<std::size_t>(wlba) * kWbfsBlock;
    const std::size_t bytes = truncate ? 700 : std::max(kWlba + table_bytes, data + kWbfsBlock);
    std::vector<std::uint8_t> all(bytes);
    if (bytes >= 13) {
        std::memcpy(all.data(), "WBFS", 4); be32(all, 4, 2000); all[8] = 9; all[9] = shift; all[12] = active ? 1 : 0;
    }
    if (!truncate && bytes >= kWlba + table_bytes) {
        all[kDiscInfo] = 'R'; all[kDiscInfo + 1] = 'M'; all[kDiscInfo + 2] = 'C'; all[kDiscInfo + 3] = 'E';
        be32(all, kDiscInfo + 0x18, 0x5D1C9EA3); be16(all, kWlba, wlba);
        if (data + 512 <= all.size()) { all[data] = 0xAA; all[data + 511] = 0xBB; }
    }
    UsbImage x; x.format = UsbImageFormat::Wbfs;
    if (split && !truncate && data + 16 * 512 < all.size()) {
        const std::size_t cut = data + 16 * 512;
        std::vector<std::uint8_t> first(all.begin(), all.begin() + cut);
        std::vector<std::uint8_t> second(all.begin() + cut, all.end());
        x.pieces.push_back(piece(std::move(first), 1000));
        x.pieces.push_back(piece(std::move(second), 5000));
    } else x.pieces.push_back(piece(std::move(all), 1000));
    return x;
}

// A source with a huge reported size but only a small served prefix.
// Models multi-GB images without allocating them; reads outside the
// prefix fail, like an unmapped region would.
class BigSource final : public ByteSource {
public:
    BigSource(std::uint64_t size, std::vector<std::uint8_t> prefix)
        : size_(size), prefix_(std::move(prefix)) {}
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || static_cast<std::uint64_t>(length) > size_ - offset) return false;
        if (length == 0) return true;
        if (destination == nullptr) return false;
        if (offset + length > prefix_.size()) return false;
        std::memcpy(destination, prefix_.data() + static_cast<std::size_t>(offset), length);
        return true;
    }
private:
    std::uint64_t size_;
    std::vector<std::uint8_t> prefix_;
};

// A huge sparse file with byte ranges placed at explicit offsets. This lets
// the USB mapper prove second-layer reads without allocating an image-sized
// buffer.
class SparseSource final : public ByteSource {
public:
    struct Span { std::uint64_t offset; std::vector<std::uint8_t> bytes; };
    SparseSource(std::uint64_t size, std::vector<Span> spans) : size_(size), spans_(std::move(spans)) {}
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size_ || static_cast<std::uint64_t>(length) > size_ - offset) return false;
        for (const Span& span : spans_) {
            if (offset < span.offset || static_cast<std::uint64_t>(length) > span.bytes.size() ||
                offset - span.offset > span.bytes.size() - length) continue;
            std::memcpy(destination, span.bytes.data() + static_cast<std::size_t>(offset - span.offset), length);
            return true;
        }
        return length == 0;
    }
private:
    std::uint64_t size_;
    std::vector<Span> spans_;
};

UsbImagePiece big_piece(std::uint64_t bytes, std::uint64_t sector, std::vector<std::uint8_t> prefix) {
    UsbImagePiece p;
    p.source = std::make_shared<BigSource>(bytes, std::move(prefix));
    p.file.entry.size = static_cast<std::uint32_t>(bytes);
    p.file.fragments.push_back({sector, bytes / 512});
    return p;
}

// ContentProvider serving the disc side from a USB image source and the
// SD side from memory. The disc path is cosmetic here: file-to-FST mapping
// is loader-side, while this proves plan output applies over a USB-mapped
// source through the same overlay machinery as disc images.
class UsbTestProvider final : public ContentProvider {
public:
    UsbTestProvider(std::vector<std::uint8_t> disc, std::map<std::string, std::vector<std::uint8_t>> ext)
        : disc_(std::move(disc)), ext_(std::move(ext)) {}
    OpenStatus open_disc(const std::string&, std::unique_ptr<ByteSource>& out, std::string& error) override {
        if (disc_.empty()) { error = "no disc"; return OpenStatus::NotFound; }
        out.reset(new MemorySource(disc_));
        return OpenStatus::Ok;
    }
    OpenStatus open_external(const std::string& p, std::unique_ptr<ByteSource>& out, std::string& error) override {
        auto it = ext_.find(p);
        if (it == ext_.end()) { error = "no such external"; return OpenStatus::NotFound; }
        out.reset(new MemorySource(it->second));
        return OpenStatus::Ok;
    }
private:
    std::vector<std::uint8_t> disc_;
    std::map<std::string, std::vector<std::uint8_t>> ext_;
};

void test_basic() {
    std::string error; D2xFragmentList list; std::vector<std::uint8_t> bytes;
    UsbImage iso = iso_image();
    EXPECT_TRUE(build_usb_fragments(iso, list, error));
    EXPECT_EQ(list.size, std::uint32_t(4));
    EXPECT_EQ(list.num, std::uint32_t(2));
    EXPECT_EQ(list.entries[0].sector, std::uint32_t(100));
    EXPECT_EQ(list.entries[1].offset, std::uint32_t(2));
    EXPECT_EQ(list.entries[1].sector, std::uint32_t(200));
    std::unique_ptr<UsbDiscSource> disc;
    EXPECT_TRUE(UsbDiscSource::open(iso, disc, error));
    std::uint8_t out[4] = {};
    EXPECT_TRUE(disc->read(1022, out, sizeof(out)) && out[0] == 254 && out[2] == 7);
    EXPECT_TRUE(build_usb_fragments(iso_image(true), list, error) && list.num == 1 && list.entries[0].count == 4);

    UsbImage wbfs = wbfs_image();
    EXPECT_TRUE(build_usb_fragments(wbfs, list, error));
    EXPECT_EQ(list.size, std::uint32_t(143432u * 64u));  // single layer: nothing past 4.7 GB
    EXPECT_EQ(list.num, std::uint32_t(2));
    EXPECT_EQ(list.entries[0].offset, std::uint32_t(0));
    EXPECT_EQ(list.entries[0].sector, std::uint32_t(2280));
    EXPECT_EQ(list.entries[0].count, std::uint32_t(16));
    EXPECT_EQ(list.entries[1].sector, std::uint32_t(5000));
    EXPECT_EQ(list.entries[1].count, std::uint32_t(48));
    EXPECT_TRUE(UsbDiscSource::open(wbfs, disc, error));
    EXPECT_TRUE(disc->read(0, out, 2) && out[0] == 0xAA && out[1] == 0);
    EXPECT_FALSE(disc->read(kWbfsBlock, out, 1));  // sparse virtual Wii block

    EXPECT_FALSE(build_usb_fragments(wbfs_image(false), list, error));
    EXPECT_FALSE(build_usb_fragments(wbfs_image(true, 20, false, true), list, error));
    EXPECT_FALSE(build_usb_fragments(wbfs_image(true, 40), list, error));
    EXPECT_FALSE(build_usb_fragments(wbfs_image(true, 20, false, false, 14), list, error));

    D2xFragmentList bad; bad.size = 4; bad.num = 2; bad.entries = {{0, 1, 3}, {2, 5, 1}};
    EXPECT_FALSE(bad.encode(bytes, error));
    bad.size = 4; bad.num = 1; bad.entries = {{0, 0xFFFFFFFFu, 2}};
    EXPECT_FALSE(bad.encode(bytes, error));

    UsbImage excessive; excessive.format = UsbImageFormat::Iso;
    for (std::uint32_t i = 0; i <= kD2xFragmentLimit; ++i) excessive.pieces.push_back(piece(std::vector<std::uint8_t>(512), i * 2));
    EXPECT_FALSE(build_usb_fragments(excessive, list, error));
}

void test_large_iso() {
    // Two 3 GiB pieces: far past the 256 MiB streaming cap, served by
    // FAT-sized fake sources. d2x reads bulk bytes itself; only the
    // mapping math and small header reads run here.
    constexpr std::uint64_t kThreeGiB = 3ull * 1024 * 1024 * 1024;
    std::vector<std::uint8_t> prefix(1024);
    for (std::size_t i = 0; i < prefix.size(); ++i) prefix[i] = static_cast<std::uint8_t>(i);
    UsbImage x; x.format = UsbImageFormat::Iso;
    x.pieces.push_back(big_piece(kThreeGiB, 100, prefix));
    const std::uint64_t second_local = 1024ull * 1024 * 1024 + 0x40;
    std::vector<std::uint8_t> second_bytes = {0xD1, 0xD2, 0xD3, 0xD4};
    UsbImagePiece second;
    second.source = std::make_shared<SparseSource>(kThreeGiB,
        std::vector<SparseSource::Span>{{second_local, second_bytes}});
    second.file.entry.size = static_cast<std::uint32_t>(kThreeGiB);
    second.file.fragments.push_back({9000000, kThreeGiB / 512});
    second.path = "usb:/games/dual.iso";
    x.pieces.push_back(std::move(second));
    std::string error; D2xFragmentList list;
    EXPECT_TRUE(build_usb_fragments(x, list, error));
    EXPECT_EQ(list.size, std::uint32_t(12582912));
    EXPECT_EQ(list.num, std::uint32_t(2));
    EXPECT_EQ(list.entries[0].offset, std::uint32_t(0));
    EXPECT_EQ(list.entries[0].sector, std::uint32_t(100));
    EXPECT_EQ(list.entries[0].count, std::uint32_t(6291456));
    EXPECT_EQ(list.entries[1].offset, std::uint32_t(6291456));
    EXPECT_EQ(list.entries[1].sector, std::uint32_t(9000000));
    EXPECT_EQ(list.entries[1].count, std::uint32_t(6291456));
    std::vector<std::uint8_t> bytes;
    EXPECT_TRUE(list.encode(bytes, error));
    EXPECT_EQ(bytes.size(), std::size_t(12 + 2 * 12));
    std::unique_ptr<UsbDiscSource> disc;
    EXPECT_TRUE(UsbDiscSource::open(x, disc, error));
    EXPECT_EQ(disc->size(), kThreeGiB * 2);
    std::uint8_t out[4] = {};
    EXPECT_TRUE(disc->read(0, out, sizeof(out)) && out[0] == 0 && out[3] == 3);
    EXPECT_TRUE(disc->read(kThreeGiB + second_local, out, sizeof(out)));
    EXPECT_EQ(out[0], std::uint8_t(0xD1));
    EXPECT_EQ(out[3], std::uint8_t(0xD4));
    EXPECT_FALSE(disc->read(kThreeGiB - 2, out, sizeof(out)));  // beyond the served prefix

    // UsbDiscSource uses the same d2x-sized logical sector field as the
    // fragment list; do not truncate a synthetic larger raw container.
    UsbImage oversized;
    oversized.format = UsbImageFormat::Iso;
    constexpr std::uint64_t kPieceBytes = 0xFFFFFE00ull;
    for (unsigned i = 0; i < 513; ++i) oversized.pieces.push_back(big_piece(kPieceBytes, i, {}));
    EXPECT_FALSE(UsbDiscSource::open(oversized, disc, error));
}

void test_large_wbfs() {
    // A 4 GiB container file with a valid header/table mapping one Wii
    // block, placed past the table like real images do. hd sectors and
    // WLBA targets exceed any 32-bit long file offsets on purpose.
    constexpr std::size_t kPrefixBytes = 2 * 1024 * 1024;
    constexpr std::uint64_t kBlockSector = 2048;  // container sector of Wii block 0
    std::vector<std::uint8_t> prefix(kPrefixBytes, 0);
    std::memcpy(prefix.data(), "WBFS", 4);
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        prefix[at] = v >> 24; prefix[at + 1] = v >> 16; prefix[at + 2] = v >> 8; prefix[at + 3] = v;
    };
    put32(4, 16777216); prefix[8] = 9; prefix[9] = 15; prefix[12] = 1;
    prefix[kDiscInfo] = 'R'; prefix[kDiscInfo + 1] = 'M';
    prefix[kDiscInfo + 2] = 'C'; prefix[kDiscInfo + 3] = 'E';
    put32(kDiscInfo + 0x18, 0x5D1C9EA3);
    prefix[kWlba] = 0; prefix[kWlba + 1] = 32;  // block 0 lives at container sector 2048
    for (int i = 0; i < 64; ++i) prefix[kBlockSector * 512 + i] = static_cast<std::uint8_t>(0xA0 + i);
    UsbImage x; x.format = UsbImageFormat::Wbfs;
    UsbImagePiece p;
    p.source = std::make_shared<BigSource>(0xFFFFFFFFull, std::move(prefix));
    p.file.entry.size = 0xFFFFFFFFu;
    p.file.fragments.push_back({1000, 2000000});
    p.path = "usb:/wbfs/big.wbfs";
    x.pieces.push_back(std::move(p));
    std::string error; D2xFragmentList list;
    EXPECT_TRUE(build_usb_fragments(x, list, error));
    EXPECT_EQ(list.num, std::uint32_t(1));
    EXPECT_EQ(list.entries[0].offset, std::uint32_t(0));
    EXPECT_EQ(list.entries[0].sector, std::uint32_t(3048));
    EXPECT_EQ(list.entries[0].count, std::uint32_t(64));
    std::unique_ptr<UsbDiscSource> disc;
    EXPECT_TRUE(UsbDiscSource::open(x, disc, error));
    std::uint8_t out[4] = {};
    EXPECT_TRUE(disc->read(0, out, sizeof(out)));
    EXPECT_EQ(out[0], std::uint8_t(0xA0));
    EXPECT_EQ(out[3], std::uint8_t(0xA3));
}

void test_dual_layer_split_wbfs() {
    // A two-piece .wbfs/.wbf1 container. Its one mapped Wii block is well
    // into the second layer and physically beyond 4 GiB of the split file.
    constexpr std::uint64_t kFirstBytes = 0xFFFFFE00ull;
    constexpr std::uint64_t kSecondBytes = 0x80000000ull;
    constexpr std::uint32_t kWbfsShift = 20;
    constexpr std::uint64_t kBlockBytes = std::uint64_t(1) << kWbfsShift;
    constexpr std::uint64_t kBlockSectors = kBlockBytes / 512;
    constexpr std::uint64_t kSecondLayerBlock = 6000;
    constexpr std::uint16_t kWlbaSecondLayer = 6000;
    constexpr std::uint64_t kContainerBytes = std::uint64_t(kWlbaSecondLayer) * kBlockBytes;
    constexpr std::uint64_t kSecondLocal = kContainerBytes - kFirstBytes;
    std::vector<std::uint8_t> prefix(2 * 1024 * 1024, 0);
    std::memcpy(prefix.data(), "WBFS", 4);
    be32(prefix, 4, 0xFFFFFFFFu);
    prefix[8] = 9;
    prefix[9] = kWbfsShift;
    prefix[12] = 1;
    prefix[kDiscInfo] = 'R'; prefix[kDiscInfo + 1] = 'M';
    prefix[kDiscInfo + 2] = 'C'; prefix[kDiscInfo + 3] = 'E';
    be32(prefix, kDiscInfo + 0x18, 0x5D1C9EA3);
    be16(prefix, kWlba + kSecondLayerBlock * 2, kWlbaSecondLayer);

    UsbImage image;
    image.format = UsbImageFormat::Wbfs;
    UsbImagePiece first;
    first.source = std::make_shared<BigSource>(kFirstBytes, std::move(prefix));
    first.file.entry.size = static_cast<std::uint32_t>(kFirstBytes);
    first.file.fragments.push_back({1000, kFirstBytes / 512});
    first.path = "usb:/wbfs/dual.wbfs";
    image.pieces.push_back(std::move(first));
    UsbImagePiece second;
    second.source = std::make_shared<SparseSource>(kSecondBytes,
        std::vector<SparseSource::Span>{{kSecondLocal + 7, {0xE1, 0xE2, 0xE3, 0xE4}}});
    second.file.entry.size = static_cast<std::uint32_t>(kSecondBytes);
    second.file.fragments.push_back({20000000, kSecondBytes / 512});
    second.path = "usb:/wbfs/dual.wbf1";
    image.pieces.push_back(std::move(second));

    std::string error;
    D2xFragmentList list;
    EXPECT_TRUE(build_usb_fragments(image, list, error));
    EXPECT_EQ(list.size, std::uint32_t(143432u * 2u * 64u));
    EXPECT_EQ(list.num, std::uint32_t(1));
    EXPECT_EQ(list.entries[0].offset, static_cast<std::uint32_t>(kSecondLayerBlock * kBlockSectors));
    EXPECT_EQ(list.entries[0].sector, static_cast<std::uint32_t>(20000000ull + kSecondLocal / 512));
    EXPECT_EQ(list.entries[0].count, static_cast<std::uint32_t>(kBlockSectors));

    std::unique_ptr<UsbDiscSource> disc;
    EXPECT_TRUE(UsbDiscSource::open(image, disc, error));
    std::uint8_t out[4] = {};
    EXPECT_TRUE(disc->read(kSecondLayerBlock * kBlockBytes + 7, out, sizeof(out)));
    EXPECT_EQ(out[0], std::uint8_t(0xE1));
    EXPECT_EQ(out[3], std::uint8_t(0xE4));
}

// A single-layer disc: d2x takes the list's size as the disc's, so it must
// end at the single-layer length (d2x DVD5_LENGTH) even though the WBFS
// block holding the disc's last sectors runs past it. Games read just past
// that end and expect an error, as from a real drive (Error #001 otherwise).
void test_single_layer_wbfs_size() {
    constexpr std::uint32_t kWbfsShift = 21;
    constexpr std::uint64_t kBlockBytes = std::uint64_t(1) << kWbfsShift;
    constexpr std::uint64_t kBlockSectors = kBlockBytes / 512;
    constexpr std::uint64_t kSingleLayerSectors = 143432ull * 64;
    constexpr std::uint64_t kLastBlock = kSingleLayerSectors / kBlockSectors;  // straddles the end
    std::vector<std::uint8_t> prefix(64 * 1024, 0);
    std::memcpy(prefix.data(), "WBFS", 4);
    be32(prefix, 4, 0xFFFFFFFFu);
    prefix[8] = 9;
    prefix[9] = kWbfsShift;
    prefix[12] = 1;
    prefix[kDiscInfo] = 'S'; prefix[kDiscInfo + 1] = 'B';
    prefix[kDiscInfo + 2] = '4'; prefix[kDiscInfo + 3] = 'E';
    be32(prefix, kDiscInfo + 0x18, 0x5D1C9EA3);
    be16(prefix, kWlba, 2);                  // disc block 0 -> WBFS block 2
    be16(prefix, kWlba + kLastBlock * 2, 1);  // the straddling block -> WBFS block 1

    UsbImage image;
    image.format = UsbImageFormat::Wbfs;
    UsbImagePiece only;
    only.source = std::make_shared<BigSource>(3 * kBlockBytes, std::move(prefix));
    only.file.entry.size = static_cast<std::uint32_t>(3 * kBlockBytes);
    only.file.fragments.push_back({1000, 3 * kBlockBytes / 512});
    only.path = "usb:/wbfs/SB4E01.wbfs";
    image.pieces.push_back(std::move(only));

    std::string error;
    D2xFragmentList list;
    EXPECT_TRUE(build_usb_fragments(image, list, error));
    EXPECT_EQ(list.size, static_cast<std::uint32_t>(kSingleLayerSectors));
    EXPECT_EQ(list.num, std::uint32_t(2));
    EXPECT_EQ(list.entries[1].offset, static_cast<std::uint32_t>(kLastBlock * kBlockSectors));
    EXPECT_EQ(list.entries[1].sector, static_cast<std::uint32_t>(1000 + kBlockSectors));
    EXPECT_EQ(list.entries[1].count, static_cast<std::uint32_t>(kSingleLayerSectors - kLastBlock * kBlockSectors));
    // The game's check read (word 0x460A0000) and d2x's layer probe (word
    // 0x47000000) both lie past the list.
    EXPECT_TRUE(0x460A0000ull * 4 / 512 >= list.size);
    EXPECT_TRUE(0x47000000ull * 4 / 512 >= list.size);
}

void test_collect_split_pieces() {
    std::string error; std::vector<std::string> out;
    EXPECT_TRUE(collect_split_pieces("usb:/wbfs", "game.iso", {"game.iso"}, UsbImageFormat::Iso, out, error));
    EXPECT_EQ(out.size(), std::size_t(1));
    std::vector<std::string> siblings = {"RMCE01.wbfs", "other.wbfs", "RMCE01.wbf1", "RMCE01.wbf2"};
    EXPECT_TRUE(collect_split_pieces("usb:/wbfs", "RMCE01.wbfs", siblings, UsbImageFormat::Wbfs, out, error));
    EXPECT_EQ(out.size(), std::size_t(3));
    EXPECT_EQ(out[1], std::string("usb:/wbfs/RMCE01.wbf1"));
    EXPECT_EQ(out[2], std::string("usb:/wbfs/RMCE01.wbf2"));
    // The collector deliberately receives a source-neutral directory.  The
    // SD catalog uses the identical split-image rule and mapper.
    EXPECT_TRUE(collect_split_pieces("sd:/wbfs", "RMCE01.wbfs", siblings, UsbImageFormat::Wbfs, out, error));
    EXPECT_EQ(out[1], std::string("sd:/wbfs/RMCE01.wbf1"));
    std::vector<std::string> gap = {"RMCE01.wbfs", "RMCE01.wbf1", "RMCE01.wbf3"};
    EXPECT_FALSE(collect_split_pieces("usb:/wbfs", "RMCE01.wbfs", gap, UsbImageFormat::Wbfs, out, error));
    EXPECT_FALSE(error.empty());
    std::vector<std::string> mixed = {"RMCE01.WBFS", "rmce01.WBF1"};
    EXPECT_TRUE(collect_split_pieces("usb:/wbfs", "RMCE01.WBFS", mixed, UsbImageFormat::Wbfs, out, error));
    EXPECT_EQ(out.size(), std::size_t(2));
    EXPECT_EQ(out[1], std::string("usb:/wbfs/RMCE01.wbf1"));  // same FAT file as rmce01.WBF1
    std::vector<std::string> unrelated = {"OTHER.wbf5", "notes.txt"};
    EXPECT_TRUE(collect_split_pieces("usb:/wbfs", "RMCE01.wbfs", unrelated, UsbImageFormat::Wbfs, out, error));
    EXPECT_EQ(out.size(), std::size_t(1));
    EXPECT_FALSE(collect_split_pieces("", "x.wbfs", siblings, UsbImageFormat::Wbfs, out, error));
}

void test_cios_readiness_note() {
    CiosSlotState none[] = {{249, false}, {250, false}, {251, false}};
    const std::string note = cios_readiness_note(none, 3);
    EXPECT_FALSE(note.empty());
    EXPECT_TRUE(note.find("249") != std::string::npos);
    EXPECT_TRUE(note.find("d2x") != std::string::npos);
    EXPECT_TRUE(note.find("v11 beta3") != std::string::npos);
    CiosSlotState one[] = {{249, false}, {250, true}, {251, false}};
    EXPECT_TRUE(cios_readiness_note(one, 3).empty());
    CiosSlotState all[] = {{249, true}, {250, true}, {251, true}};
    EXPECT_TRUE(cios_readiness_note(all, 3).empty());
    EXPECT_TRUE(cios_readiness_note(nullptr, 0).empty() == false);
    EXPECT_TRUE(cios_revision_is_stub(65280));
    EXPECT_FALSE(cios_revision_is_stub(65535));  // guided d2x installs stamp this whatever the version
    EXPECT_FALSE(cios_revision_is_stub(21011));  // d2x v11 beta1
    EXPECT_FALSE(cios_revision_is_stub(21010));  // d2x v10 beta52
    EXPECT_FALSE(cios_revision_is_stub(21008));  // d2x v8 final
    EXPECT_FALSE(cios_revision_is_stub(5662));   // stock IOS56 revision
}

void test_plan_over_usb() {
    // XML -> plan -> replacement over a USB-mapped disc source, consumed
    // through the read overlay. File-to-FST mapping is loader-side; here
    // the planned file applies over the mapped bytes directly.
    const std::string xml =
        std::string("<wiidisc version=\"1\" root=\"/riivolution\">") +
        "<options><section name=\"Mods\">"
        "<option name=\"Mod\" default=\"1\">"
        "<choice name=\"On\"><patch id=\"p\"/></choice>"
        "</option></section></options>"
        "<patch id=\"p\"><file disc=\"/DATA/sys.bin\" external=\"mod.bin\""
        " offset=\"8\" length=\"16\" resize=\"false\" create=\"false\"/></patch>"
        "</wiidisc>";
    Package pkg; std::string err;
    EXPECT_TRUE(parse_package(xml, pkg, err));
    Plan plan; PlanOptions opts;
    EXPECT_TRUE(plan_package(pkg, DiscIdentity{"ABCDEF", 0, 0}, opts, plan, err));
    EXPECT_EQ(plan.files.size(), std::size_t(1));
    EXPECT_EQ(plan.files[0].external, std::string("/riivolution/mod.bin"));
    std::vector<std::uint8_t> orig(64);
    for (std::size_t i = 0; i < orig.size(); ++i) orig[i] = static_cast<std::uint8_t>(i);
    std::vector<std::uint8_t> mod(16, 0xA0);
    UsbTestProvider provider(orig, {{"/riivolution/mod.bin", mod}});
    std::unique_ptr<AppliedFile> applied;
    EXPECT_TRUE(build_replacement(plan.files[0], provider, applied, err));
    EXPECT_EQ(applied->size(), std::uint64_t(64));
    std::vector<std::uint8_t> got(64, 0);
    EXPECT_TRUE(applied->read(0, got.data(), got.size()));
    std::vector<std::uint8_t> want = orig;
    std::copy(mod.begin(), mod.end(), want.begin() + 8);
    EXPECT_TRUE(got == want);
}

}  // namespace

int main() {
    test_basic();
    test_large_iso();
    test_large_wbfs();
    test_dual_layer_split_wbfs();
    test_single_layer_wbfs_size();
    test_collect_split_pieces();
    test_cios_readiness_note();
    test_plan_over_usb();
    if (g_failures == 0) std::cout << "usbgame tests passed\n";
    else std::cerr << g_failures << " TEST CHECKS FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
