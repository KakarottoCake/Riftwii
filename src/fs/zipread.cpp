// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/zipread.hpp"

#include <algorithm>
#include <cctype>

#include "riftwii/themepack.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kEndSignature = 0x06054B50;
constexpr std::uint32_t kCentralSignature = 0x02014B50;
constexpr std::uint32_t kLocalSignature = 0x04034B50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kCentralSize = 46;
constexpr std::size_t kLocalSize = 30;
constexpr std::size_t kMaxNameBytes = 200;
// How much of a stored entry is handed to the sink at once.
constexpr std::size_t kChunk = 16384;

std::uint16_t Le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t Le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

bool Fail(std::string& error, const char* why) {
    error = why;
    return false;
}

std::string Lower(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

std::uint32_t zip_crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    std::uint32_t c = crc ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool zip_read_directory(const std::uint8_t* data, std::size_t size, std::vector<ZipEntry>& entries,
                        std::string& error, std::size_t max_entries) {
    entries.clear();
    if (size < kEndSize) return Fail(error, "the download is too short to be a zip file");
    // The end record is the last thing in the file, followed by at most a
    // 65535-byte comment.
    const std::size_t window = std::min<std::size_t>(size, kEndSize + 65535);
    std::size_t end = 0;
    bool found = false;
    for (std::size_t back = 0; back + kEndSize <= window; ++back) {
        end = size - kEndSize - back;
        if (Le32(data + end) == kEndSignature) {
            found = true;
            break;
        }
    }
    if (!found) return Fail(error, "the download is not a zip file");
    const unsigned disk = Le16(data + end + 4), directory_disk = Le16(data + end + 6);
    const unsigned here = Le16(data + end + 8), total = Le16(data + end + 10);
    const std::uint32_t directory_size = Le32(data + end + 12), directory_offset = Le32(data + end + 16);
    if (disk != 0 || directory_disk != 0 || here != total) return Fail(error, "split zip files are not supported");
    if (total == 0xFFFF || directory_size == 0xFFFFFFFFu || directory_offset == 0xFFFFFFFFu)
        return Fail(error, "zip64 files are not supported");
    if (total > max_entries) return Fail(error, "the zip file has too many entries");
    if (directory_offset > end || directory_size > end - directory_offset)
        return Fail(error, "the zip file's directory is outside the file");

    std::size_t at = directory_offset;
    const std::size_t directory_end = std::size_t(directory_offset) + directory_size;
    for (unsigned i = 0; i < total; ++i) {
        if (directory_end - at < kCentralSize || Le32(data + at) != kCentralSignature)
            return Fail(error, "the zip file's directory is damaged");
        const std::uint8_t* h = data + at;
        const unsigned flags = Le16(h + 8);
        ZipEntry entry;
        entry.method = Le16(h + 10);
        entry.crc = Le32(h + 16);
        entry.packed_size = Le32(h + 20);
        entry.unpacked_size = Le32(h + 24);
        const std::size_t name_size = Le16(h + 28), extra_size = Le16(h + 30), comment_size = Le16(h + 32);
        const std::uint32_t local_offset = Le32(h + 42);
        const std::size_t record = kCentralSize + name_size + extra_size + comment_size;
        if (directory_end - at < record || name_size > kMaxNameBytes)
            return Fail(error, "the zip file's directory is damaged");
        if (flags & 1) return Fail(error, "encrypted zip files are not supported");
        if (entry.packed_size == 0xFFFFFFFFu || entry.unpacked_size == 0xFFFFFFFFu || local_offset == 0xFFFFFFFFu)
            return Fail(error, "zip64 files are not supported");
        entry.name.assign(reinterpret_cast<const char*>(h + kCentralSize), name_size);
        entry.directory = !entry.name.empty() && entry.name.back() == '/';
        at += record;
        if (entry.directory) {
            if (entry.unpacked_size != 0) return Fail(error, "the zip file has a folder with data in it");
            entries.push_back(std::move(entry));
            continue;
        }
        if (entry.method != 0 && entry.method != 8)
            return Fail(error, "the zip file uses a compression RiftWii does not read");
        if (entry.method == 0 && entry.packed_size != entry.unpacked_size)
            return Fail(error, "the zip file's directory is damaged");
        // The local header repeats the name and has its own extra field; the
        // packed bytes follow it, and must lie inside the file, before the directory.
        if (local_offset > directory_offset || directory_offset - local_offset < kLocalSize ||
            Le32(data + local_offset) != kLocalSignature)
            return Fail(error, "the zip file's directory is damaged");
        const std::size_t local_name = Le16(data + local_offset + 26), local_extra = Le16(data + local_offset + 28);
        const std::size_t data_start = std::size_t(local_offset) + kLocalSize + local_name + local_extra;
        if (data_start > directory_offset || entry.packed_size > directory_offset - data_start)
            return Fail(error, "the zip file's directory is damaged");
        entry.data_offset = data_start;
        entries.push_back(std::move(entry));
    }
    return true;
}

bool zip_theme_path_ok(const std::string& name, const std::string& folder, std::string& file) {
    if (name.empty() || name.size() > kMaxNameBytes || folder.empty()) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '\\' || c == ':') return false;
    }
    if (name[0] == '/') return false;
    const std::string::size_type slash = name.find('/');
    if (slash == std::string::npos || name.find('/', slash + 1) != std::string::npos) return false;
    if (name.compare(0, slash, folder) != 0 || slash != folder.size()) return false;
    const std::string part = name.substr(slash + 1);
    // Plain names only: this also leaves out "", ".", ".." and hidden files.
    if (!theme_pack_name_ok(part, true)) return false;
    file = part;
    return true;
}

bool zip_plan_theme(const std::vector<ZipEntry>& entries, const std::string& folder, const ZipThemeLimits& limits,
                    std::vector<ZipThemeFile>& plan, std::string& error) {
    plan.clear();
    std::uint64_t total = 0;
    std::vector<std::string> seen;
    bool has_ini = false;
    const std::size_t max_files = std::min(limits.files, limits.max_files);
    const std::uint64_t max_total = std::min(limits.unpacked_size, limits.max_unpacked_size);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        if (entry.directory) {
            // Only the theme's own folder may be named.
            if (entry.name.substr(0, entry.name.size() - 1) != folder)
                return Fail(error, "the zip file holds a path outside the theme's folder");
            continue;
        }
        std::string file;
        if (!zip_theme_path_ok(entry.name, folder, file))
            return Fail(error, "the zip file holds a path outside the theme's folder");
        const std::string key = Lower(file);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            return Fail(error, "the zip file holds one file name twice");
        seen.push_back(key);
        if (entry.unpacked_size > limits.max_file_size) return Fail(error, "a file in the zip is too big");
        total += entry.unpacked_size;
        if (plan.size() + 1 > max_files) return Fail(error, "the zip file holds more files than the theme list says");
        if (total > max_total) return Fail(error, "the zip file unpacks to more than the theme list says");
        if (key == "theme.ini") has_ini = true;
        plan.push_back({file, i});
    }
    if (!has_ini) return Fail(error, "the zip file has no theme.ini");
    return true;
}

bool zip_extract(const std::uint8_t* data, std::size_t size, const ZipEntry& entry, ZipInflater inflate,
                 const ZipSink& sink, std::string& error) {
    if (entry.data_offset > size || entry.packed_size > size - entry.data_offset)
        return Fail(error, "the zip file's directory is damaged");
    const std::uint8_t* packed = data + entry.data_offset;
    std::uint64_t produced = 0;
    std::uint32_t crc = 0;
    bool sink_failed = false;
    const ZipSink counted = [&](const std::uint8_t* bytes, std::size_t count) {
        if (produced + count > entry.unpacked_size) return false;  // more than it said
        crc = zip_crc32_update(crc, bytes, count);
        produced += count;
        if (!sink(bytes, count)) {
            sink_failed = true;
            return false;
        }
        return true;
    };
    bool ran = true;
    if (entry.method == 0) {
        for (std::size_t at = 0; ran && at < entry.packed_size; at += kChunk)
            ran = counted(packed + at, std::min<std::size_t>(kChunk, entry.packed_size - at));
    } else {
        ran = inflate && inflate(packed, entry.packed_size, entry.unpacked_size, counted);
    }
    if (sink_failed) return Fail(error, "a file could not be written");
    if (!ran) return Fail(error, "a file in the zip is damaged");
    if (produced != entry.unpacked_size) return Fail(error, "a file in the zip is not the size it says");
    if (crc != entry.crc) return Fail(error, "a file in the zip is damaged");
    return true;
}

}  // namespace riftwii
