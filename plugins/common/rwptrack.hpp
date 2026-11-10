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
namespace riftwii::plugin {

class BlockTracker {
public:
    // Tracks into `slots` (`count` of them, a power of two). A table that
    // fills up to three quarters stops taking new blocks (overflowed()):
    // those are not swept.
    void start(void** slots, std::uint32_t count) {
        slots_ = slots;
        count_ = count;
        occupied_ = 0;
        live_ = 0;
        overflowed_ = false;
        for (std::uint32_t i = 0; i < count; ++i) slots_[i] = nullptr;
    }
    void stop() {
        slots_ = nullptr;
        count_ = 0;
    }
    bool active() const { return slots_ != nullptr; }
    bool overflowed() const { return overflowed_; }
    std::uint32_t live() const { return live_; }

    void add(void* block) {
        if (slots_ == nullptr || block == nullptr) return;
        if (occupied_ >= count_ / 4 * 3) {
            overflowed_ = true;
            return;
        }
        for (std::uint32_t i = first(block);; i = (i + 1) & (count_ - 1)) {
            if (slots_[i] == nullptr || slots_[i] == gone()) {
                if (slots_[i] == nullptr) ++occupied_;
                slots_[i] = block;
                ++live_;
                return;
            }
        }
    }
    // A block not in the table (from before tracking, or from RiftWii's own
    // code) is left alone.
    void remove(void* block) {
        if (slots_ == nullptr || block == nullptr) return;
        for (std::uint32_t i = first(block); slots_[i] != nullptr; i = (i + 1) & (count_ - 1)) {
            if (slots_[i] == block) {
                slots_[i] = gone();
                --live_;
                return;
            }
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
            if (slots[i] == nullptr || slots[i] == gone()) continue;
            release(slots[i]);
            ++freed;
        }
        return freed;
    }

private:
    static void* gone() { return reinterpret_cast<void*>(std::uintptr_t(1)); }
    std::uint32_t first(void* block) const {
        const auto bits = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block) >> 3);
        return (bits * 2654435761u) & (count_ - 1);
    }

    void** slots_ = nullptr;
    std::uint32_t count_ = 0;
    std::uint32_t occupied_ = 0;  // live and removed slots: a probe ends only at an empty one
    std::uint32_t live_ = 0;
    bool overflowed_ = false;
};

}  // namespace riftwii::plugin
