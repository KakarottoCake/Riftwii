// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ntfs.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <set>

#include "riftwii/redirect.hpp"

namespace riftwii {
namespace {

constexpr std::uint64_t kBlock = 512;
constexpr std::uint32_t kFixupStride = 512;  // update sequence stride, whatever the sector size
constexpr std::uint32_t kAttrAttributeList = 0x20;
constexpr std::uint32_t kAttrData = 0x80;
constexpr std::uint32_t kAttrIndexRoot = 0x90;
constexpr std::uint32_t kAttrIndexAllocation = 0xA0;
constexpr std::uint32_t kAttrEnd = 0xFFFFFFFFu;
constexpr std::uint16_t kAttrCompressed = 0x0001;
constexpr std::uint16_t kAttrEncrypted = 0x4000;
constexpr std::uint16_t kAttrSparse = 0x8000;
constexpr std::uint16_t kRecordInUse = 0x0001;
constexpr std::uint16_t kRecordDirectory = 0x0002;
constexpr std::uint32_t kFileNameDirectory = 0x10000000u;
constexpr std::uint8_t kNamespaceDos = 2;
constexpr std::uint64_t kRootRecord = 5;
constexpr std::uint64_t kFirstUserRecord = 16;  // 0..15 are the file system's own metadata
constexpr std::uint64_t kRecordMask = 0x0000FFFFFFFFFFFFull;
constexpr std::size_t kMaxRuns = 1u << 20;
constexpr std::size_t kMaxAttributeList = 1u << 22;
constexpr unsigned kMaxIndexDepth = 16;
constexpr std::size_t kMaxIndexBlocks = 1u << 16;
constexpr std::size_t kMaxDirectoryEntries = 100000;
constexpr std::size_t kMaxPathDepth = 64;
constexpr std::uint32_t kReadChunkBlocks = 128;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* p) { return le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32); }

bool power_of_two(std::uint64_t v) { return v != 0 && (v & (v - 1)) == 0; }

// Undoes the update sequence: the last two bytes of every 512-byte stride
// hold the sequence number on disk (a torn write shows as a mismatch) and
// the real bytes are kept in the array.
bool apply_fixups(std::uint8_t* buf, std::size_t size, const char* magic, std::string& error) {
    if (size < 8 || std::memcmp(buf, magic, 4) != 0) {
        error = std::string("not an NTFS ") + magic + " block";
        return false;
    }
    const std::size_t usa_ofs = le16(buf + 4);
    const std::size_t usa_count = le16(buf + 6);
    if (usa_count < 2 || (usa_count - 1) * kFixupStride != size || usa_ofs + usa_count * 2 > size) {
        error = std::string("NTFS ") + magic + " block has a malformed update sequence";
        return false;
    }
    const std::uint16_t usn = le16(buf + usa_ofs);
    for (std::size_t i = 1; i < usa_count; ++i) {
        std::uint8_t* tail = buf + i * kFixupStride - 2;
        if (le16(tail) != usn) {
            error = std::string("NTFS ") + magic + " block is torn (update sequence mismatch)";
            return false;
        }
        tail[0] = buf[usa_ofs + 2 * i];
        tail[1] = buf[usa_ofs + 2 * i + 1];
    }
    return true;
}

// Calls `visit(attribute, length)` for each attribute of a fixed-up MFT
// record until it returns false. Fails on a malformed record.
bool for_each_attribute(const std::vector<std::uint8_t>& rec,
                        const std::function<bool(const std::uint8_t*, std::uint32_t)>& visit, std::string& error) {
    const std::size_t used = std::min<std::size_t>(le32(rec.data() + 0x18), rec.size());
    std::size_t off = le16(rec.data() + 0x14);
    for (;;) {
        if (off + 4 > used) {
            error = "MFT record has no attribute end marker";
            return false;
        }
        if (le32(rec.data() + off) == kAttrEnd) return true;
        if (off + 0x18 > used) {
            error = "MFT record attribute runs past the record";
            return false;
        }
        const std::uint32_t len = le32(rec.data() + off + 4);
        if (len < 0x18 || len > used - off || (len & 7) != 0) {
            error = "MFT record attribute has a bad length";
            return false;
        }
        if (!visit(rec.data() + off, len)) return true;
        off += len;
    }
}

bool name_matches(const std::uint8_t* attr, std::uint32_t len, const std::u16string& name) {
    const std::size_t n = attr[9];
    const std::size_t off = le16(attr + 0x0A);
    if (n != name.size()) return false;
    if (n == 0) return true;
    if (off + 2 * n > len) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (le16(attr + off + 2 * i) != name[i]) return false;
    }
    return true;
}

bool resident_value(const std::uint8_t* attr, std::uint32_t len, const std::uint8_t*& value, std::uint32_t& size) {
    size = le32(attr + 0x10);
    const std::uint32_t off = le16(attr + 0x14);
    if (off > len || size > len - off) return false;
    value = attr + off;
    return true;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string utf16_to_utf8(const std::uint8_t* p, std::size_t units) {
    std::string out;
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t c = le16(p + 2 * i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
            const std::uint32_t lo = le16(p + 2 * (i + 1));
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        append_utf8(out, c);
    }
    return out;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

const std::u16string kI30 = u"$I30";

}  // namespace

bool ntfs_decode_runs(const std::uint8_t* pairs, std::size_t size, std::uint64_t first_vcn,
                      std::vector<NtfsRun>& out, std::string& error) {
    std::size_t i = 0;
    std::uint64_t vcn = first_vcn;
    std::int64_t lcn = 0;
    while (i < size) {
        const std::uint8_t header = pairs[i++];
        if (header == 0) return true;
        const unsigned length_bytes = header & 0x0F;
        const unsigned offset_bytes = header >> 4;
        if (length_bytes == 0 || length_bytes > 8 || offset_bytes > 8 || length_bytes + offset_bytes > size - i) {
            error = "NTFS run list is malformed";
            return false;
        }
        std::uint64_t length = 0;
        for (unsigned k = 0; k < length_bytes; ++k) length |= static_cast<std::uint64_t>(pairs[i + k]) << (8 * k);
        i += length_bytes;
        NtfsRun run;
        run.vcn = vcn;
        run.length = length;
        if (offset_bytes == 0) {
            run.sparse = true;
        } else {
            std::uint64_t raw = 0;
            for (unsigned k = 0; k < offset_bytes; ++k) raw |= static_cast<std::uint64_t>(pairs[i + k]) << (8 * k);
            i += offset_bytes;
            if (offset_bytes < 8 && (raw & (std::uint64_t(1) << (8 * offset_bytes - 1)))) {
                raw |= ~std::uint64_t(0) << (8 * offset_bytes);  // sign-extend the delta
            }
            const std::int64_t delta = static_cast<std::int64_t>(raw);
            if ((delta > 0 && lcn > std::numeric_limits<std::int64_t>::max() - delta) || lcn + delta < 0) {
                error = "NTFS run list points outside the volume";
                return false;
            }
            lcn += delta;
            run.lcn = static_cast<std::uint64_t>(lcn);
        }
        if (length == 0 || vcn > std::numeric_limits<std::uint64_t>::max() - length || out.size() >= kMaxRuns) {
            error = "NTFS run list is malformed";
            return false;
        }
        vcn += length;
        out.push_back(run);
    }
    return true;  // ran to the end of the attribute without a terminator
}

bool NtfsVolume::is_boot_sector(const std::uint8_t* block) {
    return std::memcmp(block + 3, "NTFS    ", 8) == 0 && block[510] == 0x55 && block[511] == 0xAA;
}

bool NtfsVolume::mount_at(BlockReader reader, std::uint64_t volume_lba, NtfsVolume& out, std::string& error) {
    if (!reader) {
        error = "no block reader";
        return false;
    }
    std::uint8_t b[kBlock];
    if (!reader(volume_lba, 1, b)) {
        error = "cannot read the NTFS boot sector";
        return false;
    }
    if (!is_boot_sector(b)) {
        error = "not an NTFS boot sector";
        return false;
    }
    NtfsGeometry g;
    g.volume_lba = volume_lba;
    g.bytes_per_sector = le16(b + 0x0B);
    if (g.bytes_per_sector < 512 || g.bytes_per_sector > 4096 || !power_of_two(g.bytes_per_sector)) {
        error = "NTFS sector size " + std::to_string(g.bytes_per_sector) + " is not supported";
        return false;
    }
    const std::uint8_t spc = b[0x0D];
    if (spc > 0x80) {
        const unsigned shift = 256u - spc;
        if (shift > 16) {
            error = "NTFS cluster size is out of range";
            return false;
        }
        g.sectors_per_cluster = 1u << shift;
    } else {
        g.sectors_per_cluster = spc;
    }
    if (!power_of_two(g.sectors_per_cluster)) {
        error = "NTFS sectors per cluster is invalid";
        return false;
    }
    g.cluster_bytes = std::uint64_t(g.bytes_per_sector) * g.sectors_per_cluster;
    if (g.cluster_bytes > (2u << 20)) {
        error = "NTFS cluster size is out of range";
        return false;
    }
    const auto record_size = [&](std::int8_t v, std::uint32_t& size) {
        const std::uint64_t s = v > 0 ? std::uint64_t(v) * g.cluster_bytes : (v < 0 && v >= -20 ? (std::uint64_t(1) << -v) : 0);
        if (s < 512 || s > 65536 || !power_of_two(s)) return false;
        size = static_cast<std::uint32_t>(s);
        return true;
    };
    if (!record_size(static_cast<std::int8_t>(b[0x40]), g.mft_record_bytes) ||
        !record_size(static_cast<std::int8_t>(b[0x44]), g.index_block_bytes)) {
        error = "NTFS record or index block size is invalid";
        return false;
    }
    g.total_clusters = le64(b + 0x28) / g.sectors_per_cluster;
    g.mft_lcn = le64(b + 0x30);
    if (g.total_clusters == 0 || g.mft_lcn >= g.total_clusters) {
        error = "NTFS MFT location is outside the volume";
        return false;
    }

    NtfsVolume v;
    v.reader_ = std::move(reader);
    v.geo_ = g;
    // $MFT's own record (0) is read at the boot sector's location; its
    // $DATA run list then locates every other record. Should $MFT carry an
    // attribute list, the extents in the base record cover the records the
    // list points at.
    std::vector<std::uint8_t> rec(g.mft_record_bytes);
    if (!v.read_device(volume_lba * kBlock + g.mft_lcn * g.cluster_bytes, rec.size(), rec.data())) {
        error = "cannot read the NTFS MFT";
        return false;
    }
    if (!apply_fixups(rec.data(), rec.size(), "FILE", error)) return false;
    bool ok = true;
    if (!for_each_attribute(rec, [&](const std::uint8_t* a, std::uint32_t len) {
            if (le32(a) != kAttrData || a[8] == 0 || a[9] != 0 || len < 0x40 || le64(a + 0x10) != 0) return true;
            const std::uint32_t mp = le16(a + 0x20);
            if (mp >= len) {
                ok = false;
                return false;
            }
            ok = ntfs_decode_runs(a + mp, len - mp, 0, v.mft_runs_, error);
            return false;
        }, error)) {
        return false;
    }
    if (!ok) return false;
    if (v.mft_runs_.empty()) {
        error = "NTFS $MFT has no data runs";
        return false;
    }
    AttrData mft;
    bool found = false;
    if (!v.find_attribute(0, rec, kAttrData, u"", mft, found, error)) return false;
    if (!found || mft.resident || mft.runs.empty()) {
        error = "NTFS $MFT data attribute is missing";
        return false;
    }
    v.mft_runs_ = std::move(mft.runs);
    std::vector<std::uint8_t> root;
    if (!v.read_record(kRootRecord, root, error)) return false;
    if (!(le16(root.data() + 0x16) & kRecordDirectory)) {
        error = "NTFS root directory record is not a directory";
        return false;
    }
    out = std::move(v);
    error.clear();
    return true;
}

bool NtfsVolume::read_device(std::uint64_t byte_offset, std::size_t length, std::uint8_t* out) const {
    std::vector<std::uint8_t> buf;
    while (length > 0) {
        const std::uint64_t block = byte_offset / kBlock;
        const std::size_t skip = static_cast<std::size_t>(byte_offset % kBlock);
        const std::uint64_t want = (skip + length + kBlock - 1) / kBlock;
        const std::uint32_t blocks = static_cast<std::uint32_t>(std::min<std::uint64_t>(want, kReadChunkBlocks));
        buf.resize(static_cast<std::size_t>(blocks) * kBlock);
        if (!reader_(block, blocks, buf.data())) return false;
        const std::size_t take = std::min<std::size_t>(length, buf.size() - skip);
        std::memcpy(out, buf.data() + skip, take);
        out += take;
        length -= take;
        byte_offset += take;
    }
    return true;
}

bool NtfsVolume::read_runs(const std::vector<NtfsRun>& runs, std::uint64_t byte_offset, std::size_t length,
                           std::uint8_t* out, std::string& error) const {
    const std::uint64_t cb = geo_.cluster_bytes;
    while (length > 0) {
        const std::uint64_t vcn = byte_offset / cb;
        const std::uint64_t within = byte_offset % cb;
        const NtfsRun* run = nullptr;
        for (const NtfsRun& r : runs) {
            if (vcn >= r.vcn && vcn - r.vcn < r.length) {
                run = &r;
                break;
            }
        }
        if (run == nullptr || run->sparse) {
            error = "NTFS metadata lies outside its run list";
            return false;
        }
        const std::uint64_t lcn = run->lcn + (vcn - run->vcn);
        if (lcn >= geo_.total_clusters) {
            error = "NTFS run points past the end of the volume";
            return false;
        }
        const std::uint64_t avail = (run->vcn + run->length - vcn) * cb - within;
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(length, avail));
        if (!read_device(geo_.volume_lba * kBlock + lcn * cb + within, take, out)) {
            error = "NTFS device read failed";
            return false;
        }
        out += take;
        length -= take;
        byte_offset += take;
    }
    return true;
}

bool NtfsVolume::read_record(std::uint64_t record, std::vector<std::uint8_t>& out, std::string& error) const {
    out.assign(geo_.mft_record_bytes, 0);
    if (record > std::numeric_limits<std::uint64_t>::max() / geo_.mft_record_bytes ||
        !read_runs(mft_runs_, record * geo_.mft_record_bytes, out.size(), out.data(), error)) {
        if (error.empty()) error = "NTFS record number out of range";
        error = "MFT record " + std::to_string(record) + ": " + error;
        return false;
    }
    if (!apply_fixups(out.data(), out.size(), "FILE", error)) {
        error = "MFT record " + std::to_string(record) + ": " + error;
        return false;
    }
    if (!(le16(out.data() + 0x16) & kRecordInUse)) {
        error = "MFT record " + std::to_string(record) + " is not in use";
        return false;
    }
    if (le16(out.data() + 0x14) >= out.size()) {
        error = "MFT record " + std::to_string(record) + " is malformed";
        return false;
    }
    return true;
}

bool NtfsVolume::find_attribute(std::uint64_t record_number, const std::vector<std::uint8_t>& record,
                                std::uint32_t type, const std::u16string& name, AttrData& out, bool& found,
                                std::string& error) const {
    found = false;
    out = AttrData{};
    // Takes one extent of the attribute into `out`.
    const auto take = [&](const std::uint8_t* a, std::uint32_t len) {
        if (a[8] == 0) {
            const std::uint8_t* value = nullptr;
            std::uint32_t size = 0;
            if (!resident_value(a, len, value, size)) {
                error = "NTFS resident attribute is malformed";
                return false;
            }
            out.resident = true;
            out.value.assign(value, value + size);
            out.data_size = size;
            out.flags = le16(a + 0x0C);
            return true;
        }
        if (len < 0x40) {
            error = "NTFS non-resident attribute is truncated";
            return false;
        }
        const std::uint64_t lowest = le64(a + 0x10);
        const std::uint32_t mp = le16(a + 0x20);
        if (mp >= len) {
            error = "NTFS run list offset is out of range";
            return false;
        }
        if (lowest == 0) {
            out.data_size = le64(a + 0x30);
            out.flags = le16(a + 0x0C);
        }
        return ntfs_decode_runs(a + mp, len - mp, lowest, out.runs, error);
    };

    // Without an attribute list every extent is in this record.
    AttrData list;
    bool has_list = false;
    bool ok = true;
    if (!for_each_attribute(record, [&](const std::uint8_t* a, std::uint32_t len) {
            if (le32(a) != kAttrAttributeList) return true;
            has_list = true;
            if (a[8] == 0) {
                const std::uint8_t* value = nullptr;
                std::uint32_t size = 0;
                if (!resident_value(a, len, value, size)) {
                    error = "NTFS attribute list is malformed";
                    ok = false;
                } else {
                    list.value.assign(value, value + size);
                }
                return false;
            }
            AttrData runs;
            std::swap(out, runs);
            ok = take(a, len);
            std::swap(out, runs);
            if (ok) {
                if (runs.data_size > kMaxAttributeList) {
                    error = "NTFS attribute list is too large";
                    ok = false;
                } else {
                    list.value.resize(static_cast<std::size_t>(runs.data_size));
                    ok = read_runs(runs.runs, 0, list.value.size(), list.value.data(), error);
                }
            }
            return false;
        }, error)) {
        return false;
    }
    if (!ok) return false;
    if (!has_list) {
        if (!for_each_attribute(record, [&](const std::uint8_t* a, std::uint32_t len) {
                if (le32(a) != type || !name_matches(a, len, name)) return true;
                found = true;
                ok = take(a, len);
                return false;
            }, error)) {
            return false;
        }
        return ok;
    }

    // With one, it names the record holding each extent.
    struct Extent {
        std::uint64_t lowest;
        std::uint64_t record;
    };
    std::vector<Extent> extents;
    for (std::size_t off = 0; off + 0x1A <= list.value.size();) {
        const std::uint8_t* e = list.value.data() + off;
        const std::size_t elen = le16(e + 4);
        if (elen < 0x1A || elen > list.value.size() - off) {
            error = "NTFS attribute list entry is malformed";
            return false;
        }
        if (le32(e) == type) {
            const std::size_t n = e[6];
            const std::size_t noff = e[7];
            bool match = n == name.size() && noff + 2 * n <= elen;
            for (std::size_t i = 0; match && i < n; ++i) match = le16(e + noff + 2 * i) == name[i];
            if (match) extents.push_back(Extent{le64(e + 8), le64(e + 0x10) & kRecordMask});
        }
        off += elen;
    }
    std::sort(extents.begin(), extents.end(), [](const Extent& a, const Extent& b) { return a.lowest < b.lowest; });
    std::vector<std::uint8_t> other;
    for (const Extent& x : extents) {
        const std::vector<std::uint8_t>* rec = &record;
        if (x.record != record_number) {
            if (!read_record(x.record, other, error)) return false;
            rec = &other;
        }
        bool seen = false;
        if (!for_each_attribute(*rec, [&](const std::uint8_t* a, std::uint32_t len) {
                if (le32(a) != type || !name_matches(a, len, name)) return true;
                const std::uint64_t lowest = a[8] ? (len >= 0x40 ? le64(a + 0x10) : ~0ull) : 0;
                if (lowest != x.lowest) return true;
                seen = true;
                ok = take(a, len);
                return false;
            }, error)) {
            return false;
        }
        if (!ok) return false;
        if (!seen) {
            error = "NTFS attribute list names an extent that is missing";
            return false;
        }
        found = true;
    }
    // The extents must join up without gaps or overlaps.
    for (std::size_t i = 1; i < out.runs.size(); ++i) {
        if (out.runs[i].vcn != out.runs[i - 1].vcn + out.runs[i - 1].length) {
            error = "NTFS attribute extents do not join up";
            return false;
        }
    }
    return true;
}

bool NtfsVolume::walk_directory(std::uint64_t record_number, std::vector<DirEntry>& out, std::string& error) const {
    out.clear();
    std::vector<std::uint8_t> rec;
    if (!read_record(record_number, rec, error)) return false;
    if (!(le16(rec.data() + 0x16) & kRecordDirectory)) {
        error = "not a directory";
        return false;
    }
    AttrData root;
    bool found = false;
    if (!find_attribute(record_number, rec, kAttrIndexRoot, kI30, root, found, error)) return false;
    if (!found || !root.resident || root.value.size() < 0x20) {
        error = "NTFS directory has no index root";
        return false;
    }
    const std::uint32_t block_bytes = le32(root.value.data() + 8);
    AttrData alloc;
    bool has_alloc = false;
    if (root.value[0x10 + 0x0C] & 0x01) {  // large index: the tree continues in index blocks
        if (!find_attribute(record_number, rec, kAttrIndexAllocation, kI30, alloc, has_alloc, error)) return false;
        if (!has_alloc || alloc.resident || block_bytes < 512 || block_bytes > 65536 || !power_of_two(block_bytes)) {
            error = "NTFS directory index allocation is missing or malformed";
            return false;
        }
    }
    const std::uint64_t vcn_unit = block_bytes >= geo_.cluster_bytes ? geo_.cluster_bytes : 512;
    std::set<std::uint64_t> visited;
    std::vector<std::uint8_t> block;

    // Walks one index node: its entries, and the child node before each
    // entry that has one. `h` is the node's index header.
    std::function<bool(const std::uint8_t*, std::size_t, unsigned)> walk = [&](const std::uint8_t* h, std::size_t avail,
                                                                               unsigned depth) -> bool {
        if (avail < 0x10) {
            error = "NTFS index node is truncated";
            return false;
        }
        const std::size_t first = le32(h);
        const std::size_t end = le32(h + 4);
        if (first < 0x10 || end > avail || first > end) {
            error = "NTFS index node header is malformed";
            return false;
        }
        for (std::size_t off = first; off + 0x10 <= end;) {
            const std::uint8_t* e = h + off;
            const std::size_t elen = le16(e + 8);
            const std::size_t klen = le16(e + 0x0A);
            const std::uint16_t flags = le16(e + 0x0C);
            if (elen < 0x10 || elen > end - off) {
                error = "NTFS index entry is malformed";
                return false;
            }
            if (flags & 0x01) {  // a child node precedes this entry
                if (!has_alloc || elen < 0x18 || depth >= kMaxIndexDepth) {
                    error = "NTFS index tree is malformed or too deep";
                    return false;
                }
                const std::uint64_t vcn = le64(e + elen - 8);
                if (!visited.insert(vcn).second || visited.size() > kMaxIndexBlocks ||
                    vcn > std::numeric_limits<std::uint64_t>::max() / vcn_unit) {
                    error = "NTFS index tree loops";
                    return false;
                }
                std::vector<std::uint8_t> child(block_bytes);
                if (!read_runs(alloc.runs, vcn * vcn_unit, child.size(), child.data(), error)) return false;
                if (!apply_fixups(child.data(), child.size(), "INDX", error)) return false;
                if (!walk(child.data() + 0x18, child.size() - 0x18, depth + 1)) return false;
            }
            if (flags & 0x02) break;  // the last entry carries no key
            if (klen < 0x42 || 0x10 + klen > elen) {
                error = "NTFS index key is malformed";
                return false;
            }
            const std::uint8_t* key = e + 0x10;
            const std::size_t units = key[0x40];
            if (0x42 + 2 * units > klen) {
                error = "NTFS file name is malformed";
                return false;
            }
            const std::uint64_t ref = le64(e) & kRecordMask;
            // DOS 8.3 aliases repeat a long name; metadata files stay hidden.
            if (key[0x41] != kNamespaceDos && ref >= kFirstUserRecord) {
                DirEntry d;
                d.entry.name = utf16_to_utf8(key + 0x42, units);
                d.entry.is_directory = (le32(key + 0x38) & kFileNameDirectory) != 0;
                d.entry.size = d.entry.is_directory ? 0 : le64(key + 0x30);
                d.record = ref;
                if (d.entry.name != "." && d.entry.name != "..") {
                    if (out.size() >= kMaxDirectoryEntries) {
                        error = "NTFS directory has too many entries";
                        return false;
                    }
                    out.push_back(std::move(d));
                }
            }
            off += elen;
        }
        return true;
    };
    return walk(root.value.data() + 0x10, root.value.size() - 0x10, 0);
}

bool NtfsVolume::resolve(const std::string& path, DirEntry& out, std::string& error) const {
    DirEntry cur;
    cur.entry.is_directory = true;
    cur.record = kRootRecord;
    std::size_t depth = 0;
    std::size_t pos = 0;
    std::vector<DirEntry> entries;
    while (pos < path.size()) {
        std::size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        const std::string part = path.substr(pos, next - pos);
        pos = next + 1;
        if (part.empty() || part == ".") continue;
        if (++depth > kMaxPathDepth || !cur.entry.is_directory) {
            error = "no such file or directory '" + path + "'";
            return false;
        }
        if (!walk_directory(cur.record, entries, error)) return false;
        const DirEntry* hit = nullptr;
        for (const DirEntry& d : entries) {
            if (d.entry.name == part) {
                hit = &d;
                break;
            }
            if (hit == nullptr && same_name(d.entry.name, part)) hit = &d;
        }
        if (hit == nullptr) {
            error = "no such file or directory '" + path + "'";
            return false;
        }
        cur = *hit;
    }
    out = cur;
    return true;
}

bool NtfsVolume::file_of(const DirEntry& d, VolumeFile& out, std::string& error) const {
    out = VolumeFile{};
    out.entry = d.entry;
    if (d.entry.is_directory) return true;
    std::vector<std::uint8_t> rec;
    if (!read_record(d.record, rec, error)) return false;
    AttrData data;
    bool found = false;
    if (!find_attribute(d.record, rec, kAttrData, u"", data, found, error)) return false;
    const std::string what = "'" + d.entry.name + "'";
    if (!found) {
        error = what + " has no data";
        return false;
    }
    if (data.flags & (kAttrCompressed | kAttrEncrypted | kAttrSparse)) {
        error = what + " is compressed, encrypted or sparse on NTFS; d2x needs it stored plainly";
        return false;
    }
    if (data.resident) {
        error = what + " is too small to be a disc image";
        return false;
    }
    out.entry.size = data.data_size;
    const std::uint64_t per_cluster = geo_.cluster_bytes / kBlock;
    std::uint64_t remaining = (data.data_size + kBlock - 1) / kBlock;
    for (const NtfsRun& r : data.runs) {
        if (remaining == 0) break;
        if (r.sparse) {
            error = what + " has holes (sparse runs); d2x needs it stored plainly";
            return false;
        }
        if (r.lcn >= geo_.total_clusters || r.length > geo_.total_clusters - r.lcn) {
            error = what + " has a run outside the volume";
            return false;
        }
        const std::uint64_t start = geo_.volume_lba + r.lcn * per_cluster;
        const std::uint64_t count = std::min(remaining, r.length * per_cluster);
        if (!out.fragments.empty() && out.fragments.back().sector + out.fragments.back().sector_count == start) {
            out.fragments.back().sector_count += count;
        } else {
            out.fragments.push_back(Fragment{start, count});
        }
        remaining -= count;
    }
    if (remaining != 0) {
        error = what + ": its run list is shorter than the file";
        return false;
    }
    return true;
}

bool NtfsVolume::lookup(const std::string& path, VolumeFile& out, std::string& error) const {
    DirEntry d;
    if (!resolve(path, d, error)) return false;
    if (!file_of(d, out, error)) return false;
    error.clear();
    return true;
}

bool NtfsVolume::list(const std::string& path, std::vector<VolumeEntry>& out, std::string& error) const {
    DirEntry d;
    if (!resolve(path, d, error)) return false;
    if (!d.entry.is_directory) {
        error = "'" + path + "' is not a directory";
        return false;
    }
    std::vector<DirEntry> entries;
    if (!walk_directory(d.record, entries, error)) return false;
    out.clear();
    for (DirEntry& e : entries) out.push_back(std::move(e.entry));
    error.clear();
    return true;
}

bool NtfsVolume::read(const VolumeFile& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const {
    if (!reader_) return false;
    if (offset > file.entry.size || length > file.entry.size - offset) return false;
    if (length == 0) return true;
    std::vector<PlacedRun> runs;
    std::string error;
    if (!place_on_fragments(file.fragments, offset, length, runs, error)) return false;
    for (const PlacedRun& r : runs) {
        if (!read_device(r.source * kBlock + r.skip, static_cast<std::size_t>(r.length), out)) return false;
        out += r.length;
    }
    return true;
}

}  // namespace riftwii
