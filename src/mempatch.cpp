// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/mempatch.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {
namespace {

constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::size_t kSearchChunk = 0x8000;

std::string hex32(std::uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    std::string s = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) s += digits[(v >> shift) & 0xF];
    return s;
}

bool inside(const std::vector<MemoryRegion>& regions, std::uint32_t address, std::size_t length) {
    for (const MemoryRegion& r : regions) {
        if (address >= r.address && length <= r.length && address - r.address <= r.length - length) return true;
    }
    return false;
}

std::uint32_t target_of(const MemoryPatch& p) {
    return static_cast<std::uint32_t>(p.offset) | 0x80000000u;
}

bool write_checked(const std::vector<MemoryRegion>& writable, MemoryAccess& memory, std::uint32_t address,
                   const std::vector<std::uint8_t>& bytes, std::string& error) {
    if (!inside(writable, address, bytes.size())) {
        error = "memory patch writes outside RAM at " + hex32(address);
        return false;
    }
    if (!memory.write(address, bytes.data(), bytes.size())) {
        error = "memory write failed at " + hex32(address);
        return false;
    }
    return true;
}

// The first address in `loaded` (load order, `align` stride) holding
// `pattern`; the region is read in chunks that overlap by the pattern's
// length, so no whole-region copy is needed.
bool find_pattern(const std::vector<MemoryRegion>& loaded, MemoryAccess& memory, const std::vector<std::uint8_t>& pattern,
                  std::uint64_t align, std::uint32_t& found, std::size_t& region_index, std::string& error) {
    if (align == 0) align = 1;
    const std::size_t n = pattern.size();
    std::vector<std::uint8_t> chunk;
    for (std::size_t ri = 0; ri < loaded.size(); ++ri) {
        const MemoryRegion& r = loaded[ri];
        if (r.length < n) continue;
        std::uint64_t pos = r.address;
        if (pos % align != 0) pos += align - pos % align;
        const std::uint64_t last = static_cast<std::uint64_t>(r.address) + r.length - n;  // last start
        while (pos <= last) {
            // A chunk starting at `pos` covering as many candidate starts as fit.
            const std::uint64_t region_end = static_cast<std::uint64_t>(r.address) + r.length;
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(kSearchChunk + n, region_end - pos));
            chunk.resize(want);
            if (!memory.read(static_cast<std::uint32_t>(pos), chunk.data(), want)) {
                error = "memory read failed at " + hex32(static_cast<std::uint32_t>(pos));
                return false;
            }
            const std::size_t starts = want >= n ? want - n + 1 : 0;
            for (std::size_t i = 0; i < starts; i += static_cast<std::size_t>(align)) {
                if (std::memcmp(chunk.data() + i, pattern.data(), n) == 0) {
                    found = static_cast<std::uint32_t>(pos + i);
                    region_index = ri;
                    return true;
                }
            }
            if (starts == 0) break;
            // Next chunk begins at the first candidate not yet examined.
            std::uint64_t next = pos + starts;
            if (next % align != 0) next += align - next % align;
            pos = next;
        }
    }
    found = 0;
    region_index = loaded.size();
    return true;
}

bool apply_plain(const MemoryPatch& p, const std::vector<MemoryRegion>& writable, MemoryAccess& memory,
                 std::vector<std::string>& notes, std::string& error) {
    if (!p.has_offset) {
        error = "memory patch without offset";
        return false;
    }
    if (p.value.empty()) {
        error = "memory patch without value";
        return false;
    }
    const std::uint32_t address = target_of(p);
    if (!p.original.empty()) {
        if (!inside(writable, address, p.original.size())) {
            error = "memory patch reads outside RAM at " + hex32(address);
            return false;
        }
        std::vector<std::uint8_t> current(p.original.size());
        if (!memory.read(address, current.data(), current.size())) {
            error = "memory read failed at " + hex32(address);
            return false;
        }
        if (current != p.original) {
            notes.push_back("memory " + hex32(address) + ": skipped, original differs");
            return true;
        }
    }
    if (!write_checked(writable, memory, address, p.value, error)) return false;
    notes.push_back("memory " + hex32(address) + ": " + std::to_string(p.value.size()) + " bytes written");
    return true;
}

bool apply_search(const MemoryPatch& p, const std::vector<MemoryRegion>& loaded, const std::vector<MemoryRegion>& writable,
                  MemoryAccess& memory, std::vector<std::string>& notes, std::string& error) {
    if (p.original.empty()) {
        error = "memory search patch without original";
        return false;
    }
    if (p.value.empty()) {
        error = "memory search patch without value";
        return false;
    }
    std::uint32_t at = 0;
    std::size_t region = 0;
    if (!find_pattern(loaded, memory, p.original, p.align, at, region, error)) return false;
    if (region == loaded.size()) {
        notes.push_back("memory search: no match for " + std::to_string(p.original.size()) + " bytes");
        return true;
    }
    if (!write_checked(writable, memory, at, p.value, error)) return false;
    notes.push_back("memory search: " + std::to_string(p.value.size()) + " bytes written at " + hex32(at));
    return true;
}

bool apply_ocarina(const MemoryPatch& p, const std::vector<MemoryRegion>& loaded, const std::vector<MemoryRegion>& writable,
                   MemoryAccess& memory, std::vector<std::string>& notes, std::string& error) {
    if (!p.has_offset) {
        error = "ocarina patch without offset";
        return false;
    }
    if (p.value.empty()) {
        error = "ocarina patch without value";
        return false;
    }
    std::uint32_t at = 0;
    std::size_t region = 0;
    if (!find_pattern(loaded, memory, p.value, p.align, at, region, error)) return false;
    if (region == loaded.size()) {
        notes.push_back("ocarina: no match for " + std::to_string(p.value.size()) + " bytes");
        return true;
    }
    const MemoryRegion& r = loaded[region];
    const std::uint64_t region_end = static_cast<std::uint64_t>(r.address) + r.length;
    std::uint64_t scan = (static_cast<std::uint64_t>(at) + 3) & ~std::uint64_t(3);
    for (; scan + 4 <= region_end; scan += 4) {
        std::uint8_t word[4];
        if (!memory.read(static_cast<std::uint32_t>(scan), word, 4)) {
            error = "memory read failed at " + hex32(static_cast<std::uint32_t>(scan));
            return false;
        }
        const std::uint32_t v = (std::uint32_t(word[0]) << 24) | (std::uint32_t(word[1]) << 16) |
                                (std::uint32_t(word[2]) << 8) | std::uint32_t(word[3]);
        if (v != kBlr) continue;
        const std::uint32_t blr = static_cast<std::uint32_t>(scan);
        const std::uint32_t target = target_of(p);
        const std::int64_t displacement = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(blr);
        if ((target & 3) != 0 || displacement < -0x2000000 || displacement > 0x1FFFFFC) {
            error = "ocarina target " + hex32(target) + " is out of branch range from " + hex32(blr);
            return false;
        }
        const std::uint32_t branch = 0x48000000u | (static_cast<std::uint32_t>(displacement) & 0x03FFFFFCu);
        const std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>(branch >> 24), static_cast<std::uint8_t>(branch >> 16),
                                                 static_cast<std::uint8_t>(branch >> 8), static_cast<std::uint8_t>(branch)};
        if (!write_checked(writable, memory, blr, bytes, error)) return false;
        notes.push_back("ocarina: match at " + hex32(at) + ", blr at " + hex32(blr) + " -> b " + hex32(target));
        return true;
    }
    notes.push_back("ocarina: match at " + hex32(at) + " but no blr after it");
    return true;
}

}  // namespace

bool apply_memory_patches(const std::vector<MemoryPatch>& patches, const std::vector<MemoryRegion>& loaded,
                          const std::vector<MemoryRegion>& writable, MemoryAccess& memory,
                          std::vector<std::string>& notes, std::string& error) {
    for (const MemoryPatch& p : patches) {
        if (!p.valuefile.empty() && p.value.empty()) {
            error = "memory patch valuefile '" + p.valuefile + "' was not read";
            return false;
        }
        if (p.ocarina && p.search) {
            error = "memory patch is both ocarina and search";
            return false;
        }
        bool ok;
        if (p.ocarina) {
            ok = apply_ocarina(p, loaded, writable, memory, notes, error);
        } else if (p.search) {
            ok = apply_search(p, loaded, writable, memory, notes, error);
        } else {
            ok = apply_plain(p, writable, memory, notes, error);
        }
        if (!ok) return false;
    }
    error.clear();
    return true;
}

}  // namespace riftwii
