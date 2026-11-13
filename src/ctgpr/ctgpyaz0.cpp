// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpyaz0.hpp"

#include <cstring>

namespace riftwii {
namespace {

constexpr std::uint32_t kWindowSize = 0x1000;  // ring window, 4 KiB
constexpr std::uint32_t kWindowMask = kWindowSize - 1;
constexpr std::uint32_t kTableSize = 0x4000;  // hash slots
constexpr std::uint32_t kTableMask = kTableSize - 1;
// A slot: kLive set = holds a window position (low 12 bits); kUsed set
// without kLive = deleted (a tombstone the probe has to walk past); 0 = empty.
constexpr std::uint16_t kUsed = 0x8000;
constexpr std::uint16_t kLive = 0x4000;
constexpr std::uint32_t kTombstoneLimit = 0x556;  // rebuild the table at this many
constexpr std::uint32_t kMaxMatch = 0x111;        // longest Yaz0 copy (273)
constexpr std::uint32_t kNoMatch = 0xffffffffu;

std::uint32_t key_hash(std::uint32_t key) {
    return ((key + 0xb205u + key * key * 0xef34u) >> 10) & kTableMask;
}

class Encoder {
public:
    Encoder() : window_(kWindowSize, 0), table_(kTableSize, 0) {}

    // Feeds one input byte. Returns false when a position leaving the window
    // has no table entry, where the channel's encoder would search forever.
    // Positions 0-3 are never entered, so this happens when one of positions
    // 4-7 was skipped as a repeat of one of them (data starting with seven
    // equal bytes, for one) and the input is longer than 4101 bytes.
    bool push(std::uint8_t byte) {
        ++count_;
        shift_ = (static_cast<std::uint32_t>(byte) << 24) | (shift_ >> 8);
        if (++ahead_ < 3)
            return true;
        return step();
    }

    // Ends the stream: emits the running match or pending literals, then the
    // bytes still waiting in the look-ahead, then the last partial group.
    void finish() {
        if (src_ != kNoMatch && pending_ > 3)
            emit_match_run();
        emit_pending_literals();
        // The look-ahead bytes sit at the top of the shift register, oldest
        // lowest.
        for (std::uint32_t i = 0; i < ahead_; ++i)
            emit_literal(static_cast<std::uint8_t>(shift_ >> (8 * (4 - ahead_ + i))));
        ahead_ = 0;
        if (group_len_ != 0)
            out_.insert(out_.end(), group_, group_ + group_len_);
        group_len_ = 0;
    }

    std::vector<std::uint8_t>& out() { return out_; }

    // True when a copy reached back before the start of the input. That only
    // happens when the second byte is 0 and the data starts as "a 0 a 0 a"
    // (five zero bytes, for one). The copy then reads the channel encoder's
    // window before anything was written to it, which the channel never
    // clears, so its output for such a file can't be known (and our zeroed
    // window would make a stream that reads before the output's start).
    bool unreproducible() const { return unsafe_; }

private:
    std::uint8_t at(std::uint32_t pos) const { return window_[pos & kWindowMask]; }

    // The 3-byte key starting at a window position, first byte lowest.
    std::uint32_t key_at(std::uint32_t pos) const {
        return at(pos) | (static_cast<std::uint32_t>(at(pos + 1)) << 8) |
               (static_cast<std::uint32_t>(at(pos + 2)) << 16);
    }

    // One input byte (the oldest look-ahead byte) moves into the window.
    bool step() {
        if (count_ >= 0x1006 && !forget(wpos_ + 1))
            return false;
        if (count_ >= 10)
            remember((wpos_ - 3) & kWindowMask);

        const std::uint8_t cur = static_cast<std::uint8_t>(shift_ >> 8);
        if (src_ != kNoMatch) {
            if (at(src_) == cur) {
                commit(cur);
                src_ = (src_ + 1) & kWindowMask;
                ++pending_;
                return true;
            }
            if (pending_ > 3)
                emit_match_run();
        }
        emit_pending_literals();
        commit(cur);

        prestart_ = false;
        if (find(shift_ >> 8))
            return true;
        emit_literal(cur);
        pending_ = 0;
        src_ = kNoMatch;
        dist_ = 0;
        // No table hit: try a run of the byte before (distance 1 or 2).
        const std::uint8_t next = static_cast<std::uint8_t>(shift_ >> 16);
        const std::uint32_t last = (wpos_ - 1) & kWindowMask;
        if (at(last) == next)
            src_ = last;
        const std::uint32_t before = (wpos_ - 2) & kWindowMask;
        if (at(before) == next) {
            src_ = before;
            dist_ = 1;
            // On the first byte this reads the window slot before the file's
            // start, which was never written (see unreproducible()).
            prestart_ = count_ == 3;
        }
        return true;
    }

    void commit(std::uint8_t cur) {
        window_[wpos_] = cur;
        wpos_ = (wpos_ + 1) & kWindowMask;
        --ahead_;
    }

    // Looks the key up; on a hit the match starts at the byte just committed.
    bool find(std::uint32_t key) {
        std::uint32_t slot = key_hash(key);
        for (std::uint32_t probes = 0; probes < kTableSize; ++probes) {
            const std::uint16_t entry = table_[slot];
            slot = (slot + 1) & kTableMask;
            if (entry & kLive) {
                if (key_at(entry) == key) {
                    src_ = (entry + 1u) & kWindowMask;
                    pending_ = 1;
                    dist_ = (wpos_ - 1 - src_) & kWindowMask;
                    return true;
                }
            } else if (!(entry & kUsed)) {
                return false;
            }
        }
        return false;
    }

    // Position `pos` is about to leave the window. Its slot moves on to the
    // first of the next four positions with the same key, or is deleted.
    bool forget(std::uint32_t pos) {
        pos &= kWindowMask;
        const std::uint32_t key = key_at(pos);
        std::uint32_t slot = key_hash(key);
        std::uint32_t probes = 0;
        while (!((table_[slot] & kLive) && (table_[slot] & 0x3fff) == pos)) {
            slot = (slot + 1) & kTableMask;
            if (++probes == kTableSize)
                return false;
        }
        for (std::uint32_t step = 1; step <= 4; ++step) {
            if (key_at(pos + step) == key) {
                table_[slot] = static_cast<std::uint16_t>(((pos + step) & kWindowMask) | kUsed | kLive);
                return true;
            }
        }
        table_[slot] = kUsed;
        if (++tombstones_ >= kTombstoneLimit) {
            rebuild();
            tombstones_ = 0;
        }
        return true;
    }

    // Adds position `pos` unless one of the four positions before it has the
    // same key (forget() hands that entry forward instead).
    void remember(std::uint32_t pos) {
        const std::uint32_t key = key_at(pos);
        for (std::uint32_t back = 4; back >= 1; --back) {
            if (key_at(pos - back) == key)
                return;
        }
        std::uint32_t slot = key_hash(key);
        while (table_[slot] & kLive)
            slot = (slot + 1) & kTableMask;
        if (table_[slot] & kUsed)
            --tombstones_;
        table_[slot] = static_cast<std::uint16_t>(pos | kUsed | kLive);
    }

    // Clears the tombstones, scanning slots in order. For each tombstone, the
    // live entries after it (up to the next empty slot) are pulled back into
    // the hole where their probe chain allows, nearest the run's end first.
    void rebuild() {
        for (std::uint32_t i = 0; i < kTableSize; ++i) {
            const std::uint16_t entry = table_[i];
            if (!(entry & kUsed) || (entry & kLive))
                continue;
            std::uint32_t end = i;
            do {
                end = (end + 1) & kTableMask;
            } while (table_[end] & kUsed);
            std::uint32_t hole = i;
            for (;;) {
                table_[hole] = 0;
                std::uint32_t k = end;
                std::uint32_t home = 0;
                bool moved = false;
                for (;;) {
                    k = (k - 1) & kTableMask;
                    if (k == hole)
                        break;
                    if (!(table_[k] & kLive))
                        continue;
                    home = key_hash(key_at(table_[k]));
                    if (((k - home) & kTableMask) >= ((hole - home) & kTableMask)) {
                        moved = true;
                        break;
                    }
                }
                if (!moved)
                    break;
                table_[hole] = table_[k];
                table_[k] = kUsed;
                hole = k;
            }
        }
    }

    void close_group_item() {
        --bits_;
        if (bits_ == 0) {
            out_.insert(out_.end(), group_, group_ + group_len_);
            group_len_ = 0;
        }
    }

    void emit_literal(std::uint8_t byte) {
        if (bits_ == 0) {
            group_[0] = 0x80;
            group_[1] = byte;
            group_len_ = 2;
            bits_ = 7;
            return;
        }
        group_[group_len_++] = byte;
        --bits_;
        group_[0] |= static_cast<std::uint8_t>(1u << bits_);
        if (bits_ == 0) {
            out_.insert(out_.end(), group_, group_ + group_len_);
            group_len_ = 0;
        }
    }

    // Emits the running match in copies of at most 273 bytes; a tail of 1 or
    // 2 bytes stays pending (and goes out as literals).
    void emit_match_run() {
        if (prestart_)
            unsafe_ = true;
        do {
            if (bits_ == 0) {
                group_[0] = 0;
                group_len_ = 1;
                bits_ = 8;
            }
            const std::uint32_t len = pending_ > kMaxMatch ? kMaxMatch : pending_;
            if (len >= 0x12) {
                group_[group_len_++] = static_cast<std::uint8_t>(dist_ >> 8);
                group_[group_len_++] = static_cast<std::uint8_t>(dist_);
                group_[group_len_++] = static_cast<std::uint8_t>(len - 0x12);
            } else {
                group_[group_len_++] = static_cast<std::uint8_t>(((len - 2) << 4) | (dist_ >> 8));
                group_[group_len_++] = static_cast<std::uint8_t>(dist_);
            }
            close_group_item();
            pending_ -= len;
        } while (pending_ >= 3);
    }

    // The `pending_` bytes just before the write position go out as literals.
    void emit_pending_literals() {
        std::uint32_t pos = wpos_ - pending_;
        while (pending_ != 0) {
            emit_literal(at(pos++));
            --pending_;
        }
    }

    std::vector<std::uint8_t> window_;
    std::vector<std::uint16_t> table_;
    std::vector<std::uint8_t> out_;
    std::uint32_t count_ = 0;       // input bytes taken
    std::uint32_t wpos_ = 0;        // next window position to write
    std::uint32_t shift_ = 0;       // last four input bytes, newest highest
    std::uint32_t ahead_ = 0;       // input bytes not yet in the window
    std::uint32_t tombstones_ = 0;  // deleted slots since the last rebuild
    std::uint32_t src_ = kNoMatch;  // window position the match reads next
    std::uint32_t dist_ = 0;        // match distance minus one
    std::uint32_t pending_ = 0;     // bytes in the running match
    bool prestart_ = false;  // the running match began before the input
    bool unsafe_ = false;
    std::uint8_t group_[1 + 8 * 3] = {};
    std::uint32_t group_len_ = 0;
    std::uint32_t bits_ = 0;  // flag bits left in the group being built
};

}  // namespace

struct CtgpYaz0ExactWriter::State {
    Encoder enc;
    Sink sink;
    std::uint32_t size = 0;
    std::uint64_t pushed = 0;
    bool failed = false;
};

namespace {
constexpr std::size_t kFlushBytes = 16 * 1024;
}

CtgpYaz0ExactWriter::CtgpYaz0ExactWriter(std::uint32_t size, Sink sink) : state_(new State()) {
    state_->sink = std::move(sink);
    state_->size = size;
    std::vector<std::uint8_t>& body = state_->enc.out();
    body.reserve(kFlushBytes + 64);
    const std::uint8_t header[16] = {'Y', 'a', 'z', '0',
                                     static_cast<std::uint8_t>(size >> 24),
                                     static_cast<std::uint8_t>(size >> 16),
                                     static_cast<std::uint8_t>(size >> 8),
                                     static_cast<std::uint8_t>(size)};
    body.insert(body.end(), header, header + 16);
}

CtgpYaz0ExactWriter::~CtgpYaz0ExactWriter() = default;

bool CtgpYaz0ExactWriter::push(const std::uint8_t* data, std::size_t length, std::string& error) {
    State& s = *state_;
    if (s.failed) {
        error = "Yaz0 writer already failed";
        return false;
    }
    if (length > s.size - s.pushed) {
        s.failed = true;
        error = "Yaz0 input longer than its header says";
        return false;
    }
    std::vector<std::uint8_t>& body = s.enc.out();
    for (std::size_t i = 0; i < length; ++i) {
        if (!s.enc.push(data[i])) {
            s.failed = true;
            error = "Yaz0 input starts with a pattern the pack's encoder can't handle";
            return false;
        }
        if (body.size() >= kFlushBytes) {
            if (!s.sink(body.data(), body.size())) {
                s.failed = true;
                error = "the packed file could not be kept";
                return false;
            }
            body.clear();
        }
    }
    s.pushed += length;
    return true;
}

bool CtgpYaz0ExactWriter::finish(std::string& error) {
    State& s = *state_;
    if (s.failed) {
        error = "Yaz0 writer already failed";
        return false;
    }
    if (s.pushed != s.size) {
        s.failed = true;
        error = "Yaz0 input shorter than its header says";
        return false;
    }
    s.enc.finish();
    if (s.enc.unreproducible()) {
        s.failed = true;
        error = "Yaz0 input starts with a pattern the pack's encoder can't be matched on";
        return false;
    }
    std::vector<std::uint8_t>& body = s.enc.out();
    if (!body.empty() && !s.sink(body.data(), body.size())) {
        s.failed = true;
        error = "the packed file could not be kept";
        return false;
    }
    body.clear();
    return true;
}

bool ctgp_yaz0_encode_exact(const std::uint8_t* data, std::size_t size,
                            std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    if (size > kCtgpYaz0MaxInput) {
        error = "Yaz0 input too large";
        return false;
    }
    if (size != 0 && data == nullptr) {
        error = "Yaz0 input missing";
        return false;
    }
    std::vector<std::uint8_t> packed;
    packed.reserve(16 + size + size / 8 + 16);
    CtgpYaz0ExactWriter writer(static_cast<std::uint32_t>(size), [&packed](const std::uint8_t* d, std::size_t n) {
        packed.insert(packed.end(), d, d + n);
        return true;
    });
    if (!writer.push(data, size, error) || !writer.finish(error)) {
        return false;
    }
    out.swap(packed);
    return true;
}

}  // namespace riftwii
