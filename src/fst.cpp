// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/fst.hpp"

#include <utility>

namespace riftwii {
namespace {

constexpr std::size_t kEntryBytes = 12;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t be24(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) |
           static_cast<std::uint32_t>(p[2]);
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

char fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool names_equal(const std::string& a, const std::string& b, bool case_insensitive) {
    if (a.size() != b.size()) return false;
    if (!case_insensitive) return a == b;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

bool valid_name(const std::string& name, std::uint32_t max_name) {
    if (name.empty() || name.size() > max_name) return false;
    if (name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if (c == '/' || c == '\\' || c == 0) return false;
    }
    return true;
}

// Encodes a file's byte offset into the 32-bit on-disc field.
bool encode_offset(bool wii, std::uint64_t offset, std::uint32_t& out) {
    if (wii) {
        if ((offset & 3u) != 0) return false;
        const std::uint64_t words = offset >> 2;
        if (words > 0xFFFFFFFFull) return false;
        out = static_cast<std::uint32_t>(words);
        return true;
    }
    if (offset > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(offset);
    return true;
}

}  // namespace

bool Fst::parse(const std::uint8_t* data, std::size_t size, bool wii_offsets, Fst& out,
                std::string& error, const FstLimits& limits) {
    if (data == nullptr || size < kEntryBytes) {
        error = "fst image too small";
        return false;
    }
    if (data[0] != 1) {
        error = "fst root entry is not a directory";
        return false;
    }
    const std::uint32_t count = be32(data + 8);
    if (count == 0) {
        error = "fst entry count is zero";
        return false;
    }
    if (count > limits.max_entries) {
        error = "fst entry count exceeds limit";
        return false;
    }
    if (static_cast<std::uint64_t>(count) * kEntryBytes > size) {
        error = "fst entry count exceeds image size";
        return false;
    }
    const std::size_t table_start = static_cast<std::size_t>(count) * kEntryBytes;
    const std::size_t table_size = size - table_start;
    const std::uint8_t* table = data + table_start;

    std::vector<FstEntry> entries;
    try {
        entries.reserve(count);
    } catch (...) {
        error = "allocation failure";
        return false;
    }
    // Enclosing directories: (index, first index after its subtree).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> stack;
    stack.push_back({0, count});
    try {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint8_t* e = data + static_cast<std::size_t>(i) * kEntryBytes;
            const std::uint8_t flags = e[0];
            if (flags != 0 && flags != 1) {
                error = "fst entry " + std::to_string(i) + " has invalid type";
                return false;
            }
            const std::uint32_t name_offset = be24(e + 1);
            const std::uint32_t a = be32(e + 4);
            const std::uint32_t b = be32(e + 8);
            FstEntry entry;
            if (i == 0) {
                entry.is_directory = true;
                entry.parent = 0;
                entry.next = count;
                entries.push_back(entry);
                continue;
            }
            while (i >= stack.back().second) stack.pop_back();
            entry.parent = stack.back().first;
            if (name_offset >= table_size) {
                error = "fst entry " + std::to_string(i) + " name offset outside string table";
                return false;
            }
            std::size_t len = 0;
            while (name_offset + len < table_size && table[name_offset + len] != 0) ++len;
            if (name_offset + len >= table_size) {
                error = "fst entry " + std::to_string(i) + " name is not terminated";
                return false;
            }
            entry.name.assign(reinterpret_cast<const char*>(table + name_offset), len);
            if (!valid_name(entry.name, limits.max_name)) {
                error = "fst entry " + std::to_string(i) + " has an invalid name";
                return false;
            }
            if (flags == 1) {
                entry.is_directory = true;
                entry.next = b;
                if (entry.next <= i || entry.next > stack.back().second) {
                    error = "fst directory " + std::to_string(i) + " has an invalid subtree range";
                    return false;
                }
                stack.push_back({i, entry.next});
                if (stack.size() > static_cast<std::size_t>(limits.max_depth) + 1) {
                    error = "fst nesting depth exceeds limit";
                    return false;
                }
            } else {
                entry.offset = wii_offsets ? (static_cast<std::uint64_t>(a) << 2) : a;
                entry.size = b;
            }
            entries.push_back(std::move(entry));
        }
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    }
    out.entries_ = std::move(entries);
    out.wii_offsets_ = wii_offsets;
    out.limits_ = limits;
    error.clear();
    return true;
}

bool Fst::serialize(std::vector<std::uint8_t>& out, std::string& error) const {
    if (entries_.empty()) {
        error = "fst is empty";
        return false;
    }
    try {
        std::vector<std::uint8_t> table;
        std::vector<std::uint32_t> name_offsets(entries_.size(), 0);
        for (std::size_t i = 1; i < entries_.size(); ++i) {
            name_offsets[i] = static_cast<std::uint32_t>(table.size());
            table.insert(table.end(), entries_[i].name.begin(), entries_[i].name.end());
            table.push_back(0);
            if (table.size() > 0xFFFFFFu) {
                error = "fst string table exceeds 24-bit addressing";
                return false;
            }
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(entries_.size() * kEntryBytes + table.size());
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const FstEntry& e = entries_[i];
            const std::uint32_t name_offset = name_offsets[i];
            bytes.push_back(e.is_directory ? 1 : 0);
            bytes.push_back(static_cast<std::uint8_t>(name_offset >> 16));
            bytes.push_back(static_cast<std::uint8_t>(name_offset >> 8));
            bytes.push_back(static_cast<std::uint8_t>(name_offset));
            if (e.is_directory) {
                put32(bytes, i == 0 ? 0u : e.parent);
                put32(bytes, i == 0 ? static_cast<std::uint32_t>(entries_.size()) : e.next);
            } else {
                std::uint32_t encoded = 0;
                if (!encode_offset(wii_offsets_, e.offset, encoded)) {
                    error = "fst entry " + std::to_string(i) + " offset cannot be encoded";
                    return false;
                }
                put32(bytes, encoded);
                put32(bytes, e.size);
            }
        }
        bytes.insert(bytes.end(), table.begin(), table.end());
        out = std::move(bytes);
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    }
    error.clear();
    return true;
}

bool Fst::children(std::uint32_t directory, std::vector<std::uint32_t>& out) const {
    if (directory >= entries_.size() || !entries_[directory].is_directory) return false;
    std::vector<std::uint32_t> result;
    std::uint32_t i = directory + 1;
    const std::uint32_t end = entries_[directory].next;
    while (i < end && i < entries_.size()) {
        result.push_back(i);
        i = entries_[i].is_directory ? entries_[i].next : i + 1;
    }
    out = std::move(result);
    return true;
}

std::uint32_t Fst::find(const std::string& absolute_path, bool case_insensitive) const {
    if (entries_.empty() || absolute_path.empty() || absolute_path[0] != '/') return npos;
    if (absolute_path == "/") return 0;
    std::uint32_t current = 0;
    std::size_t pos = 1;
    while (pos <= absolute_path.size()) {
        std::size_t end = absolute_path.find('/', pos);
        if (end == std::string::npos) end = absolute_path.size();
        const std::string segment = absolute_path.substr(pos, end - pos);
        if (segment.empty()) return npos;  // "//" or trailing '/'
        const bool last = end == absolute_path.size();
        std::vector<std::uint32_t> kids;
        if (!children(current, kids)) return npos;
        std::uint32_t match = npos;
        for (std::uint32_t k : kids) {
            if (names_equal(entries_[k].name, segment, case_insensitive)) {
                match = k;
                break;
            }
        }
        if (match == npos) return npos;
        if (last) return match;
        if (!entries_[match].is_directory) return npos;
        current = match;
        pos = end + 1;
    }
    return npos;
}

std::vector<std::uint32_t> Fst::find_files_named(const std::string& name, bool case_insensitive) const {
    std::vector<std::uint32_t> result;
    for (std::uint32_t i = 1; i < entries_.size(); ++i) {
        if (!entries_[i].is_directory && names_equal(entries_[i].name, name, case_insensitive)) {
            result.push_back(i);
        }
    }
    return result;
}

bool Fst::path_of(std::uint32_t index, std::string& out) const {
    if (index >= entries_.size()) return false;
    if (index == 0) {
        out = "/";
        return true;
    }
    std::vector<const std::string*> parts;
    std::uint32_t current = index;
    std::uint32_t guard = 0;
    while (current != 0) {
        parts.push_back(&entries_[current].name);
        current = entries_[current].parent;
        if (current >= entries_.size() || ++guard > limits_.max_depth + 1) return false;
    }
    std::string path;
    for (std::size_t i = parts.size(); i > 0; --i) {
        path += '/';
        path += *parts[i - 1];
    }
    out = std::move(path);
    return true;
}

bool Fst::patch_image(std::vector<std::uint8_t>& image, std::string& error) const {
    if (entries_.empty() || image.size() < entries_.size() * kEntryBytes || be32(image.data() + 8) != entries_.size()) {
        error = "fst image does not match the table";
        return false;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        std::uint8_t* e = image.data() + i * kEntryBytes;
        if ((e[0] == 1) != entries_[i].is_directory) {
            error = "fst image entry " + std::to_string(i) + " does not match the table";
            return false;
        }
        if (entries_[i].is_directory) continue;
        std::uint32_t encoded = 0;
        if (!encode_offset(wii_offsets_, entries_[i].offset, encoded)) {
            error = "fst entry " + std::to_string(i) + " offset cannot be encoded";
            return false;
        }
        e[4] = static_cast<std::uint8_t>(encoded >> 24);
        e[5] = static_cast<std::uint8_t>(encoded >> 16);
        e[6] = static_cast<std::uint8_t>(encoded >> 8);
        e[7] = static_cast<std::uint8_t>(encoded);
        e[8] = static_cast<std::uint8_t>(entries_[i].size >> 24);
        e[9] = static_cast<std::uint8_t>(entries_[i].size >> 16);
        e[10] = static_cast<std::uint8_t>(entries_[i].size >> 8);
        e[11] = static_cast<std::uint8_t>(entries_[i].size);
    }
    error.clear();
    return true;
}

bool Fst::set_file_extent(std::uint32_t index, std::uint64_t offset, std::uint32_t size, std::string& error) {
    if (index >= entries_.size() || entries_[index].is_directory) {
        error = "fst index is not a file";
        return false;
    }
    std::uint32_t encoded = 0;
    if (!encode_offset(wii_offsets_, offset, encoded)) {
        error = "fst file offset cannot be encoded";
        return false;
    }
    entries_[index].offset = offset;
    entries_[index].size = size;
    return true;
}

bool Fst::insert_entry(std::uint32_t directory, FstEntry entry, std::uint32_t& index, std::string& error) {
    if (directory >= entries_.size() || !entries_[directory].is_directory) {
        error = "fst target is not a directory";
        return false;
    }
    if (!valid_name(entry.name, limits_.max_name)) {
        error = "fst name is invalid";
        return false;
    }
    if (entries_.size() + 1 > limits_.max_entries) {
        error = "fst entry count exceeds limit";
        return false;
    }
    const std::uint32_t pos = entries_[directory].next;  // end of the subtree
    // Ancestors (including `directory`) grow by one; every other range or
    // reference at or after `pos` shifts by one. The walk also measures the
    // nesting depth so a new directory cannot exceed the parse limit.
    std::vector<bool> ancestor(entries_.size(), false);
    std::uint32_t depth = 0;  // number of directories enclosing the new entry
    {
        std::uint32_t current = directory;
        while (true) {
            ancestor[current] = true;
            ++depth;
            if (current == 0) break;
            current = entries_[current].parent;
            if (current >= entries_.size() || depth > limits_.max_depth + 1) {
                error = "fst parent chain is corrupt";
                return false;
            }
        }
    }
    if (entry.is_directory && depth > limits_.max_depth) {
        error = "fst nesting depth exceeds limit";
        return false;
    }
    try {
        // Reserve first so the insert below cannot fail after the fix-ups.
        entries_.reserve(entries_.size() + 1);
        for (std::size_t j = 0; j < entries_.size(); ++j) {
            FstEntry& e = entries_[j];
            if (e.is_directory) {
                if (ancestor[j] || e.next > pos) e.next += 1;
            }
            if (j != 0 && e.parent >= pos) e.parent += 1;
        }
        entry.parent = directory;
        if (entry.is_directory) entry.next = pos + 1;
        entries_.insert(entries_.begin() + pos, std::move(entry));
    } catch (const std::bad_alloc&) {
        error = "allocation failure";
        return false;
    }
    index = pos;
    error.clear();
    return true;
}

bool Fst::add_file(std::uint32_t directory, const std::string& name, std::uint64_t offset,
                   std::uint32_t size, std::uint32_t& index, std::string& error) {
    std::uint32_t encoded = 0;
    if (!encode_offset(wii_offsets_, offset, encoded)) {
        error = "fst file offset cannot be encoded";
        return false;
    }
    FstEntry entry;
    entry.is_directory = false;
    entry.name = name;
    entry.offset = offset;
    entry.size = size;
    return insert_entry(directory, std::move(entry), index, error);
}

bool Fst::add_directory(std::uint32_t directory, const std::string& name, std::uint32_t& index,
                        std::string& error) {
    FstEntry entry;
    entry.is_directory = true;
    entry.name = name;
    return insert_entry(directory, std::move(entry), index, error);
}

bool Fst::create_file(const std::string& absolute_path, std::uint64_t offset, std::uint32_t size,
                      std::uint32_t& index, std::string& error) {
    if (entries_.empty() || absolute_path.size() < 2 || absolute_path[0] != '/' || absolute_path.back() == '/') {
        error = "fst path '" + absolute_path + "' is not an absolute file path";
        return false;
    }
    // Every segment is checked before anything is inserted, so a bad path
    // leaves the table untouched.
    std::vector<std::string> segments;
    for (std::size_t pos = 1; pos <= absolute_path.size();) {
        std::size_t end = absolute_path.find('/', pos);
        if (end == std::string::npos) end = absolute_path.size();
        segments.push_back(absolute_path.substr(pos, end - pos));
        if (!valid_name(segments.back(), limits_.max_name)) {
            error = "fst path '" + absolute_path + "' has an invalid name";
            return false;
        }
        pos = end + 1;
    }
    std::uint32_t current = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const std::string& segment = segments[i];
        const bool last = i + 1 == segments.size();
        std::vector<std::uint32_t> kids;
        if (!children(current, kids)) {
            error = "fst directory is corrupt";
            return false;
        }
        std::uint32_t match = npos;
        for (std::uint32_t k : kids) {
            if (names_equal(entries_[k].name, segment, true)) {
                match = k;
                break;
            }
        }
        if (last) {
            if (match != npos) {
                error = "fst path '" + absolute_path + "' already exists";
                return false;
            }
            return add_file(current, segment, offset, size, index, error);
        }
        if (match == npos) {
            if (!add_directory(current, segment, match, error)) return false;
        } else if (!entries_[match].is_directory) {
            error = "fst path '" + absolute_path + "' crosses a file";
            return false;
        }
        current = match;
    }
    error = "fst path is empty";
    return false;
}

}  // namespace riftwii
