// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace riftwii {

// Largest input ctgp_yaz0_encode_exact takes.
constexpr std::size_t kCtgpYaz0MaxInput = 64u * 1024u * 1024u;

// Compresses `data` to Nintendo Yaz0 ("Yaz0", big-endian size, 8 zero bytes,
// then flag groups) the same way the CTGP-R channel compresses the files it
// writes into its pack, byte for byte. The pack's hash list covers the stored
// (compressed) bytes, so a file rebuilt here must come out exactly as the
// channel would have written it; any standard Yaz0 encoder would decode the
// same but hash differently.
//
// How the channel's encoder works, in short: a 4 KiB ring window and a
// 16384-slot open-addressed hash table of 3-byte keys (window positions). A
// match is only searched for when no match is running; the first live table
// entry whose key equals the next three bytes wins, and the match then grows
// one byte at a time until a byte differs. Matches shorter than 4 bytes go out
// as literals. Uses about 40 KiB of heap (window and table) plus `out`.
bool ctgp_yaz0_encode_exact(const std::uint8_t* data, std::size_t size,
                            std::vector<std::uint8_t>& out, std::string& error);

// The same encoder, fed in pieces. `size` is the whole input's length (the
// header holds it, so it must be known first); `sink` gets the packed bytes
// in order, a few KiB at a time. Neither the input nor the output has to be
// in memory at once: the writer itself holds about 60 KiB.
class CtgpYaz0ExactWriter {
public:
    using Sink = std::function<bool(const std::uint8_t* data, std::size_t length)>;
    CtgpYaz0ExactWriter(std::uint32_t size, Sink sink);
    ~CtgpYaz0ExactWriter();
    CtgpYaz0ExactWriter(const CtgpYaz0ExactWriter&) = delete;
    CtgpYaz0ExactWriter& operator=(const CtgpYaz0ExactWriter&) = delete;

    bool push(const std::uint8_t* data, std::size_t length, std::string& error);
    // Ends the stream. Fails when the bytes pushed are not exactly `size`.
    bool finish(std::string& error);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace riftwii
