// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Allocations that fail as an error return instead of an exception.
//
// The Wii build has no C++ exceptions (-fno-exceptions, issue #242): when a
// std::vector or std::string cannot grow, operator new's std::bad_alloc has
// no handler and stops RiftWii. A buffer whose size comes from a disc, a
// pack, a download or a scan therefore asks for its memory through these
// helpers, which try the allocation without throwing first and return false
// when there is no room. Small allocations (a name, a path, a list entry)
// still go straight to the library: if a few bytes cannot be had, nothing
// after them could run either.
//
// A caller several layers up that words "out of memory" its own way, as
// its catch of std::bad_alloc used to, opens an OutOfMemoryScope around the
// step and asks it afterwards whether a check failed inside.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace riftwii {

// Which thread is running, for OutOfMemoryScope. The platform sets it once
// at start-up (the Wii: LWP_GetSelf, wii/main.cpp). Without it every thread
// counts as one, which is right for the single-threaded host tests.
// thread_local is not used: devkitPPC compiles it to r2-relative accesses,
// and libogc gives its threads no TLS blocks (r2 is the small-data base).
using ThreadIdentity = std::uintptr_t (*)();
inline ThreadIdentity g_thread_identity = nullptr;

inline void set_thread_identity(ThreadIdentity identity) {
    g_thread_identity = identity;
}

namespace detail {

// One slot for each thread that has a scope open. A slot is claimed and
// released atomically; its flag is read and written by its owner only, so
// a failure in one thread never shows in another's scope.
struct OutOfMemorySlot {
    std::atomic<std::uintptr_t> owner{0};  // the thread's key; 0: free
    bool failed = false;
};
constexpr int kOutOfMemorySlots = 16;
inline OutOfMemorySlot g_out_of_memory_slots[kOutOfMemorySlots];

// Never 0, so a free slot cannot match.
inline std::uintptr_t this_thread_key() {
    return (g_thread_identity != nullptr ? g_thread_identity() : 0) + 1;
}

inline OutOfMemorySlot* slot_of_this_thread() {
    const std::uintptr_t key = this_thread_key();
    for (OutOfMemorySlot& slot : g_out_of_memory_slots) {
        if (slot.owner.load(std::memory_order_acquire) == key) return &slot;
    }
    return nullptr;
}

// A check failed: tell this thread's open scope, if there is one.
inline void note_out_of_memory() {
    if (OutOfMemorySlot* slot = slot_of_this_thread()) slot->failed = true;
}

}  // namespace detail

// Notes whether a checked allocation failed in this thread while it is
// open. Scopes nest: an inner one starts clear and, when it closes, gives
// the outer one back what it had, as an inner catch kept a failure it had
// handled to itself. With all slots taken (more than 16 threads at once)
// failed() stays false and the step's own error is shown instead.
class OutOfMemoryScope {
public:
    OutOfMemoryScope() {
        slot_ = detail::slot_of_this_thread();
        if (slot_ != nullptr) {
            nested_ = true;
            saved_ = slot_->failed;
        } else {
            const std::uintptr_t key = detail::this_thread_key();
            for (detail::OutOfMemorySlot& slot : detail::g_out_of_memory_slots) {
                std::uintptr_t free_slot = 0;
                if (slot.owner.compare_exchange_strong(free_slot, key, std::memory_order_acq_rel)) {
                    slot_ = &slot;
                    break;
                }
            }
        }
        if (slot_ != nullptr) slot_->failed = false;
    }
    ~OutOfMemoryScope() {
        if (slot_ == nullptr) return;
        if (nested_) {
            slot_->failed = saved_;
        } else {
            slot_->failed = false;
            slot_->owner.store(0, std::memory_order_release);
        }
    }
    OutOfMemoryScope(const OutOfMemoryScope&) = delete;
    OutOfMemoryScope& operator=(const OutOfMemoryScope&) = delete;

    // True when a check failed in this thread since the scope opened.
    bool failed() const { return slot_ != nullptr && slot_->failed; }

private:
    detail::OutOfMemorySlot* slot_ = nullptr;
    bool nested_ = false;
    bool saved_ = false;
};

// The innermost open scope's answer, for a step inside one that wants to
// stop early (false when this thread has none open).
inline bool took_out_of_memory() {
    const detail::OutOfMemorySlot* slot = detail::slot_of_this_thread();
    return slot != nullptr && slot->failed;
}

// The most one checked request may ask for. A Wii has 88 MiB in all, so a
// request past this comes from damaged or hostile input and is refused
// without asking the allocator; the host tests stay far below it too, and
// AddressSanitizer would stop a test on a malloc of an impossible size
// rather than return null.
constexpr std::size_t kMaxCheckedBytes = std::size_t(256) << 20;

// True when `count` elements of `element_bytes` each stay within
// kMaxCheckedBytes (so the product cannot overflow either).
inline bool within_checked_cap(std::size_t count, std::size_t element_bytes) {
    return element_bytes != 0 && count <= kMaxCheckedBytes / element_bytes;
}

namespace detail {

// can_allocate without telling the scope: for a first try that has a
// smaller fallback.
inline bool fits_now(std::size_t bytes) {
    if (bytes == 0) return true;
    if (bytes > kMaxCheckedBytes) return false;
    void* trial = std::malloc(bytes);
    if (trial == nullptr) return false;
    std::free(trial);
    return true;
}

template <typename T, typename A>
bool reserve_quietly(std::vector<T, A>& v, std::size_t count) {
    if (count <= v.capacity()) return true;
    if (count > v.max_size() || !within_checked_cap(count, sizeof(T))) return false;
    if (!fits_now(count * sizeof(T))) return false;
    v.reserve(count);
    return true;
}

}  // namespace detail

// True when `bytes` can be allocated now. The trial block is freed at once,
// and the allocator gives the same room to the request that follows.
inline bool can_allocate(std::size_t bytes) {
    if (detail::fits_now(bytes)) return true;
    detail::note_out_of_memory();
    return false;
}

// True when `total` bytes are free in pieces of `piece` bytes: for a step
// that makes many small allocations (a tree of thousands of names), whose
// sum is known roughly but which needs no single block that large. The
// pieces are held at once, then all freed.
inline bool have_room(std::size_t total, std::size_t piece = 64 * 1024) {
    if (total > kMaxCheckedBytes) {
        detail::note_out_of_memory();
        return false;
    }
    // Each piece holds the address of the one before, so the check needs
    // no list of its own.
    void* chain = nullptr;
    std::size_t held = 0;
    bool enough = true;
    while (held < total) {
        void* block = std::malloc(piece);
        if (block == nullptr) {
            enough = false;
            break;
        }
        *static_cast<void**>(block) = chain;
        chain = block;
        held += piece;
    }
    while (chain != nullptr) {
        void* before = *static_cast<void**>(chain);
        std::free(chain);
        chain = before;
    }
    if (!enough) detail::note_out_of_memory();
    return enough;
}

// Room for `count` elements, so filling up to there cannot fail.
template <typename T, typename A>
bool try_reserve(std::vector<T, A>& v, std::size_t count) {
    if (detail::reserve_quietly(v, count)) return true;
    detail::note_out_of_memory();
    return false;
}

template <typename T, typename A>
bool try_resize(std::vector<T, A>& v, std::size_t count) {
    if (!try_reserve(v, count)) return false;
    v.resize(count);
    return true;
}

// `count` copies of `value`, as vector::assign; the old contents go first,
// so the check counts only the new buffer.
template <typename T, typename A>
bool try_assign(std::vector<T, A>& v, std::size_t count, const T& value) {
    if (count > v.capacity()) {
        std::vector<T, A>().swap(v);
        if (!try_reserve(v, count)) return false;
    }
    v.assign(count, value);
    return true;
}

// A copy of [first, last), as vector::assign.
template <typename T, typename A, typename It>
bool try_assign_range(std::vector<T, A>& v, It first, It last) {
    const std::size_t count = static_cast<std::size_t>(last - first);
    if (count > v.capacity()) {
        std::vector<T, A>().swap(v);
        if (!try_reserve(v, count)) return false;
    }
    v.assign(first, last);
    return true;
}

// push_back that grows the buffer the way the library does (doubling), but
// checked; for lists whose length comes from the input. Without room to
// double, one more element may still fit; only that failing counts.
template <typename T, typename A, typename U>
bool try_push_back(std::vector<T, A>& v, U&& value) {
    if (v.size() == v.capacity()) {
        const std::size_t doubled = v.capacity() == 0 ? 1 : v.capacity() * 2;
        if (!detail::reserve_quietly(v, doubled) && !try_reserve(v, v.size() + 1)) return false;
    }
    v.push_back(std::forward<U>(value));
    return true;
}

// Appends [first, last), growing the buffer at least twofold when it must
// grow, as insert at the end does; for data that arrives in pieces.
template <typename T, typename A, typename It>
bool try_append(std::vector<T, A>& v, It first, It last) {
    const std::size_t count = static_cast<std::size_t>(last - first);
    if (count > v.capacity() - v.size()) {
        if (count > v.max_size() - v.size()) {
            detail::note_out_of_memory();
            return false;
        }
        const std::size_t needed = v.size() + count;
        const std::size_t doubled = v.capacity() > v.max_size() / 2 ? v.max_size() : v.capacity() * 2;
        if (!detail::reserve_quietly(v, std::max(needed, doubled)) && !try_reserve(v, needed)) return false;
    }
    v.insert(v.end(), first, last);
    return true;
}

inline bool try_reserve(std::string& s, std::size_t count) {
    if (count <= s.capacity()) return true;
    if (count > s.max_size() || count >= kMaxCheckedBytes) {
        detail::note_out_of_memory();
        return false;
    }
    if (!can_allocate(count + 1)) return false;
    s.reserve(count);
    return true;
}

inline bool try_resize(std::string& s, std::size_t count, char fill = '\0') {
    if (!try_reserve(s, count)) return false;
    s.resize(count, fill);
    return true;
}

// Appends `length` bytes at `data`, checked, growing twofold as the
// library would; for a string that grows with the input.
inline bool try_append(std::string& s, const char* data, std::size_t length) {
    if (length > s.capacity() - s.size()) {
        const std::size_t needed = s.size() + length;
        const std::size_t doubled = s.capacity() * 2;
        if (std::max(needed, doubled) < kMaxCheckedBytes && detail::fits_now(std::max(needed, doubled) + 1)) {
            s.reserve(std::max(needed, doubled));
        } else if (!try_reserve(s, needed)) {
            return false;
        }
    }
    s.append(data, length);
    return true;
}

}  // namespace riftwii
