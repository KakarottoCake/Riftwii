// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/source.hpp"
#include "riftwii/usbgame.hpp"

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace riftwii;

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
}  // namespace

int main() {
    std::string error; D2xFragmentList list; std::vector<std::uint8_t> bytes;
    UsbImage iso = iso_image();
    assert(build_usb_fragments(iso, list, error));
    assert(list.size == 4 && list.num == 2 && list.entries[0].sector == 100 && list.entries[1].offset == 2 && list.entries[1].sector == 200);
    std::unique_ptr<UsbDiscSource> disc;
    assert(UsbDiscSource::open(iso, disc, error));
    std::uint8_t out[4] = {};
    assert(disc->read(1022, out, sizeof(out)) && out[0] == 254 && out[2] == 7);
    assert(build_usb_fragments(iso_image(true), list, error) && list.num == 1 && list.entries[0].count == 4);

    UsbImage wbfs = wbfs_image();
    assert(build_usb_fragments(wbfs, list, error));
    assert(list.size == 18359296 && list.num == 2 && list.entries[0].offset == 0 && list.entries[0].sector == 2280 && list.entries[0].count == 16 && list.entries[1].sector == 5000 && list.entries[1].count == 48);
    assert(UsbDiscSource::open(wbfs, disc, error));
    assert(disc->read(0, out, 2) && out[0] == 0xAA && out[1] == 0);
    assert(!disc->read(kWbfsBlock, out, 1));  // sparse virtual Wii block

    assert(!build_usb_fragments(wbfs_image(false), list, error));
    assert(!build_usb_fragments(wbfs_image(true, 20, false, true), list, error));
    assert(!build_usb_fragments(wbfs_image(true, 40), list, error));
    assert(!build_usb_fragments(wbfs_image(true, 20, false, false, 14), list, error));

    D2xFragmentList bad; bad.size = 4; bad.num = 2; bad.entries = {{0, 1, 3}, {2, 5, 1}};
    assert(!bad.encode(bytes, error));
    bad.size = 4; bad.num = 1; bad.entries = {{0, 0xFFFFFFFFu, 2}};
    assert(!bad.encode(bytes, error));

    UsbImage excessive; excessive.format = UsbImageFormat::Iso;
    for (std::uint32_t i = 0; i <= kD2xFragmentLimit; ++i) excessive.pieces.push_back(piece(std::vector<std::uint8_t>(512), i * 2));
    assert(!build_usb_fragments(excessive, list, error));
    std::cout << "usbgame tests passed\n";
}
