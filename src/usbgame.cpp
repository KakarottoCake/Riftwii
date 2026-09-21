// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/usbgame.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace riftwii {
namespace {

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
    out = a + b;
    return true;
}

bool validate_fragments(const std::vector<D2xFragment>& input, std::uint64_t logical_size,
                        std::vector<D2xFragment>& out, std::string& error) {
    if (logical_size == 0 || logical_size > std::numeric_limits<std::uint32_t>::max()) {
        error = "logical Wii disc sector range does not fit d2x's 32-bit ABI";
        return false;
    }
    std::vector<D2xFragment> sorted = input;
    std::sort(sorted.begin(), sorted.end(), [](const D2xFragment& a, const D2xFragment& b) {
        return a.offset < b.offset;
    });
    std::vector<D2xFragment> result;
    for (const D2xFragment& f : sorted) {
        if (f.count == 0) { error = "zero-length USB fragment"; return false; }
        const std::uint64_t logical_end = std::uint64_t(f.offset) + f.count;
        const std::uint64_t physical_end = std::uint64_t(f.sector) + f.count;
        if (logical_end > (std::uint64_t(1) << 32) || physical_end > (std::uint64_t(1) << 32)) {
            error = "USB fragment exceeds d2x's 32-bit sector range";
            return false;
        }
        if (logical_end > logical_size) { error = "USB fragment lies beyond the logical disc range"; return false; }
        if (!result.empty()) {
            D2xFragment& last = result.back();
            const std::uint64_t last_end = std::uint64_t(last.offset) + last.count;
            if (f.offset < last_end) { error = "overlapping logical USB fragments"; return false; }
            if (f.offset == last_end && std::uint64_t(last.sector) + last.count == f.sector) {
                if (std::uint64_t(last.count) + f.count > std::numeric_limits<std::uint32_t>::max()) {
                    error = "coalesced USB fragment count overflows";
                    return false;
                }
                last.count += f.count;
                continue;
            }
        }
        result.push_back(f);
        if (result.size() > kD2xFragmentLimit) { error = "USB image needs more than 20000 d2x fragments"; return false; }
    }
    if (result.empty()) { error = "USB image has no mapped Wii sectors"; return false; }
    out = std::move(result);
    return true;
}

bool map_container_range(const UsbImage& image, std::uint64_t container_sector, std::uint64_t count,
                         std::uint64_t logical_sector, std::vector<D2xFragment>& out, std::string& error) {
    std::uint64_t at_bytes = container_sector * kUsbSectorBytes;
    std::uint64_t remaining = count;
    std::uint64_t logical = logical_sector;
    for (const UsbImagePiece& piece : image.pieces) {
        const std::uint64_t piece_bytes = piece.file.entry.size;
        if (at_bytes >= piece_bytes) { at_bytes -= piece_bytes; continue; }
        if ((at_bytes & (kUsbSectorBytes - 1)) != 0) { error = "container mapping is not sector aligned"; return false; }
        const std::uint64_t available = (piece_bytes - at_bytes) / kUsbSectorBytes;
        const std::uint64_t take = std::min(remaining, available);
        if (take == 0) continue;
        std::uint64_t file_sector = at_bytes / kUsbSectorBytes;
        std::uint64_t need = take;
        for (const Fragment& f : piece.file.fragments) {
            if (file_sector >= f.sector_count) { file_sector -= f.sector_count; continue; }
            const std::uint64_t n = std::min(need, f.sector_count - file_sector);
            if (f.sector > std::numeric_limits<std::uint32_t>::max() || f.sector + file_sector > std::numeric_limits<std::uint32_t>::max() ||
                n > std::numeric_limits<std::uint32_t>::max() || logical > std::numeric_limits<std::uint32_t>::max()) {
                error = "FAT32 physical extent exceeds d2x's 32-bit sector ABI";
                return false;
            }
            out.push_back({static_cast<std::uint32_t>(logical), static_cast<std::uint32_t>(f.sector + file_sector), static_cast<std::uint32_t>(n)});
            logical += n; need -= n; file_sector = 0;
            if (!need) break;
        }
        if (need) { error = "FAT32 fragments do not cover USB file size"; return false; }
        remaining -= take;
        if (!remaining) return true;
        at_bytes = 0;
    }
    error = "split USB image is missing a required piece or is truncated";
    return false;
}

bool read_exact(const ByteSource& s, std::uint64_t at, std::uint8_t* out, std::size_t n, const char* what, std::string& error) {
    if (at > s.size() || n > s.size() - at || !s.read(at, out, n)) { error = std::string("cannot read ") + what; return false; }
    return true;
}

bool build_raw(const UsbImage& image, std::vector<D2xFragment>& map, std::uint64_t& sectors, std::string& error) {
    UsbContainerSource c(image.pieces);
    if (c.size() == 0 || (c.size() % kUsbSectorBytes) != 0) { error = "ISO size is empty or not a multiple of 512 bytes"; return false; }
    sectors = c.size() / kUsbSectorBytes;
    return map_container_range(image, 0, sectors, 0, map, error);
}

constexpr std::uint64_t kWiiDiscSectors = 143432ull * 2;
constexpr std::uint64_t kWiiDiscBytes = kWiiDiscSectors * 0x8000ull;

bool read_wbfs_layout(const UsbContainerSource& c, std::uint32_t& hd_count, std::uint64_t& block_sectors,
                      std::uint64_t& disc_blocks, std::vector<std::uint8_t>& table, std::string& error) {
    std::uint8_t h[13] = {};
    if (!read_exact(c, 0, h, sizeof(h), "WBFS header", error)) return false;
    if (std::memcmp(h, "WBFS", 4) != 0) { error = "WBFS magic is missing"; return false; }
    hd_count = be32(h + 4);
    const std::uint8_t hd_shift = h[8], wbfs_shift = h[9];
    if (hd_count == 0 || hd_shift != 9 || wbfs_shift < 15 || wbfs_shift >= 32) {
        error = "WBFS has unsupported or invalid sector-size shifts (requires 512-byte hard sectors)"; return false;
    }
    if (h[0x0C] == 0) { error = "WBFS has no active first disc-info record"; return false; }
    block_sectors = std::uint64_t(1) << (wbfs_shift - 9);
    disc_blocks = kWiiDiscSectors >> (wbfs_shift - 15);
    const std::uint64_t table_bytes = disc_blocks * 2;
    constexpr std::uint64_t kDiscInfo = 512, kWlba = kDiscInfo + 0x100;
    if (kWlba > c.size() || table_bytes > c.size() - kWlba) { error = "WBFS disc-info/WLBA table is truncated"; return false; }
    std::uint8_t disc_header[0x100] = {};
    if (!read_exact(c, kDiscInfo, disc_header, sizeof(disc_header), "WBFS disc header", error)) return false;
    if (be32(disc_header + 0x18) != 0x5D1C9EA3u) { error = "WBFS active disc header has no Wii magic"; return false; }
    table.assign(static_cast<std::size_t>(table_bytes), 0);
    return read_exact(c, kWlba, table.data(), table.size(), "WBFS WLBA table", error);
}

bool build_wbfs(const UsbImage& image, std::vector<D2xFragment>& map, std::uint64_t& sectors, std::string& error) {
    UsbContainerSource c(image.pieces); std::uint32_t hd_count = 0; std::uint64_t block_sectors = 0, disc_blocks = 0;
    std::vector<std::uint8_t> table;
    if (!read_wbfs_layout(c, hd_count, block_sectors, disc_blocks, table, error)) return false;
    sectors = kWiiDiscBytes / kUsbSectorBytes;
    for (std::uint64_t i = 0; i < disc_blocks; ++i) {
        const std::uint16_t wlba = be16(table.data() + i * 2);
        if (wlba == 0) continue;  // sparse logical block: d2x must report a read error.
        const std::uint64_t container_sector = std::uint64_t(wlba) * block_sectors;
        if (container_sector >= hd_count || block_sectors > std::uint64_t(hd_count) - container_sector) {
            error = "WBFS WLBA exceeds the header's device-sector count";
            return false;
        }
        if (container_sector > c.size() / kUsbSectorBytes || block_sectors > c.size() / kUsbSectorBytes - container_sector) {
            error = "WBFS WLBA points outside the split container";
            return false;
        }
        if (!map_container_range(image, container_sector, block_sectors, i * block_sectors, map, error)) return false;
    }
    return true;
}

bool build_map(const UsbImage& image, std::vector<D2xFragment>& map, std::uint64_t& sectors, std::string& error) {
    if (image.pieces.empty()) { error = "USB image has no file pieces"; return false; }
    for (const auto& p : image.pieces) {
        if (!p.source) { error = "USB image piece has no byte source"; return false; }
        if (p.file.entry.size == 0) { error = "USB image has an empty file piece"; return false; }
        if (p.source->size() != p.file.entry.size) { error = "USB image piece size differs from its FAT32 entry"; return false; }
    }
    return image.format == UsbImageFormat::Iso ? build_raw(image, map, sectors, error) : build_wbfs(image, map, sectors, error);
}

bool build_reader_map(const UsbImage& image, std::vector<D2xFragment>& map, std::uint64_t& sectors, std::string& error) {
    UsbContainerSource c(image.pieces);
    if (image.format == UsbImageFormat::Iso) {
        if (c.size() == 0 || c.size() % kUsbSectorBytes) { error = "ISO size is empty or not a multiple of 512 bytes"; return false; }
        sectors = c.size() / kUsbSectorBytes;
        map.push_back({0, 0, static_cast<std::uint32_t>(sectors)});
        return true;
    }
    std::uint32_t hd_count = 0; std::uint64_t block = 0, blocks = 0; std::vector<std::uint8_t> table;
    if (!read_wbfs_layout(c, hd_count, block, blocks, table, error)) return false;
    sectors = kWiiDiscBytes / kUsbSectorBytes;
    for (std::uint64_t i = 0; i < blocks; ++i) {
        const std::uint16_t w = be16(table.data() + 2 * i); if (!w) continue;
        const std::uint64_t cs = std::uint64_t(w) * block;
        if (cs >= hd_count || block > std::uint64_t(hd_count) - cs) { error = "WBFS WLBA exceeds the header's device-sector count"; return false; }
        if (cs > c.size() / kUsbSectorBytes || block > c.size() / kUsbSectorBytes - cs) { error = "WBFS WLBA points outside the split container"; return false; }
        map.push_back({static_cast<std::uint32_t>(i * block), static_cast<std::uint32_t>(cs), static_cast<std::uint32_t>(block)});
    }
    return true;
}

}  // namespace

bool D2xFragmentList::encode(std::vector<std::uint8_t>& out, std::string& error) const {
    if (num != entries.size() || maxnum != kD2xFragmentLimit || entries.size() > kD2xFragmentLimit) {
        error = "invalid d2x fragment-list header"; return false;
    }
    std::vector<D2xFragment> checked;
    if (!validate_fragments(entries, size, checked, error) || checked.size() != entries.size() ||
        !std::equal(checked.begin(), checked.end(), entries.begin(), [](const D2xFragment& a, const D2xFragment& b) {
            return a.offset == b.offset && a.sector == b.sector && a.count == b.count;
        })) {
        if (error.empty()) error = "d2x fragments are not sorted and coalesced";
        return false;
    }
    const std::size_t bytes = 12 + entries.size() * 12;
    out.assign(bytes, 0);
    std::memcpy(out.data(), &size, 4); std::memcpy(out.data() + 4, &num, 4); std::memcpy(out.data() + 8, &maxnum, 4);
    if (!entries.empty()) std::memcpy(out.data() + 12, entries.data(), entries.size() * sizeof(D2xFragment));
    return true;
}

UsbContainerSource::UsbContainerSource(std::vector<UsbImagePiece> pieces) : pieces_(std::move(pieces)) {
    for (const auto& p : pieces_) { starts_.push_back(size_); if (!add(size_, p.file.entry.size, size_)) { size_ = 0; break; } }
}
std::uint64_t UsbContainerSource::size() const { return size_; }
bool UsbContainerSource::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > size_ || length > size_ - offset) return false;
    while (length) {
        std::size_t i = 0; while (i + 1 < starts_.size() && starts_[i + 1] <= offset) ++i;
        if (i >= pieces_.size() || !pieces_[i].source) return false;
        const std::uint64_t local = offset - starts_[i];
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(length, pieces_[i].file.entry.size - local));
        if (!pieces_[i].source->read(local, destination, take)) return false;
        offset += take; destination += take; length -= take;
    }
    return true;
}

bool build_usb_fragments(const UsbImage& image, D2xFragmentList& out, std::string& error) {
    std::vector<D2xFragment> raw, checked; std::uint64_t sectors = 0;
    if (!build_map(image, raw, sectors, error) || !validate_fragments(raw, sectors, checked, error)) return false;
    out.size = static_cast<std::uint32_t>(sectors); out.num = static_cast<std::uint32_t>(checked.size());
    out.maxnum = kD2xFragmentLimit; out.entries = std::move(checked); error.clear(); return true;
}

bool UsbDiscSource::open(const UsbImage& image, std::unique_ptr<UsbDiscSource>& out, std::string& error) {
    std::vector<D2xFragment> map; std::uint64_t sectors = 0;
    if (!build_reader_map(image, map, sectors, error)) return false;
    out.reset(new UsbDiscSource(UsbContainerSource(image.pieces), std::move(map), sectors * kUsbSectorBytes)); error.clear(); return true;
}
std::uint64_t UsbDiscSource::size() const { return bytes_; }
bool UsbDiscSource::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > bytes_ || length > bytes_ - offset) return false;
    while (length) {
        const std::uint64_t sector = offset / kUsbSectorBytes, skip = offset % kUsbSectorBytes;
        const auto it = std::upper_bound(map_.begin(), map_.end(), sector, [](std::uint64_t v, const D2xFragment& f) { return v < f.offset; });
        if (it == map_.begin()) return false;
        const D2xFragment& f = *std::prev(it);
        if (sector < f.offset || sector >= std::uint64_t(f.offset) + f.count) return false;
        const std::uint64_t available = (std::uint64_t(f.offset) + f.count - sector) * kUsbSectorBytes - skip;
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(length, available));
        const std::uint64_t container_sector = std::uint64_t(f.sector) + (sector - f.offset);
        if (!container_.read(container_sector * kUsbSectorBytes + skip, destination, take)) return false;
        offset += take; destination += take; length -= take;
    }
    return true;
}

}  // namespace riftwii
