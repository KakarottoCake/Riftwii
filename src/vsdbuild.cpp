// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/vsdbuild.hpp"

#include <algorithm>
#include <cstring>
#include <map>

// The layout follows Microsoft's FAT specification (fatgen103, "Microsoft
// Extensible Firmware Initiative FAT32 File System Specification"): the
// boot sector and FSInfo fields, the 8.3 and long-name directory entries
// and the long name's checksum.
namespace riftwii {
namespace {

constexpr std::uint32_t kMinClusters = 65536 + 16;  // FAT32 by count, with room to spare
constexpr std::uint32_t kFatGoal = 1u << 20;        // clusters per FAT before a bigger cluster (4 MiB of FAT)
constexpr std::uint32_t kMaxClusters = 0x0FFFFFF5u - 2;
constexpr std::uint64_t kRoundSectors = 8192;       // the card's size, in 4 MiB steps
constexpr std::size_t kMaxNameUnits = 255;

void put16(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

// UTF-8 to UTF-16 (the long names'); a byte that is not part of a valid
// sequence, or a character past U+FFFF, becomes '_'.
std::u16string utf16(const std::string& s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = '_';
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && (s[i + 1] & 0xC0) == 0x80) {
            cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
            cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            len = 4;  // past U+FFFF: '_'
        }
        out.push_back(cp < 0x20 ? u'_' : static_cast<char16_t>(cp));
        i += len;
    }
    return out;
}

bool short_char(char c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return std::strchr("!#$%&'()-@^_`{}~", c) != nullptr;
}

std::string upper(const std::string& s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::uint8_t checksum(const std::string& short_name) {
    std::uint8_t sum = 0;
    for (char c : short_name) sum = static_cast<std::uint8_t>(((sum & 1) << 7) + (sum >> 1) + static_cast<std::uint8_t>(c));
    return sum;
}

std::uint32_t long_entries(const VsdPlan::Node& n) {
    return n.long_name ? static_cast<std::uint32_t>((utf16(n.name).size() + 12) / 13) : 0;
}

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) { return (a + b - 1) / b; }

}  // namespace

std::string vsd_short_name(const std::string& name, const std::vector<std::string>& taken, bool& needs_long) {
    // Base and extension at the last dot (a leading dot starts no extension).
    std::string stem = name, ext;
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot + 1);
    }
    bool lossy = false;
    const auto clean = [&lossy](const std::string& s) {
        std::string out;
        for (char ch : upper(s)) {
            if (ch == ' ' || ch == '.') {
                lossy = true;  // dropped
            } else if (short_char(ch)) {
                out.push_back(ch);
            } else {
                out.push_back('_');
                lossy = true;
            }
        }
        return out;
    };
    std::string base = clean(stem), extension = clean(ext);
    if (base.empty()) {
        base = "_";
        lossy = true;
    }
    const auto stored = [](const std::string& b, const std::string& e) {
        std::string s = b.substr(0, 8);
        s.resize(8, ' ');
        std::string x = e.substr(0, 3);
        x.resize(3, ' ');
        return s + x;
    };
    const auto is_taken = [&taken](const std::string& s) {
        return std::find(taken.begin(), taken.end(), s) != taken.end();
    };
    const std::string shown = base + (extension.empty() ? "" : "." + extension);
    needs_long = lossy || base.size() > 8 || extension.size() > 3 || shown != name;
    if (!lossy && base.size() <= 8 && extension.size() <= 3) {
        const std::string plain = stored(base, extension);
        if (!is_taken(plain)) return plain;
        needs_long = true;  // a case-only clash: the long name tells them apart
    }
    for (std::uint32_t n = 1;; ++n) {
        const std::string tail = "~" + std::to_string(n);
        const std::string s = stored(base.substr(0, 8 - tail.size()) + tail, extension);
        if (!is_taken(s)) return s;
    }
}

std::vector<std::string> vsd_gct_folders(const std::vector<std::uint8_t>& gct) {
    std::vector<std::string> out, seen;
    const auto name_char = [](std::uint8_t c) {
        return c >= 0x20 && c < 0x7F && !std::strchr("/\\\":*?<>|", c);
    };
    for (std::size_t i = 0; i < gct.size(); ++i) {
        if (gct[i] != '/') continue;
        std::size_t end = i + 1;
        while (end < gct.size() && end - i <= 64 && name_char(gct[end])) ++end;
        if (end >= gct.size() || gct[end] != '/' || end == i + 1 || end - i > 65) continue;
        const std::string name(gct.begin() + static_cast<std::ptrdiff_t>(i + 1), gct.begin() + static_cast<std::ptrdiff_t>(end));
        // A name may not end in a dot or a space, nor be "." or "..".
        if (name.back() == '.' || name.back() == ' ' || name[0] == ' ') continue;
        // Only the top: "/Project+/pf/" names Project+, not pf, so the
        // scan goes on after the first name of a path.
        const bool top = i == 0 || gct[i - 1] == ':' || !name_char(gct[i - 1]);
        if (top && std::find(seen.begin(), seen.end(), upper(name)) == seen.end()) {
            seen.push_back(upper(name));
            out.push_back(name);
        }
        i = end - 1;
        while (i + 1 < gct.size() && (gct[i + 1] == '/' || name_char(gct[i + 1]))) ++i;
    }
    return out;
}

bool plan_vsd_image(const std::vector<VsdItem>& items, std::uint64_t spare_bytes, VsdPlan& out, std::string& error) {
    VsdPlan plan;
    plan.serial = out.serial;
    plan.fat_date = out.fat_date;
    plan.fat_time = out.fat_time;
    std::vector<VsdPlan::Node> tree(1);
    tree[0].directory = true;
    tree[0].path = "/";
    std::map<std::string, int> by_path;  // upper-cased: FAT names ignore case
    by_path["/"] = 0;

    // A node for every path and every folder above it.
    const auto node_for = [&](const std::string& path, bool directory, std::uint64_t size) -> int {
        if (path.size() < 2 || path[0] != '/' || path.back() == '/') {
            error = "not a card path: " + path;
            return -1;
        }
        int parent = 0;
        std::size_t at = 1;
        while (true) {
            const std::size_t slash = path.find('/', at);
            const bool last = slash == std::string::npos;
            const std::string name = path.substr(at, last ? std::string::npos : slash - at);
            const std::string sub = path.substr(0, last ? std::string::npos : slash);
            if (name.empty() || name == "." || name == "..") {
                error = "not a card path: " + path;
                return -1;
            }
            if (utf16(name).size() > kMaxNameUnits) {
                error = "a name longer than 255 characters: " + path;
                return -1;
            }
            const std::string key = upper(sub);
            const auto found = by_path.find(key);
            if (found != by_path.end()) {
                VsdPlan::Node& n = tree[static_cast<std::size_t>(found->second)];
                if (last) {
                    if (!n.directory || !directory) {
                        error = "listed twice: " + path;
                        return -1;
                    }
                    return found->second;
                }
                if (!n.directory) {
                    error = "a file where a folder should be: " + sub;
                    return -1;
                }
                parent = found->second;
            } else {
                VsdPlan::Node n;
                n.name = name;
                n.parent = parent;
                n.directory = last ? directory : true;
                n.size = last && !directory ? size : 0;
                n.path = sub;
                const int index = static_cast<int>(tree.size());
                tree[static_cast<std::size_t>(parent)].children.push_back(index);
                tree.push_back(std::move(n));
                by_path[key] = index;
                if (last) return index;
                parent = index;
            }
            at = slash + 1;
        }
    };
    for (const VsdItem& item : items) {
        if (!item.directory && item.size > kVsdMaxFileBytes) {
            error = "a file of 4 GiB or more cannot go on a FAT32 card: " + item.path;
            return false;
        }
        if (node_for(item.path, item.directory, item.size) < 0) return false;
    }

    // Each folder's children in name order, their 8.3 names, and the
    // folder's size in directory entries.
    for (VsdPlan::Node& dir : tree) {
        if (!dir.directory) continue;
        std::sort(dir.children.begin(), dir.children.end(), [&tree](int a, int b) {
            return upper(tree[static_cast<std::size_t>(a)].name) < upper(tree[static_cast<std::size_t>(b)].name);
        });
        std::vector<std::string> taken;
        std::uint64_t entries = dir.parent < 0 ? 1 : 2;  // the volume label, or "." and ".."
        for (int c : dir.children) {
            VsdPlan::Node& child = tree[static_cast<std::size_t>(c)];
            child.short_name = vsd_short_name(child.name, taken, child.long_name);
            taken.push_back(child.short_name);
            entries += 1 + long_entries(child);
        }
        dir.size = entries * 32;
    }

    for (const VsdPlan::Node& n : tree) {
        if (n.directory) ++plan.folders;
        else {
            ++plan.files;
            plan.file_bytes += n.size;
        }
    }
    plan.folders -= 1;  // not the root

    // The cluster size: 4 KiB, bigger while the FAT would pass 4 MiB,
    // smaller (or more free space) while the card would have too few
    // clusters to read as FAT32.
    const auto clusters_for = [&tree](std::uint32_t cb) {
        std::uint64_t c = 0;
        for (const VsdPlan::Node& n : tree) {
            if (n.directory) c += std::max<std::uint64_t>(1, ceil_div(n.size, cb));
            else c += ceil_div(n.size, cb);
        }
        return c;
    };
    std::uint32_t cb = 4096;
    std::uint64_t used = 0, data = 0;
    const auto measure = [&]() {
        used = clusters_for(cb);
        data = used + ceil_div(spare_bytes, cb);
    };
    measure();
    while (data > kFatGoal && cb < 32768) {
        cb *= 2;
        measure();
    }
    while (data < kMinClusters && cb > 512) {
        cb /= 2;
        measure();
    }
    if (data < kMinClusters) data = kMinClusters;
    if (data > kMaxClusters) {
        error = "too much for one FAT32 card";
        return false;
    }
    const std::uint32_t spc = cb / kVsdSectorBytes;

    // The size: whole 4 MiB steps; the FAT covers every cluster that fits.
    std::uint32_t fat = static_cast<std::uint32_t>(ceil_div((data + 2) * 4, kVsdSectorBytes));
    std::uint64_t total = 0, count = 0;
    for (int pass = 0; pass < 16; ++pass) {
        const std::uint64_t start = kVsdReservedSectors + 2ull * fat;
        total = ceil_div(start + data * spc, kRoundSectors) * kRoundSectors;
        count = (total - start) / spc;
        const std::uint32_t need = static_cast<std::uint32_t>(ceil_div((count + 2) * 4, kVsdSectorBytes));
        if (need <= fat) break;
        fat = need;
    }
    if (total > 0xFFFFFFFFull) {
        error = "too much for one FAT32 card";
        return false;
    }

    // Clusters in one pass from cluster 2: a folder, its files, then its
    // folders the same way.
    std::vector<int> order;
    std::uint32_t next = 2;
    const std::function<void(int)> place = [&](int i) {
        VsdPlan::Node& n = tree[static_cast<std::size_t>(i)];
        n.clusters = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, ceil_div(n.size, cb)));
        n.first_cluster = next;
        next += n.clusters;
        order.push_back(i);
        for (int c : n.children) {
            VsdPlan::Node& f = tree[static_cast<std::size_t>(c)];
            if (f.directory) continue;
            f.clusters = static_cast<std::uint32_t>(ceil_div(f.size, cb));
            f.first_cluster = f.clusters ? next : 0;
            next += f.clusters;
            order.push_back(c);
        }
        for (int c : n.children)
            if (tree[static_cast<std::size_t>(c)].directory) place(c);
    };
    place(0);

    // The plan's nodes in that order, their links renumbered.
    std::vector<int> renumber(tree.size(), -1);
    for (std::size_t k = 0; k < order.size(); ++k) renumber[static_cast<std::size_t>(order[k])] = static_cast<int>(k);
    plan.nodes.resize(order.size());
    for (std::size_t k = 0; k < order.size(); ++k) {
        VsdPlan::Node n = std::move(tree[static_cast<std::size_t>(order[k])]);
        if (n.parent >= 0) n.parent = renumber[static_cast<std::size_t>(n.parent)];
        for (int& c : n.children) c = renumber[static_cast<std::size_t>(c)];
        plan.nodes[k] = std::move(n);
    }

    plan.cluster_bytes = cb;
    plan.fat_sectors = fat;
    plan.data_start = kVsdReservedSectors + 2 * fat;
    plan.cluster_count = static_cast<std::uint32_t>(count);
    plan.used_clusters = next - 2;
    plan.total_sectors = total;
    plan.image_bytes = total * kVsdSectorBytes;
    if (plan.used_clusters > plan.cluster_count) {
        error = "the card's plan does not add up";
        return false;
    }
    out = std::move(plan);
    return true;
}

namespace {

void boot_sector(const VsdPlan& p, std::uint8_t* s) {
    std::memset(s, 0, kVsdSectorBytes);
    s[0] = 0xEB;
    s[1] = 0x58;
    s[2] = 0x90;
    std::memcpy(s + 3, "MSWIN4.1", 8);
    put16(s + 11, kVsdSectorBytes);
    s[13] = static_cast<std::uint8_t>(p.cluster_bytes / kVsdSectorBytes);
    put16(s + 14, kVsdReservedSectors);
    s[16] = 2;      // FATs
    s[21] = 0xF8;   // media: fixed
    put16(s + 24, 63);   // sectors per track
    put16(s + 26, 255);  // heads
    put32(s + 32, static_cast<std::uint32_t>(p.total_sectors));
    put32(s + 36, p.fat_sectors);
    put32(s + 44, 2);  // the root folder's cluster
    put16(s + 48, 1);  // FSInfo
    put16(s + 50, 6);  // the backup boot sector
    s[64] = 0x80;      // drive number
    s[66] = 0x29;      // the next three fields are set
    put32(s + 67, p.serial);
    std::memcpy(s + 71, "RIFTWII    ", 11);
    std::memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}

void fsinfo_sector(const VsdPlan& p, std::uint8_t* s) {
    std::memset(s, 0, kVsdSectorBytes);
    put32(s, 0x41615252);
    put32(s + 484, 0x61417272);
    put32(s + 488, p.cluster_count - p.used_clusters);
    put32(s + 492, p.used_clusters + 2);
    put32(s + 508, 0xAA550000);
}

void dir_entry(std::uint8_t* e, const std::string& short_name, std::uint8_t attr, std::uint32_t cluster,
               std::uint32_t size, const VsdPlan& p) {
    std::memset(e, 0, 32);
    std::memcpy(e, short_name.data(), 11);
    e[11] = attr;
    put16(e + 14, p.fat_time);
    put16(e + 16, p.fat_date);
    put16(e + 18, p.fat_date);
    put16(e + 20, cluster >> 16);
    put16(e + 22, p.fat_time);
    put16(e + 24, p.fat_date);
    put16(e + 26, cluster & 0xFFFF);
    put32(e + 28, size);
}

// A folder's entries: "." and ".." (or the volume label for the root),
// then each child's long-name entries (last part first) and 8.3 entry.
void folder_bytes(const VsdPlan& p, const VsdPlan::Node& dir, std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(dir.size), 0);
    std::uint8_t* e = out.data();
    if (dir.parent < 0) {
        dir_entry(e, "RIFTWII    ", 0x08, 0, 0, p);
        e += 32;
    } else {
        dir_entry(e, ".          ", 0x10, dir.first_cluster, 0, p);
        const VsdPlan::Node& up = p.nodes[static_cast<std::size_t>(dir.parent)];
        dir_entry(e + 32, "..         ", 0x10, up.parent < 0 ? 0 : up.first_cluster, 0, p);
        e += 64;
    }
    for (int c : dir.children) {
        const VsdPlan::Node& n = p.nodes[static_cast<std::size_t>(c)];
        if (n.long_name) {
            const std::u16string name = utf16(n.name);
            const std::uint32_t parts = static_cast<std::uint32_t>((name.size() + 12) / 13);
            const std::uint8_t sum = checksum(n.short_name);
            static const int kSlot[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (std::uint32_t k = parts; k >= 1; --k) {
                std::memset(e, 0, 32);
                e[0] = static_cast<std::uint8_t>(k | (k == parts ? 0x40 : 0));
                e[11] = 0x0F;
                e[13] = sum;
                for (int j = 0; j < 13; ++j) {
                    const std::size_t at = (k - 1) * 13 + static_cast<std::size_t>(j);
                    const std::uint32_t unit = at < name.size() ? name[at] : at == name.size() ? 0x0000 : 0xFFFF;
                    put16(e + kSlot[j], unit);
                }
                e += 32;
            }
        }
        dir_entry(e, n.short_name, n.directory ? 0x10 : 0x20, n.first_cluster,
                  n.directory ? 0 : static_cast<std::uint32_t>(n.size), p);
        e += 32;
    }
}

}  // namespace

bool write_vsd_image(const VsdPlan& plan, VsdSink& sink, const VsdRead& read, const VsdProgress& progress,
                     std::vector<std::uint8_t>& buffer, std::string& error) {
    if (plan.nodes.empty() || plan.cluster_bytes == 0) {
        error = "nothing planned";
        return false;
    }
    if (buffer.size() < std::max<std::size_t>(plan.cluster_bytes, 64 * 1024)) buffer.resize(std::max<std::size_t>(plan.cluster_bytes, 64 * 1024));
    std::uint64_t written = 0;
    std::uint64_t reported = 0;
    const auto put = [&](const std::uint8_t* data, std::size_t length) {
        if (!sink.write(data, length)) {
            error = "could not write the image (is the card full?)";
            return false;
        }
        written += length;
        return true;
    };
    const auto tell = [&](const std::string& path, bool always) {
        if (!progress) return true;
        if (!always && written - reported < (4u << 20)) return true;
        reported = written;
        if (!progress(written, path)) {
            error = "stopped";
            return false;
        }
        return true;
    };
    const auto zeros = [&](std::uint64_t length) {
        std::fill(buffer.begin(), buffer.end(), 0);
        while (length) {
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(length, buffer.size()));
            if (!put(buffer.data(), n) || !tell("", false)) return false;
            length -= n;
        }
        return true;
    };

    // The reserved sectors.
    std::vector<std::uint8_t> reserved(kVsdReservedSectors * kVsdSectorBytes, 0);
    boot_sector(plan, reserved.data());
    fsinfo_sector(plan, reserved.data() + kVsdSectorBytes);
    std::memcpy(reserved.data() + 6 * kVsdSectorBytes, reserved.data(), kVsdSectorBytes);
    std::memcpy(reserved.data() + 7 * kVsdSectorBytes, reserved.data() + kVsdSectorBytes, kVsdSectorBytes);
    if (!put(reserved.data(), reserved.size())) return false;

    // The two FATs: each run of clusters chained, its last one ending it.
    std::vector<std::uint32_t> ends;
    for (const VsdPlan::Node& n : plan.nodes)
        if (n.clusters) ends.push_back(n.first_cluster + n.clusters - 1);
    std::sort(ends.begin(), ends.end());
    const std::uint32_t last_used = plan.used_clusters + 1;
    for (int copy = 0; copy < 2; ++copy) {
        std::size_t run = 0;
        const std::uint32_t per = static_cast<std::uint32_t>(buffer.size() / 4);
        const std::uint64_t entries = std::uint64_t(plan.fat_sectors) * (kVsdSectorBytes / 4);
        for (std::uint64_t first = 0; first < entries; first += per) {
            const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::uint64_t>(per, entries - first));
            for (std::uint32_t k = 0; k < n; ++k) {
                const std::uint64_t c = first + k;
                std::uint32_t v = 0;
                if (c == 0) v = 0x0FFFFFF8;
                else if (c == 1) v = 0x0FFFFFFF;
                else if (c <= last_used) {
                    while (run < ends.size() && ends[run] < c) ++run;
                    v = (run < ends.size() && ends[run] == c) ? 0x0FFFFFFF : static_cast<std::uint32_t>(c + 1);
                }
                put32(buffer.data() + 4 * k, v);
            }
            if (!put(buffer.data(), n * 4u)) return false;
        }
    }

    // The folders and files, in cluster order.
    std::vector<std::uint8_t> folder;
    for (const VsdPlan::Node& n : plan.nodes) {
        if (!n.clusters) continue;
        const std::uint64_t room = std::uint64_t(n.clusters) * plan.cluster_bytes;
        if (n.directory) {
            folder_bytes(plan, n, folder);
            if (!put(folder.data(), folder.size()) || !zeros(room - folder.size())) return false;
            continue;
        }
        if (!tell(n.path, true)) return false;
        std::uint64_t done = 0;
        while (done < n.size) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), n.size - done));
            if (!read(n.path, done, buffer.data(), want)) {
                error = "could not read " + n.path;
                return false;
            }
            if (!put(buffer.data(), want) || !tell(n.path, false)) return false;
            done += want;
        }
        if (!zeros(room - n.size)) return false;
    }

    // The free space, to the card's end.
    const std::uint64_t end = plan.image_bytes;
    if (written > end) {
        error = "the image came out bigger than planned";
        return false;
    }
    if (!zeros(end - written)) return false;
    if (progress) progress(written, "");
    return true;
}

}  // namespace riftwii
