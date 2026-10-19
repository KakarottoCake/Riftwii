// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// VCDIFF (RFC 3284) decoding, written from the RFC: its default code
// table (section 5.6), address caches (5.3), integers (2) and windows
// (4.2, 4.3); xdelta3's extensions (the application header and the
// per-window Adler-32) from xdelta3's format notes.
#include "riftwii/vcdiff.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

namespace riftwii {

namespace {

// Sizes `v` to `n` bytes for new contents (the old ones are not kept): a
// buffer that has to grow is freed first, so a larger window never needs
// the old block and the new one at once. The Wii's menu has about 30 MB
// to prepare a game in, and a buffer grown in place briefly took twice
// its size (Super Mario Eclipse's third window found no 8 MB block).
void fresh_size(std::vector<std::uint8_t>& v, std::uint64_t n) {
    if (n > v.capacity()) std::vector<std::uint8_t>().swap(v);
    v.resize(static_cast<std::size_t>(n));
}


constexpr std::uint8_t kSource = 0x01;   // Win_Indicator: the copy window is in the original
constexpr std::uint8_t kTarget = 0x02;   // ...or in the patched image made so far
constexpr std::uint8_t kAdler32 = 0x04;  // xdelta3: the window's checksum follows
constexpr std::uint8_t kDecompress = 0x01;
constexpr std::uint8_t kCodeTable = 0x02;
constexpr std::uint8_t kAppHeader = 0x04;

enum : std::uint8_t { kNoop = 0, kAdd = 1, kRun = 2, kCopy = 3 };

struct Inst {
    std::uint8_t type = kNoop;
    std::uint8_t size = 0;
    std::uint8_t mode = 0;
};
using CodeEntry = std::array<Inst, 2>;

// RFC 3284 section 5.6: the default instruction code table.
const std::array<CodeEntry, 256>& code_table() {
    static const std::array<CodeEntry, 256> t = [] {
        std::array<CodeEntry, 256> c{};
        std::size_t i = 0;
        c[i++] = {{{kRun, 0, 0}, {}}};
        c[i++] = {{{kAdd, 0, 0}, {}}};
        for (std::uint8_t s = 1; s <= 17; ++s) c[i++] = {{{kAdd, s, 0}, {}}};
        for (std::uint8_t m = 0; m <= 8; ++m) {
            c[i++] = {{{kCopy, 0, m}, {}}};
            for (std::uint8_t s = 4; s <= 18; ++s) c[i++] = {{{kCopy, s, m}, {}}};
        }
        for (std::uint8_t m = 0; m <= 5; ++m)
            for (std::uint8_t a = 1; a <= 4; ++a)
                for (std::uint8_t s = 4; s <= 6; ++s) c[i++] = {{{kAdd, a, 0}, {kCopy, s, m}}};
        for (std::uint8_t m = 6; m <= 8; ++m)
            for (std::uint8_t a = 1; a <= 4; ++a) c[i++] = {{{kAdd, a, 0}, {kCopy, 4, m}}};
        for (std::uint8_t m = 0; m <= 8; ++m) c[i++] = {{{kCopy, 4, m}, {kAdd, 1, 0}}};
        return c;
    }();
    return t;
}

// RFC 3284 section 2: base 128, most significant digit first.
template <class NextByte>
bool read_integer(NextByte next, std::uint64_t& out) {
    out = 0;
    for (int i = 0; i < 10; ++i) {
        std::uint8_t b;
        if (!next(b)) return false;
        out = (out << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

class Section {
public:
    explicit Section(const std::vector<std::uint8_t>& b) : b_(b) {}
    bool byte(std::uint8_t& out) {
        if (at_ >= b_.size()) return false;
        out = b_[at_++];
        return true;
    }
    bool integer(std::uint64_t& out) {
        return read_integer([this](std::uint8_t& b) { return byte(b); }, out);
    }
    bool take(std::size_t n, const std::uint8_t*& out) {
        if (n > b_.size() - at_) return false;
        out = b_.data() + at_;
        at_ += n;
        return true;
    }
    bool done() const { return at_ >= b_.size(); }

private:
    const std::vector<std::uint8_t>& b_;
    std::size_t at_ = 0;
};

// RFC 3284 section 5.3, with the default s_near = 4, s_same = 3.
class AddressCache {
public:
    bool decode(std::uint64_t here, std::uint8_t mode, Section& addrs, std::uint64_t& out) {
        if (mode == 0) {
            if (!addrs.integer(out)) return false;
        } else if (mode == 1) {
            std::uint64_t back;
            if (!addrs.integer(back) || back > here) return false;
            out = here - back;
        } else if (mode < 6) {
            std::uint64_t add;
            if (!addrs.integer(add)) return false;
            out = near_[mode - 2] + add;
        } else if (mode < 9) {
            std::uint8_t b;
            if (!addrs.byte(b)) return false;
            out = same_[(mode - 6) * 256 + b];
        } else {
            return false;
        }
        if (out >= here) return false;  // a copy only reaches what precedes it
        near_[next_] = out;
        next_ = (next_ + 1) % near_.size();
        same_[out % same_.size()] = out;
        return true;
    }

private:
    std::array<std::uint64_t, 4> near_{};
    std::array<std::uint64_t, 3 * 256> same_{};
    std::size_t next_ = 0;
};

class Decoder {
public:
    Decoder(const VcdiffReadFn& patch, std::uint64_t patch_size, const VcdiffReadFn& source,
            std::uint64_t source_size, const VcdiffReadFn& read_target, const VcdiffLimits& limits,
            std::string& error)
        : patch_(patch), patch_size_(patch_size), source_(source), source_size_(source_size),
          read_target_(read_target), limits_(limits), error_(error) {}

    bool header() {
        std::uint8_t m[5];
        if (!take(m, 5)) return fail("not a VCDIFF patch (too short)");
        if (m[0] != 0xD6 || m[1] != 0xC3 || m[2] != 0xC4 || m[3] != 0) return fail("not a VCDIFF (xdelta) patch");
        if (m[4] & kDecompress) return fail("the patch's data is compressed (not supported)");
        if (m[4] & kCodeTable) return fail("the patch uses its own code table (not supported)");
        if (m[4] & kAppHeader) {
            std::uint64_t n;
            if (!integer(n) || n > patch_size_ - pos_) return fail("the patch's header is damaged");
            pos_ += n;
        }
        return true;
    }

    bool at_end() const { return pos_ >= patch_size_; }
    std::uint64_t pos() const { return pos_; }
    std::uint64_t made() const { return made_; }
    bool has_checksum() const { return has_adler_; }

    // The next window into `out`; checked against its checksum.
    bool window(std::vector<std::uint8_t>& out) {
        std::uint8_t win, delta_ind;
        std::uint64_t seg_len = 0, seg_pos = 0, delta_len, window_len, data_len, inst_len, addr_len;
        if (!byte(win)) return fail("a patch window is cut short");
        if ((win & kSource) && (win & kTarget)) return fail("a patch window is damaged");
        if ((win & (kSource | kTarget)) && (!integer(seg_len) || !integer(seg_pos)))
            return fail("a patch window is cut short");
        if (!integer(delta_len)) return fail("a patch window is cut short");
        // The delta encoding's length counts every byte after it, up to the
        // end of the window's three sections (RFC 3284, 4.3).
        const std::uint64_t delta_start = pos_;
        if (!integer(window_len) || !byte(delta_ind) || !integer(data_len) || !integer(inst_len) ||
            !integer(addr_len))
            return fail("a patch window is cut short");
        if (delta_ind != 0) return fail("a patch window's sections are compressed (not supported)");
        const std::uint64_t max = limits_.max_window;
        if (window_len > max || data_len > max || inst_len > max || addr_len > max || seg_len > limits_.max_copy_window)
            return fail("a patch window is larger than RiftWii takes");
        has_adler_ = (win & kAdler32) != 0;
        std::uint32_t adler = 0;
        if (has_adler_) {
            std::uint8_t c[4];
            if (!take(c, 4)) return fail("a patch window is cut short");
            adler = (std::uint32_t(c[0]) << 24) | (std::uint32_t(c[1]) << 16) | (std::uint32_t(c[2]) << 8) | c[3];
        }
        if (!section(data_, data_len) || !section(inst_, inst_len) || !section(addr_, addr_len))
            return fail("a patch window runs past the file's end");
        if (pos_ - delta_start != delta_len) return fail("a patch window's length does not match its contents");

        // The copy window: from the original, or from the image made so far.
        seg_pos_ = seg_pos;
        seg_whole_ = seg_len <= limits_.whole_copy_window || (win & kTarget);
        seg_target_ = (win & kTarget) != 0;
        if (win & kSource) {
            if (seg_pos > source_size_ || seg_len > source_size_ - seg_pos)
                return fail("the patch reaches past the end of the original (is it the right game?)");
        } else if (win & kTarget) {
            if (seg_pos > made_ || seg_len > made_ - seg_pos) return fail("a patch window copies what is not made yet");
            if (seg_len > limits_.max_window) return fail("a patch window is larger than RiftWii takes");
        }
        if (seg_whole_) {
            fresh_size(seg_, seg_len);
            if (seg_len && !read_seg(seg_pos, seg_.data(), seg_.size())) return false;
        }

        fresh_size(out, window_len);
        window_len_ = window_len;
        seg_len_ = seg_len;
        if (!seg_whole_ && seg_len != 0) {
            // A copy window too large to read at once: note the blocks of it
            // the window copies from, then read them in one forward pass, a
            // region at a time, putting each copy's bytes in place; the
            // final pass below then leaves those copies alone.
            const std::uint64_t bs = limits_.cache_block;
            const std::uint64_t region = bs * limits_.cache_blocks;
            std::vector<bool> used(static_cast<std::size_t>((seg_len + bs - 1) / bs));
            if (!walk(out, [&](std::uint64_t from, std::uint64_t n, std::uint64_t) {
                    for (std::uint64_t b = from / bs; b <= (from + n - 1) / bs; ++b) used[b] = true;
                    return true;
                }))
                return false;
            if (cache_.size() < region) fresh_size(cache_, region);
            for (std::uint64_t lo = 0; lo < seg_len; lo += region) {
                const std::uint64_t hi = std::min(seg_len, lo + region);
                bool any = false;
                for (std::uint64_t b = lo / bs; b * bs < hi; ++b) {
                    if (!used[b]) continue;
                    // A run of used blocks, read as one.
                    std::uint64_t e = b;
                    while ((e + 1) * bs < hi && used[e + 1]) ++e;
                    const std::uint64_t from = b * bs, to = std::min(hi, (e + 1) * bs);
                    if (!read_seg(seg_pos + from, cache_.data() + (from - lo), static_cast<std::size_t>(to - from)))
                        return false;
                    any = true;
                    b = e;
                }
                if (!any) continue;
                if (!walk(out, [&](std::uint64_t from, std::uint64_t n, std::uint64_t dst) {
                        const std::uint64_t a = std::max(from, lo), z = std::min(from + n, hi);
                        if (a < z)
                            std::memcpy(out.data() + dst + (a - from), cache_.data() + (a - lo),
                                        static_cast<std::size_t>(z - a));
                        return true;
                    }))
                    return false;
            }
        }
        // The final pass, in order: adds, runs, copies from this window's
        // own output, and (when it was read whole) the copy window.
        Section data(data_);
        if (!walk(out, nullptr, &data)) return false;
        if (!data.done()) return fail("a patch window has bytes left over");
        if (has_adler_ && vcdiff_adler32(out.data(), out.size()) != adler)
            return fail("a patch window's checksum does not match (is it the right game?)");
        made_ += window_len;
        return true;
    }

    VcdiffStats stats;

private:
    bool read_seg(std::uint64_t at, std::uint8_t* out, std::size_t n) {
        if (seg_target_) {
            if (!read_target_ || !read_target_(at, out, n)) return fail("cannot read back the patched image");
            return true;
        }
        stats.source_reads++;
        stats.source_bytes += n;
        if (!source_(at, out, n)) return fail("cannot read the original");
        return true;
    }
    using SourceCopyFn = std::function<bool(std::uint64_t from, std::uint64_t n, std::uint64_t dst)>;
    // Runs the window's instructions. With `on_source` (a planning pass)
    // only the copies from the copy window are reported, nothing is
    // written. Without it (the final pass) the window is made, with `data`
    // as the data section; copies from a copy window that was not read
    // whole were put in place already.
    bool walk(std::vector<std::uint8_t>& out, const SourceCopyFn& on_source, Section* data = nullptr) {
        Section insts(inst_), addrs(addr_);
        AddressCache cache;
        std::uint64_t at = 0;
        const auto& table = code_table();
        while (!insts.done()) {
            std::uint8_t code;
            if (!insts.byte(code)) break;
            for (const Inst& in : table[code]) {
                if (in.type == kNoop) continue;
                std::uint64_t size = in.size;
                if (size == 0 && !insts.integer(size)) return fail("a patch instruction is cut short");
                if (size > window_len_ - at) return fail("a patch instruction runs past its window");
                std::uint8_t* dst = out.data() + at;
                if (in.type == kAdd) {
                    if (data) {
                        const std::uint8_t* src;
                        if (!data->take(static_cast<std::size_t>(size), src))
                            return fail("a patch instruction runs past its data");
                        std::memcpy(dst, src, static_cast<std::size_t>(size));
                    }
                } else if (in.type == kRun) {
                    if (data) {
                        std::uint8_t b;
                        if (!data->byte(b)) return fail("a patch instruction runs past its data");
                        std::memset(dst, b, static_cast<std::size_t>(size));
                    }
                } else {
                    std::uint64_t addr;
                    if (!cache.decode(seg_len_ + at, in.mode, addrs, addr)) return fail("a patch address is damaged");
                    // U is the copy window followed by this window's output;
                    // a copy may run on into bytes it makes itself.
                    for (std::uint64_t i = 0; i < size;) {
                        const std::uint64_t from = addr + i;
                        if (from < seg_len_) {
                            const std::uint64_t n = std::min(size - i, seg_len_ - from);
                            if (on_source) {
                                if (!on_source(from, n, at + i)) return false;
                            } else if (seg_whole_) {
                                std::memcpy(dst + i, seg_.data() + from, static_cast<std::size_t>(n));
                            }
                            i += n;
                        } else {
                            if (!on_source) dst[i] = out[static_cast<std::size_t>(from - seg_len_)];
                            ++i;
                        }
                    }
                }
                at += size;
            }
        }
        if (at != window_len_) return fail("a patch window makes fewer bytes than it says");
        if (!addrs.done()) return fail("a patch window has bytes left over");
        return true;
    }
    bool fail(const char* why) {
        error_ = why;
        return false;
    }
    bool take(void* out, std::size_t n) {
        if (n > patch_size_ - std::min(pos_, patch_size_) || !patch_(pos_, out, n)) return false;
        pos_ += n;
        return true;
    }
    bool byte(std::uint8_t& out) { return take(&out, 1); }
    bool integer(std::uint64_t& out) {
        return read_integer([this](std::uint8_t& b) { return byte(b); }, out);
    }
    // A section's bytes, its length checked against what is left of the
    // patch before anything is allocated: a 30-byte patch whose header
    // claimed 32 MiB a section asked for 100 MB and threw (BUG-02).
    bool section(std::vector<std::uint8_t>& out, std::uint64_t n) {
        if (n > patch_size_ - std::min(pos_, patch_size_)) return false;
        fresh_size(out, n);
        return n == 0 || take(out.data(), out.size());
    }

    const VcdiffReadFn& patch_;
    std::uint64_t patch_size_;
    const VcdiffReadFn& source_;
    std::uint64_t source_size_;
    const VcdiffReadFn& read_target_;
    VcdiffLimits limits_;
    std::string& error_;
    std::uint64_t pos_ = 0;
    std::uint64_t made_ = 0;
    bool has_adler_ = false;
    std::vector<std::uint8_t> data_, inst_, addr_, seg_, cache_;
    std::uint64_t seg_pos_ = 0;
    std::uint64_t seg_len_ = 0;
    std::uint64_t window_len_ = 0;
    bool seg_whole_ = true;
    bool seg_target_ = false;
};

}  // namespace

std::uint32_t vcdiff_adler32(const std::uint8_t* data, std::size_t length, std::uint32_t adler) {
    std::uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (length != 0) {
        std::size_t n = std::min<std::size_t>(length, 5552);
        length -= n;
        while (n--) {
            a += *data++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

bool apply_vcdiff(const VcdiffReadFn& patch, std::uint64_t patch_size, const VcdiffReadFn& source,
                  std::uint64_t source_size, const VcdiffWriteFn& write, const VcdiffReadFn& read_target,
                  const VcdiffProgressFn& progress, std::string& error, const VcdiffLimits& limits,
                  VcdiffStats* stats) {
    Decoder d(patch, patch_size, source, source_size, read_target, limits, error);
    if (!d.header()) return false;
    std::vector<std::uint8_t> out;
    bool any = false;
    while (!d.at_end()) {
        if (!d.window(out)) return false;
        any = true;
        if (!out.empty() && !write(out.data(), out.size())) {
            error = "cannot write the patched image";
            return false;
        }
        if (progress && !progress(d.pos(), patch_size)) {
            error = "stopped";
            return false;
        }
    }
    if (stats) *stats = d.stats;
    if (!any) {
        error = "the patch has no windows";
        return false;
    }
    return true;
}

bool vcdiff_head(const VcdiffReadFn& patch, std::uint64_t patch_size, const VcdiffReadFn& source,
                 std::uint64_t source_size, std::size_t length, std::vector<std::uint8_t>& out, bool& checked,
                 std::string& error, const VcdiffLimits& limits) {
    Decoder d(patch, patch_size, source, source_size, VcdiffReadFn{}, limits, error);
    std::vector<std::uint8_t> first;
    if (!d.header() || !d.window(first)) return false;
    if (first.size() < length) {
        error = "the patch's first window is too short";
        return false;
    }
    checked = d.has_checksum();
    out.assign(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(length));
    return true;
}

}  // namespace riftwii
