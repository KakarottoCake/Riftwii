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
// A failure also raises a flag, so a caller several layers up can tell "out
// of memory" from the other reasons a step fails and say so, as its catch
// of std::bad_alloc used to (clear_out_of_memory, then took_out_of_memory).

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace riftwii {

// Set by a failed check, read by the caller that reports it. One flag for
// the whole program: the menu's threads seldom run out of memory at once,
// and the worst a race does is give one failure the other's message.
inline volatile bool g_out_of_memory = false;

inline void clear_out_of_memory() {
    g_out_of_memory = false;
}

// True when a check failed since clear_out_of_memory.
inline bool took_out_of_memory() {
    return g_out_of_memory;
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

// True when `bytes` can be allocated now. The trial block is freed at once,
// and the allocator gives the same room to the request that follows.
inline bool can_allocate(std::size_t bytes) {
    if (bytes == 0) return true;
    if (bytes > kMaxCheckedBytes) {
        g_out_of_memory = true;
        return false;
    }
    void* trial = std::malloc(bytes);
    if (trial == nullptr) {
        g_out_of_memory = true;
        return false;
    }
    std::free(trial);
    return true;
}

// True when `total` bytes are free in pieces of `piece` bytes: for a step
// that makes many small allocations (a tree of thousands of names), whose
// sum is known roughly but which needs no single block that large. The
// pieces are held at once, then all freed.
inline bool have_room(std::size_t total, std::size_t piece = 64 * 1024) {
    if (total > kMaxCheckedBytes) {
        g_out_of_memory = true;
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
    if (!enough) g_out_of_memory = true;
    return enough;
}

// Room for `count` elements, so filling up to there cannot fail.
template <typename T, typename A>
bool try_reserve(std::vector<T, A>& v, std::size_t count) {
    if (count <= v.capacity()) return true;
    if (count > v.max_size() || !within_checked_cap(count, sizeof(T))) {
        g_out_of_memory = true;
        return false;
    }
    if (!can_allocate(count * sizeof(T))) return false;
    v.reserve(count);
    return true;
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
// checked; for lists whose length comes from the input.
template <typename T, typename A, typename U>
bool try_push_back(std::vector<T, A>& v, U&& value) {
    if (v.size() == v.capacity()) {
        const std::size_t doubled = v.capacity() == 0 ? 1 : v.capacity() * 2;
        // Without room to double, one more element may still fit; only
        // that failing counts as running out.
        const bool flag_before = g_out_of_memory;
        if (!try_reserve(v, doubled)) {
            g_out_of_memory = flag_before;
            if (!try_reserve(v, v.size() + 1)) return false;
        }
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
            g_out_of_memory = true;
            return false;
        }
        const std::size_t needed = v.size() + count;
        const std::size_t doubled = v.capacity() > v.max_size() / 2 ? v.max_size() : v.capacity() * 2;
        const bool flag_before = g_out_of_memory;
        if (!try_reserve(v, std::max(needed, doubled))) {
            g_out_of_memory = flag_before;
            if (!try_reserve(v, needed)) return false;
        }
    }
    v.insert(v.end(), first, last);
    return true;
}

inline bool try_reserve(std::string& s, std::size_t count) {
    if (count <= s.capacity()) return true;
    if (count > s.max_size() || count >= kMaxCheckedBytes) {
        g_out_of_memory = true;
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

}  // namespace riftwii
