// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcmempatch.hpp"

#include <cstring>

namespace riftwii {
namespace {

// The game's memory as it will be when it starts: its sections, zeros
// around them, and the patches' writes so far on top. Writes are kept, not
// made: the loader makes them at the hand-over.
class StagedMemory final : public MemoryAccess {
public:
    StagedMemory(const std::vector<GcStagedSection>& sections, std::vector<GcMemoryWrite>& writes)
        : sections_(sections), writes_(writes) {}

    bool read(std::uint32_t address, std::uint8_t* out, std::size_t length) override {
        for (std::size_t i = 0; i < length; ++i) out[i] = byte_at(address + static_cast<std::uint32_t>(i));
        return true;
    }

    bool write(std::uint32_t address, const std::uint8_t* bytes, std::size_t length) override {
        GcMemoryWrite write;
        write.address = address;
        write.bytes.assign(bytes, bytes + length);
        writes_.push_back(std::move(write));
        return true;
    }

private:
    std::uint8_t byte_at(std::uint32_t address) const {
        // The latest write to an address is what the game will find there.
        for (auto w = writes_.rbegin(); w != writes_.rend(); ++w) {
            if (address >= w->address && address - w->address < w->bytes.size()) return w->bytes[address - w->address];
        }
        for (const GcStagedSection& s : sections_) {
            if (address >= s.address && address - s.address < s.size) return s.bytes[address - s.address];
        }
        return 0;
    }

    const std::vector<GcStagedSection>& sections_;
    std::vector<GcMemoryWrite>& writes_;
};

}  // namespace

bool plan_gc_memory_writes(const std::vector<MemoryPatch>& patches, const std::vector<GcStagedSection>& sections,
                           const std::vector<MemoryRegion>& writable, std::vector<GcMemoryWrite>& writes,
                           std::vector<std::string>& notes, std::string& error) {
    writes.clear();
    std::vector<MemoryRegion> loaded;
    for (const GcStagedSection& s : sections) {
        if (s.size != 0) loaded.push_back(MemoryRegion{s.address, static_cast<std::uint32_t>(s.size)});
    }
    StagedMemory memory(sections, writes);
    return apply_memory_patches(patches, loaded, writable, memory, notes, error);
}

}  // namespace riftwii
