// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The heap blocks a plugin's launch holds, so they can be given back when
// the launch leaves through escape() (rwpruntime.hpp), which unwinds
// nothing: no destructor runs for the launch's vectors and strings. The
// runtime's operator new and malloc add every block they hand out while
// tracking is on, delete and free take it out again, and after an escape
// sweep() frees what is left. Plain code over a table the caller provides
// (no allocation of its own, so it can run after an allocation failed);
// tests/plugin_tests.cpp checks it on the host.
//
// Linear probing with deletion by shifting the entries after a freed one
// back (Knuth's algorithm R), so the table holds only live blocks: any
// number of allocations and frees fit, as long as no more than three
// quarters of the slots are live at once. A block that does not fit is
// refused (add() returns false) and must not be handed out: every block a
// launch holds is in the table.
namespace riftwii::plugin {

class BlockTracker {
public:
    // Tracks into `slots` (`count` of them, a power of two).
    void start(void** slots, std::uint32_t count) {
        slots_ = slots;
        count_ = count;
        live_ = 0;
        for (std::uint32_t i = 0; i < count; ++i) slots_[i] = nullptr;
    }
    void stop() {
        slots_ = nullptr;
        count_ = 0;
    }
    bool active() const { return slots_ != nullptr; }
    std::uint32_t live() const { return live_; }

    // False when tracking is on and the table has no room: the caller gives
    // the block back instead of using it.
    bool add(void* block) {
        if (slots_ == nullptr || block == nullptr) return true;
        if (live_ >= count_ / 4 * 3) return false;
        std::uint32_t i = first(block);
        while (slots_[i] != nullptr) i = next(i);
        slots_[i] = block;
        ++live_;
        return true;
    }
    // A block not in the table (from before tracking, or from RiftWii's own
    // code) is left alone.
    void remove(void* block) {
        if (slots_ == nullptr || block == nullptr) return;
        std::uint32_t i = first(block);
        while (slots_[i] != block) {
            if (slots_[i] == nullptr) return;
            i = next(i);
        }
        slots_[i] = nullptr;
        --live_;
        // Close the gap: an entry further along whose home slot is not
        // cyclically within (i, j] moves into it, so every probe from a
        // home slot still reaches its block before an empty slot.
        for (std::uint32_t j = next(i); slots_[j] != nullptr; j = next(j)) {
            const std::uint32_t home = first(slots_[j]);
            const bool stays = i <= j ? (i < home && home <= j) : (i < home || home <= j);
            if (stays) continue;
            slots_[i] = slots_[j];
            slots_[j] = nullptr;
            i = j;
        }
    }
    // Every block still held, through `release`; tracking stops. Returns how
    // many.
    template <typename Release>
    std::uint32_t sweep(Release release) {
        std::uint32_t freed = 0;
        void** const slots = slots_;
        const std::uint32_t count = count_;
        stop();  // release() may reach remove(): nothing is looked up any more
        for (std::uint32_t i = 0; slots != nullptr && i < count; ++i) {
            if (slots[i] == nullptr) continue;
            release(slots[i]);
            ++freed;
        }
        return freed;
    }

private:
    std::uint32_t first(void* block) const {
        const auto bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block) >> 3);
        return (bits * 2654435761u) & (count_ - 1);
    }
    std::uint32_t next(std::uint32_t i) const { return (i + 1) & (count_ - 1); }

    void** slots_ = nullptr;
    std::uint32_t count_ = 0;
    std::uint32_t live_ = 0;
};

}  // namespace riftwii::plugin
