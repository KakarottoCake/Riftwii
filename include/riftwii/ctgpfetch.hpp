// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Downloading the update the player agreed to, on a connection of its own.
//
// The update server drops a connection left idle: on a Wii, one held open
// while the player read the prompt was closed before the download's first
// byte. So the download never reuses the connection of the check the prompt
// came from. It asks again, checks that the server still offers the same
// update (the same size; the header is checked after the download), and
// downloads at once. A download the server closes before its first byte is
// tried once more, on another new connection.

#include <cstdint>
#include <functional>
#include <string>

#include "riftwii/ctgpcheck.hpp"

namespace riftwii {

enum class CtgpFetch {
    Done,
    Failed,        // the network, the server or the card (`error` says which)
    OfferChanged,  // the server now offers no update, or another one
};

struct CtgpFetchSteps {
    // A fresh check on a new connection (any earlier one closed first).
    std::function<bool(CtgpCheckReply& reply, std::string& error)> check;
    // The download on that connection; `received` gets the bytes that came,
    // `stopped` whether the progress callback stopped it, `bad` whether the
    // file can never be used (a retry would not help).
    std::function<bool(std::uint64_t& received, bool& stopped, bool& bad, std::string& error)> download;
    // Closes the connection.
    std::function<void()> close;
    // A line for the log.
    std::function<void(const std::string& line)> log;
};

inline CtgpFetch ctgp_fetch_offer(std::uint32_t size, const CtgpFetchSteps& steps, std::string& error) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        steps.close();
        CtgpCheckReply again;
        if (!steps.check(again, error)) return CtgpFetch::Failed;
        if (!again.update_waiting || again.update_bytes != size) {
            steps.close();
            error = again.update_waiting ? "the server now offers another update (" +
                                               std::to_string(again.update_bytes) + " bytes, not " +
                                               std::to_string(size) + ")"
                                         : "the server no longer offers an update";
            if (steps.log) steps.log(error);
            return CtgpFetch::OfferChanged;
        }
        std::uint64_t received = 0;
        bool stopped = false, bad = false;
        if (steps.download(received, stopped, bad, error)) return CtgpFetch::Done;
        if (received != 0 || stopped || bad || attempt == 1) return CtgpFetch::Failed;
        if (steps.log) steps.log("the server sent none of the update (" + error + "); asking it again");
    }
    return CtgpFetch::Failed;
}

}  // namespace riftwii
