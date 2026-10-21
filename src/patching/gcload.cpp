// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcload.hpp"

namespace riftwii {
namespace {

class Words {
public:
    explicit Words(const std::vector<CodeRange>& text) : text_(text) {}

    bool has(std::uint32_t address) const { return find(address) != nullptr; }

    std::uint32_t at(std::uint32_t address) const {
        const std::uint8_t* bytes = find(address);
        if (bytes == nullptr) return 0;
        return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
               (std::uint32_t(bytes[2]) << 8) | bytes[3];
    }

private:
    const std::uint8_t* find(std::uint32_t address) const {
        for (const CodeRange& range : text_) {
            if (address >= range.address && range.size >= 4 &&
                address - range.address <= range.size - 4) {
                return range.bytes + (address - range.address);
            }
        }
        return nullptr;
    }

    const std::vector<CodeRange>& text_;
};

constexpr std::uint32_t kBlr = 0x4E800020;

std::int32_t displacement(std::uint32_t word) { return static_cast<std::int16_t>(word & 0xFFFF); }

bool loads_small_data(std::uint32_t word, unsigned destination) {
    return (word & 0xFFFF0000u) == (0x800D0000u | (destination << 21));
}

std::uint32_t bl_target(std::uint32_t address, std::uint32_t word) {
    if ((word & 0xFC000003u) != 0x48000001u) return 0;
    std::int32_t offset = static_cast<std::int32_t>(word & 0x03FFFFFC);
    if (offset & 0x02000000) offset -= 0x04000000;
    return address + static_cast<std::uint32_t>(offset);
}

// The leaf exchange setter returns the old heap and stores r3 in its place.
bool exchanges_heap(const Words& code, std::uint32_t function, std::int32_t offset) {
    if (function == 0 || !code.has(function + 12)) return false;
    const std::uint32_t low = static_cast<std::uint16_t>(offset);
    return code.at(function) == (0x800D0000u | low) &&
           code.at(function + 4) == (0x906D0000u | low) &&
           code.at(function + 8) == 0x7C030378u && code.at(function + 12) == kBlr;
}

// One constructor block: compare the selected heap with root, skip its
// replacement if unequal, otherwise pass the new heap to its setter.
bool root_comparison(const Words& code, std::uint32_t address, std::int32_t& heap, std::int32_t& root) {
    if (!code.has(address + 20) || !loads_small_data(code.at(address), 3) ||
        !loads_small_data(code.at(address + 4), 0) || code.at(address + 8) != 0x7C030040u ||
        code.at(address + 12) != 0x4082000Cu || code.at(address + 16) != 0x80610008u) {
        return false;
    }
    heap = displacement(code.at(address));
    root = displacement(code.at(address + 4));
    const std::uint32_t setter = bl_target(address + 20, code.at(address + 20));
    return exchanges_heap(code, setter, heap);
}

}  // namespace

GcLoadHeaps find_gc_load_heaps(const std::vector<CodeRange>& text, std::uint32_t group,
                              std::int32_t current_offset) {
    const Words code(text);
    GcLoadHeaps out;
    if (group == 0 || current_offset % 4 != 0) return out;

    // Only the group function's heap arguments count. Stack restores and
    // the byte holding the old group are not heap globals.
    unsigned main_heaps = 0;
    std::uint32_t change_group = 0;
    bool group_ended = false;
    for (std::uint32_t address = group; address < group + 0x80 && code.has(address); address += 4) {
        const std::uint32_t word = code.at(address);
        if (word == kBlr) {
            group_ended = true;
            break;
        }
        if (!loads_small_data(word, 3)) continue;
        std::uint32_t called = 0;
        for (unsigned step = 4; step <= 12 && code.has(address + step); step += 4) {
            called = bl_target(address + step, code.at(address + step));
            if (called != 0) break;
        }
        if (called == 0 || !code.has(called) || main_heaps == 2) return GcLoadHeaps{};
        if (change_group != 0 && change_group != called) return GcLoadHeaps{};
        change_group = called;
        out.offsets[main_heaps++] = displacement(word);
    }
    if (!group_ended || main_heaps != 2) return GcLoadHeaps{};

    unsigned constructors = 0;
    for (const CodeRange& range : text) {
        for (std::size_t offset = 0; offset + 4 <= range.size; offset += 4) {
            const std::uint32_t address = range.address + static_cast<std::uint32_t>(offset);
            if (!loads_small_data(code.at(address), 3)) continue;
            std::int32_t current = 0;
            std::int32_t root = 0;
            std::int32_t system = 0;
            std::int32_t system_root = 0;
            if (!root_comparison(code, address, current, root) || current != current_offset ||
                !root_comparison(code, address - 24, system, system_root) || system_root != root) {
                continue;
            }
            out.offsets[2] = system;
            out.offsets[3] = root;
            ++constructors;
        }
    }
    if (constructors != 1) return GcLoadHeaps{};

    // Reject aliases and unaligned globals instead of misclassifying a
    // temporary heap when a similar instruction sequence was found.
    for (unsigned i = 0; i < out.offsets.size(); ++i) {
        if (out.offsets[i] % 4 != 0 || out.offsets[i] == current_offset) return GcLoadHeaps{};
        for (unsigned j = 0; j < i; ++j) {
            if (out.offsets[i] == out.offsets[j]) return GcLoadHeaps{};
        }
    }
    out.found = true;
    return out;
}

}  // namespace riftwii
