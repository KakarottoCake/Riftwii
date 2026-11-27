// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/saveinflate.hpp"
#include "riftwii/checkedalloc.hpp"

namespace riftwii {

SaveZipInflater::~SaveZipInflater() { end(); }

bool SaveZipInflater::begin() {
    end();
    state_ = {};
    if (!can_allocate(96u << 10)) return false;
    active_ = inflateInit2(&state_, -15) == Z_OK;
    return active_;
}

ZipInflateStream::Step SaveZipInflater::feed(const std::uint8_t* input, std::size_t bytes, const ZipSink& sink) {
    if (!active_ || bytes > 32768) return Step::Bad;
    state_.next_in = const_cast<Bytef*>(input);
    state_.avail_in = static_cast<uInt>(bytes);
    do {
        state_.next_out = chunk_;
        state_.avail_out = sizeof(chunk_);
        const int result = inflate(&state_, Z_NO_FLUSH);
        const std::size_t made = sizeof(chunk_) - state_.avail_out;
        if ((cancelled_ && cancelled_()) || (made && !sink(chunk_, made))) return Step::Bad;
        if (result == Z_STREAM_END) return state_.avail_in == 0 ? Step::Done : Step::Bad;
        // An exact output-chunk boundary can drain the input before the
        // stream ends. More compressed bytes arrive on the next feed.
        if (result == Z_BUF_ERROR && state_.avail_in == 0) return Step::More;
        if (result != Z_OK) return Step::Bad;
    } while (state_.avail_in || state_.avail_out == 0);
    return Step::More;
}

void SaveZipInflater::end() {
    if (active_) inflateEnd(&state_);
    active_ = false;
}

}  // namespace riftwii
