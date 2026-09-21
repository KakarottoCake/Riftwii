// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/fat32.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace riftwii {
namespace {

constexpr std::uint32_t kEntryBytes = 32;
constexpr std::uint32_t kLfnChars = 13;
constexpr std::uint32_t kLfnMaxParts = 20;
constexpr std::uint32_t kFatMask = 0x0FFFFFFFu;
constexpr std::uint32_t kFatBad = 0x0FFFFFF7u;
constexpr std::uint32_t kFatEocMin = 0x0FFFFFF8u;
constexpr std::uint8_t kAttrReadOnly = 0x01, kAttrHidden = 0x02, kAttrSystem = 0x04, kAttrLabel = 0x08,
                       kAttrDirectory = 0x10;
constexpr std::uint8_t kAttrLfn = kAttrReadOnly | kAttrHidden | kAttrSystem | kAttrLabel;
constexpr std::uint8_t kAttrMask = 0x3F;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool ascii_ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]);
        unsigned char y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x = static_cast<unsigned char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<unsigned char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// UCS-2 (UTF-16 in practice) units up to the first NUL, as UTF-8.
std::string ucs2_to_utf8(const std::vector<std::uint16_t>& units) {
    std::string out;
    for (std::size_t i = 0; i < units.size(); ++i) {
        std::uint32_t u = units[i];
        if (u == 0) break;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            u = 0x10000 + ((u - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        } else if (u >= 0xD800 && u <= 0xDFFF) {
            u = 0xFFFD;
        }
        append_utf8(out, u);
    }
    return out;
}

std::uint8_t short_checksum(const std::uint8_t* name11) {
    std::uint8_t sum = 0;
    for (int i = 0; i < 11; ++i) {
        sum = static_cast<std::uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

std::string short_name_text(const std::uint8_t* e) {
    std::uint8_t name[11];
    std::memcpy(name, e, 11);
    if (name[0] == 0x05) name[0] = 0xE5;  // escaped KANJI lead byte
    const std::uint8_t flags = e[0x0C];
    std::string base, ext;
    for (int i = 0; i < 8; ++i) base.push_back(static_cast<char>(name[i]));
    for (int i = 8; i < 11; ++i) ext.push_back(static_cast<char>(name[i]));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    while (!ext.empty() && ext.back() == ' ') ext.pop_back();
    auto lower = [](std::string& s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        }
    };
    if (flags & 0x08) lower(base);
    if (flags & 0x10) lower(ext);
    return ext.empty() ? base : base + "." + ext;
}

// Splits "/a/b/c" into components; "" and "." are dropped, ".." rejected.
bool split_path(const std::string& path, std::vector<std::string>& out, std::string& error) {
    out.clear();
    std::string cur;
    auto flush = [&]() {
        if (cur == "..") {
            error = "'..' is not supported in '" + path + "'";
            return false;
        }
        if (!cur.empty() && cur != ".") out.push_back(cur);
        cur.clear();
        return true;
    };
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!flush()) return false;
        } else {
            cur.push_back(c);
        }
    }
    return flush();
}

bool parse_boot_sector(const std::uint8_t* b, std::uint64_t volume_lba, Fat32Geometry& g, std::string& error) {
    if (b[0x1FE] != 0x55 || b[0x1FF] != 0xAA) {
        error = "boot sector signature missing";
        return false;
    }
    const std::uint32_t bps = le16(b + 0x0B);
    const std::uint32_t spc = b[0x0D];
    const std::uint32_t reserved = le16(b + 0x0E);
    const std::uint32_t fats = b[0x10];
    const std::uint32_t root_entries = le16(b + 0x11);
    const std::uint32_t total16 = le16(b + 0x13);
    const std::uint32_t fat16 = le16(b + 0x16);
    const std::uint32_t total32 = le32(b + 0x20);
    const std::uint32_t fat32 = le32(b + 0x24);
    const std::uint32_t ext_flags = le16(b + 0x28);
    const std::uint32_t fs_version = le16(b + 0x2A);
    const std::uint32_t root_cluster = le32(b + 0x2C);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) {
        error = "unsupported bytes per sector " + std::to_string(bps);
        return false;
    }
    if (spc == 0 || spc > 128 || (spc & (spc - 1)) != 0) {
        error = "bad sectors per cluster " + std::to_string(spc);
        return false;
    }
    if (reserved == 0) {
        error = "no reserved sectors";
        return false;
    }
    if (fats == 0 || fats > 4) {
        error = "bad FAT count " + std::to_string(fats);
        return false;
    }
    if (root_entries != 0 || fat16 != 0 || fat32 == 0 || fs_version != 0) {
        error = "not a FAT32 volume";
        return false;
    }
    const std::uint32_t total = total32 != 0 ? total32 : total16;
    if (total == 0) {
        error = "no total sector count";
        return false;
    }
    if (root_cluster < 2) {
        error = "bad root cluster " + std::to_string(root_cluster);
        return false;
    }
    const std::uint64_t data_start = std::uint64_t(reserved) + std::uint64_t(fats) * fat32;
    if (data_start >= total) {
        error = "FAT area exceeds the volume";
        return false;
    }
    std::uint64_t clusters = (total - data_start) / spc;
    // Never trust more clusters than the FAT can describe.
    const std::uint64_t fat_entries = std::uint64_t(fat32) * bps / 4;
    if (fat_entries < 3) {
        error = "FAT too small";
        return false;
    }
    clusters = std::min<std::uint64_t>(clusters, fat_entries - 2);
    if (clusters == 0) {
        error = "no data clusters";
        return false;
    }
    if (clusters > kFatBad - 2) clusters = kFatBad - 2;
    if (root_cluster > clusters + 1) {
        error = "root cluster outside the volume";
        return false;
    }
    std::uint32_t active = 0;
    if (ext_flags & 0x80) {
        active = ext_flags & 0x0F;
        if (active >= fats) {
            error = "active FAT index out of range";
            return false;
        }
    }
    g.volume_lba = volume_lba;
    g.bytes_per_sector = bps;
    g.sectors_per_cluster = spc;
    g.reserved_sectors = reserved;
    g.fat_count = fats;
    g.fat_sectors = fat32;
    g.active_fat = active;
    g.root_cluster = root_cluster;
    g.total_sectors = total;
    g.cluster_count = static_cast<std::uint32_t>(clusters);
    g.data_start_sector = static_cast<std::uint32_t>(data_start);
    return true;
}

}  // namespace

std::uint64_t Fat32Geometry::cluster_lba(std::uint32_t cluster) const {
    const std::uint64_t sector = std::uint64_t(data_start_sector) + std::uint64_t(cluster - 2) * sectors_per_cluster;
    return volume_lba + sector * blocks_per_sector();
}

bool Fat32Volume::mount_at(BlockReader reader, std::uint64_t volume_lba, Fat32Volume& out, std::string& error,
                           const Fat32Limits& limits) {
    if (!reader) {
        error = "no block reader";
        return false;
    }
    std::uint8_t block[kFatBlockBytes];
    if (!reader(volume_lba, 1, block)) {
        error = "cannot read block " + std::to_string(volume_lba);
        return false;
    }
    Fat32Geometry g;
    if (!parse_boot_sector(block, volume_lba, g, error)) return false;
    Fat32Volume v;
    v.reader_ = std::move(reader);
    v.geo_ = g;
    v.limits_ = limits;
    out = std::move(v);
    error.clear();
    return true;
}

bool Fat32Volume::mount(BlockReader reader, Fat32Volume& out, std::string& error, const Fat32Limits& limits) {
    if (!reader) {
        error = "no block reader";
        return false;
    }
    std::uint8_t block[kFatBlockBytes];
    if (!reader(0, 1, block)) {
        error = "cannot read block 0";
        return false;
    }
    std::string first_error;
    Fat32Geometry g;
    if (parse_boot_sector(block, 0, g, first_error)) {
        return mount_at(std::move(reader), 0, out, error, limits);
    }
    if (block[0x1FE] != 0x55 || block[0x1FF] != 0xAA) {
        error = "block 0 is neither a FAT32 boot sector nor an MBR: " + first_error;
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* pe = block + 0x1BE + i * 16;
        const std::uint8_t type = pe[4];
        const std::uint32_t lba = le32(pe + 8);
        if (type == 0 || lba == 0) continue;
        std::uint8_t candidate[kFatBlockBytes];
        if (!reader(lba, 1, candidate)) continue;
        std::string e;
        if (parse_boot_sector(candidate, lba, g, e)) {
            return mount_at(std::move(reader), lba, out, error, limits);
        }
    }
    error = "no FAT32 partition found (block 0: " + first_error + ")";
    return false;
}

bool Fat32Volume::next_cluster(std::uint32_t cluster, std::uint32_t& next, std::string& error) const {
    if (cluster < 2 || cluster > geo_.cluster_count + 1) {
        error = "cluster " + std::to_string(cluster) + " outside the volume";
        return false;
    }
    const std::uint64_t byte_in_fat = std::uint64_t(cluster) * 4;
    const std::uint64_t fat_start_sector = std::uint64_t(geo_.reserved_sectors) +
                                           std::uint64_t(geo_.active_fat) * geo_.fat_sectors;
    const std::uint64_t byte_in_volume = fat_start_sector * geo_.bytes_per_sector + byte_in_fat;
    const std::uint64_t lba = geo_.volume_lba + byte_in_volume / kFatBlockBytes;
    const std::uint32_t within = static_cast<std::uint32_t>(byte_in_volume % kFatBlockBytes);
    if (!fat_cache_valid_ || fat_cache_lba_ != lba) {
        if (!reader_(lba, 1, fat_cache_)) {
            error = "cannot read FAT block " + std::to_string(lba);
            return false;
        }
        fat_cache_lba_ = lba;
        fat_cache_valid_ = true;
    }
    next = le32(fat_cache_ + within) & kFatMask;
    return true;
}

bool Fat32Volume::chain(std::uint32_t first_cluster, std::vector<Fragment>& out, std::string& error) const {
    if (!reader_) {
        error = "volume not mounted";
        return false;
    }
    std::vector<Fragment> frags;
    if (first_cluster == 0) {
        out.clear();
        error.clear();
        return true;
    }
    const std::uint64_t blocks_per_cluster = std::uint64_t(geo_.sectors_per_cluster) * geo_.blocks_per_sector();
    std::uint32_t cluster = first_cluster;
    // Brent's detector avoids allocating one bit per cluster and finds a
    // self/short cycle after O(prefix + cycle) FAT steps, rather than after
    // the volume's advertised cluster count.
    std::uint32_t tortoise = first_cluster;
    std::uint64_t power = 1, steps_since_reset = 0;
    for (;;) {
        if (cluster < 2 || cluster > geo_.cluster_count + 1) {
            error = "cluster chain leaves the volume at cluster " + std::to_string(cluster);
            return false;
        }
        const std::uint64_t lba = geo_.cluster_lba(cluster);
        if (!frags.empty() && frags.back().sector + frags.back().sector_count == lba) {
            frags.back().sector_count += blocks_per_cluster;
        } else {
            Fragment f;
            f.sector = lba;
            f.sector_count = blocks_per_cluster;
            frags.push_back(f);
        }
        std::uint32_t next = 0;
        if (!next_cluster(cluster, next, error)) return false;
        if (next >= kFatEocMin) break;
        if (next == kFatBad) {
            error = "cluster chain hits a bad cluster after " + std::to_string(cluster);
            return false;
        }
        if (next == 0) {
            error = "cluster chain hits a free cluster after " + std::to_string(cluster);
            return false;
        }
        if (power == steps_since_reset) {
            tortoise = cluster;
            power <<= 1;
            steps_since_reset = 0;
        }
        cluster = next;
        ++steps_since_reset;
        if (cluster == tortoise) {
            error = "cluster chain loops";
            return false;
        }
    }
    out = std::move(frags);
    error.clear();
    return true;
}

bool Fat32Volume::walk_directory(std::uint32_t directory_cluster,
                                 const std::function<bool(const Fat32Entry&, bool& stop)>& visit,
                                 std::string& error) const {
    std::vector<std::uint8_t> sector(geo_.bytes_per_sector);
    std::vector<std::uint16_t> lfn;
    std::uint32_t lfn_expected = 0;  // next sequence number expected, 0 = none pending
    std::uint8_t lfn_checksum = 0;
    bool lfn_valid = false;
    auto reset_lfn = [&]() {
        lfn.clear();
        lfn_expected = 0;
        lfn_valid = false;
    };
    std::uint32_t cluster = directory_cluster;
    std::uint32_t tortoise = directory_cluster;
    std::uint64_t power = 1, steps_since_reset = 0;
    std::uint32_t entries_seen = 0;
    for (;;) {
        if (cluster < 2 || cluster > geo_.cluster_count + 1) {
            error = "directory chain leaves the volume at cluster " + std::to_string(cluster);
            return false;
        }
        for (std::uint32_t s = 0; s < geo_.sectors_per_cluster; ++s) {
            const std::uint64_t lba = geo_.cluster_lba(cluster) + std::uint64_t(s) * geo_.blocks_per_sector();
            if (!reader_(lba, geo_.blocks_per_sector(), sector.data())) {
                error = "cannot read directory block " + std::to_string(lba);
                return false;
            }
            for (std::uint32_t off = 0; off + kEntryBytes <= geo_.bytes_per_sector; off += kEntryBytes) {
                const std::uint8_t* e = sector.data() + off;
                if (e[0] == 0x00) {
                    error.clear();
                    return true;  // end of directory
                }
                if (++entries_seen > limits_.max_directory_entries) {
                    error = "directory has too many entries";
                    return false;
                }
                if (e[0] == 0xE5) {
                    reset_lfn();
                    continue;
                }
                const std::uint8_t attr = e[0x0B] & kAttrMask;
                if (attr == kAttrLfn) {
                    const std::uint32_t seq = e[0] & 0x1F;
                    const bool last = (e[0] & 0x40) != 0;
                    if (seq == 0 || seq > kLfnMaxParts) {
                        reset_lfn();
                        continue;
                    }
                    if (last) {
                        lfn.assign(seq * kLfnChars, 0xFFFF);
                        lfn_expected = seq;
                        lfn_checksum = e[0x0D];
                        lfn_valid = true;
                    } else if (!lfn_valid || seq != lfn_expected || e[0x0D] != lfn_checksum) {
                        reset_lfn();
                        continue;
                    }
                    std::uint16_t* dst = lfn.data() + (seq - 1) * kLfnChars;
                    for (int i = 0; i < 5; ++i) dst[i] = le16(e + 1 + i * 2);
                    for (int i = 0; i < 6; ++i) dst[5 + i] = le16(e + 14 + i * 2);
                    for (int i = 0; i < 2; ++i) dst[11 + i] = le16(e + 28 + i * 2);
                    lfn_expected = seq - 1;
                    continue;
                }
                if (attr & kAttrLabel) {
                    reset_lfn();
                    continue;
                }
                Fat32Entry entry;
                entry.short_name = short_name_text(e);
                entry.name = entry.short_name;
                if (lfn_valid && lfn_expected == 0 && short_checksum(e) == lfn_checksum) {
                    std::size_t units = 0;
                    while (units < lfn.size() && lfn[units] != 0) ++units;
                    if (units > 0 && units <= limits_.max_name) entry.name = ucs2_to_utf8(lfn);
                }
                reset_lfn();
                entry.is_directory = (attr & kAttrDirectory) != 0;
                entry.first_cluster = (std::uint32_t(le16(e + 0x14)) << 16) | le16(e + 0x1A);
                entry.size = entry.is_directory ? 0 : le32(e + 0x1C);
                if (entry.short_name == "." || entry.short_name == "..") continue;
                bool stop = false;
                if (!visit(entry, stop)) return false;
                if (stop) {
                    error.clear();
                    return true;
                }
            }
        }
        std::uint32_t next = 0;
        if (!next_cluster(cluster, next, error)) return false;
        if (next >= kFatEocMin) break;
        if (next == kFatBad || next == 0) {
            error = "directory chain is broken after cluster " + std::to_string(cluster);
            return false;
        }
        if (power == steps_since_reset) {
            tortoise = cluster;
            power <<= 1;
            steps_since_reset = 0;
        }
        cluster = next;
        ++steps_since_reset;
        if (cluster == tortoise) {
            error = "directory chain loops";
            return false;
        }
    }
    error.clear();
    return true;
}

bool Fat32Volume::find_in_directory(std::uint32_t directory_cluster, const std::string& name, Fat32Entry& out,
                                    bool& found, std::string& error) const {
    found = false;
    return walk_directory(
        directory_cluster,
        [&](const Fat32Entry& e, bool& stop) {
            if (ascii_ieq(e.name, name) || ascii_ieq(e.short_name, name)) {
                out = e;
                found = true;
                stop = true;
            }
            return true;
        },
        error);
}

bool Fat32Volume::resolve_directory(const std::string& path, std::uint32_t& cluster, std::string& error) const {
    if (!reader_) {
        error = "volume not mounted";
        return false;
    }
    std::vector<std::string> parts;
    if (!split_path(path, parts, error)) return false;
    if (parts.size() > limits_.max_depth) {
        error = "path too deep '" + path + "'";
        return false;
    }
    std::uint32_t cur = geo_.root_cluster;
    for (const std::string& part : parts) {
        Fat32Entry e;
        bool found = false;
        if (!find_in_directory(cur, part, e, found, error)) return false;
        if (!found) {
            error = "no such directory '" + part + "' in '" + path + "'";
            return false;
        }
        if (!e.is_directory) {
            error = "'" + part + "' is not a directory in '" + path + "'";
            return false;
        }
        if (e.first_cluster == 0) {
            error = "directory '" + part + "' has no cluster";
            return false;
        }
        cur = e.first_cluster;
    }
    cluster = cur;
    error.clear();
    return true;
}

bool Fat32Volume::lookup(const std::string& path, Fat32File& out, std::string& error) const {
    std::vector<std::string> parts;
    if (!split_path(path, parts, error)) return false;
    if (parts.empty()) {
        error = "no file name in '" + path + "'";
        return false;
    }
    if (parts.size() > limits_.max_depth) {
        error = "path too deep '" + path + "'";
        return false;
    }
    std::string parent;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) parent += "/" + parts[i];
    std::uint32_t dir = 0;
    if (!resolve_directory(parent, dir, error)) return false;
    Fat32File file;
    bool found = false;
    if (!find_in_directory(dir, parts.back(), file.entry, found, error)) return false;
    if (!found) {
        error = "no such file '" + path + "'";
        return false;
    }
    if (!chain(file.entry.first_cluster, file.fragments, error)) {
        error = "'" + path + "': " + error;
        return false;
    }
    std::uint64_t capacity = 0;
    for (const Fragment& f : file.fragments) capacity += f.sector_count * kFatBlockBytes;
    if (!file.entry.is_directory && file.entry.size > capacity) {
        error = "'" + path + "' is larger than its cluster chain";
        return false;
    }
    out = std::move(file);
    error.clear();
    return true;
}

bool Fat32Volume::list(const std::string& path, std::vector<Fat32Entry>& out, std::string& error) const {
    std::uint32_t dir = 0;
    if (!resolve_directory(path, dir, error)) return false;
    std::vector<Fat32Entry> entries;
    if (!walk_directory(
            dir,
            [&](const Fat32Entry& e, bool&) {
                entries.push_back(e);
                return true;
            },
            error)) {
        return false;
    }
    out = std::move(entries);
    error.clear();
    return true;
}

bool Fat32Volume::read(const Fat32File& file, std::uint64_t offset, std::uint8_t* out, std::size_t length) const {
    if (!reader_) return false;
    if (offset > file.entry.size || length > file.entry.size - offset) return false;
    if (length == 0) return true;
    std::vector<PlacedRun> runs;
    std::string error;
    if (!place_on_fragments(file.fragments, offset, length, runs, error)) return false;
    std::vector<std::uint8_t> buffer;
    std::uint8_t* dst = out;
    for (const PlacedRun& r : runs) {
        const std::uint64_t span = r.skip + r.length;
        const std::uint64_t blocks = (span + kFatBlockBytes - 1) / kFatBlockBytes;
        if (blocks > std::numeric_limits<std::uint32_t>::max()) return false;
        buffer.resize(static_cast<std::size_t>(blocks * kFatBlockBytes));
        if (!reader_(r.source, static_cast<std::uint32_t>(blocks), buffer.data())) return false;
        std::memcpy(dst, buffer.data() + r.skip, static_cast<std::size_t>(r.length));
        dst += r.length;
    }
    return true;
}

}  // namespace riftwii
