// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <new>
#include <vector>

// Where the native CTGP-R launch keeps its big buffers (docs/MEMORY.md).
//
// The launch holds BOOT.BIN and BOAT.BIN twice (as read, and linked), the
// record table, the page table and the package and hash lists: several
// megabytes that the default heap would take from MEM1 first. This file is the
// platform-free part of keeping them in MEM2 instead: an arena the Wii side
// fills with one MEM2 block (wii/launch/ctgpr.cpp), a small first-fit
// allocator inside it, and a std allocator that draws from it. Without an
// arena (the host tests, or no MEM2 to be had) the allocator is plain
// operator new, so nothing else changes.
namespace riftwii {

class CtgpArena {
public:
    static constexpr std::size_t kAlign = 32;

    // Hands out [base, base + size) (`base` 32-byte aligned). Nothing may be
    // live from an earlier attach.
    void attach(void* base, std::size_t size) {
        base_ = static_cast<std::uint8_t*>(base);
        size_ = size / kAlign * kAlign;
        free_.clear();
        used_.clear();
        in_use_ = 0;
        peak_ = 0;
        if (size_ != 0) free_[0] = size_;
    }
    // Takes the block back once nothing is live in it; null while something is.
    void* detach() {
        if (!used_.empty()) return nullptr;
        void* block = base_;
        base_ = nullptr;
        size_ = 0;
        free_.clear();
        return block;
    }
    bool attached() const { return base_ != nullptr; }

    // A block of `bytes` (rounded up to 32), or null when no free piece is big
    // enough.
    void* allocate(std::size_t bytes) {
        if (base_ == nullptr || bytes == 0 || bytes > size_) return nullptr;
        const std::size_t need = (bytes + kAlign - 1) / kAlign * kAlign;
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            if (it->second < need) continue;
            const std::size_t offset = it->first;
            const std::size_t left = it->second - need;
            free_.erase(it);
            if (left != 0) free_[offset + need] = left;
            used_[offset] = need;
            in_use_ += need;
            if (in_use_ > peak_) peak_ = in_use_;
            return base_ + offset;
        }
        return nullptr;
    }
    bool owns(const void* p) const {
        const std::uint8_t* b = static_cast<const std::uint8_t*>(p);
        return base_ != nullptr && b >= base_ && b < base_ + size_;
    }
    // Gives a block from allocate back; neighbours that are free join up.
    void release(void* p) {
        const std::size_t offset = static_cast<std::size_t>(static_cast<std::uint8_t*>(p) - base_);
        const auto found = used_.find(offset);
        if (found == used_.end()) return;
        std::size_t start = offset;
        std::size_t length = found->second;
        used_.erase(found);
        in_use_ -= length;
        auto next = free_.lower_bound(start);
        if (next != free_.end() && start + length == next->first) {
            length += next->second;
            next = free_.erase(next);
        }
        if (next != free_.begin()) {
            auto before = std::prev(next);
            if (before->first + before->second == start) {
                start = before->first;
                length += before->second;
                free_.erase(before);
            }
        }
        free_[start] = length;
    }

    std::size_t size() const { return size_; }
    std::size_t in_use() const { return in_use_; }
    std::size_t peak() const { return peak_; }
    std::size_t live_blocks() const { return used_.size(); }

private:
    std::uint8_t* base_ = nullptr;
    std::size_t size_ = 0;
    std::map<std::size_t, std::size_t> free_;  // offset -> length
    std::map<std::size_t, std::size_t> used_;  // offset -> length
    std::size_t in_use_ = 0;
    std::size_t peak_ = 0;
};

// The arena the launch's buffers draw from; null: the default heap. Set by
// the Wii side around a launch.
inline CtgpArena* g_ctgp_arena = nullptr;

// Buffers under this size stay on the default heap: the arena is for the big
// ones, and a thousand small blocks would only fragment it.
constexpr std::size_t kCtgpArenaMinBytes = 16 * 1024;

// A std allocator that draws big blocks from g_ctgp_arena, and anything else
// (or a block the arena has no room for) from operator new. Every instance is
// equal: a block is given back to whichever of the two it came from, by its
// address.
template <typename T>
struct CtgpAllocator {
    using value_type = T;
    CtgpAllocator() = default;
    template <typename U>
    CtgpAllocator(const CtgpAllocator<U>&) {}

    T* allocate(std::size_t count) {
        const std::size_t bytes = count * sizeof(T);
        if (g_ctgp_arena != nullptr && bytes >= kCtgpArenaMinBytes && count <= SIZE_MAX / sizeof(T)) {
            if (void* block = g_ctgp_arena->allocate(bytes)) return static_cast<T*>(block);
        }
        return static_cast<T*>(::operator new(bytes));
    }
    void deallocate(T* p, std::size_t) {
        if (g_ctgp_arena != nullptr && g_ctgp_arena->owns(p)) {
            g_ctgp_arena->release(p);
            return;
        }
        ::operator delete(p);
    }
    template <typename U>
    bool operator==(const CtgpAllocator<U>&) const { return true; }
    template <typename U>
    bool operator!=(const CtgpAllocator<U>&) const { return false; }
};

// The launch's byte buffers.
using CtgpBytes = std::vector<std::uint8_t, CtgpAllocator<std::uint8_t>>;

}  // namespace riftwii
