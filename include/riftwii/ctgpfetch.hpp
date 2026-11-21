// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Downloading the update the player agreed to, on a connection of its own.
//
// The update server was seen to close a connection left idle: on a Wii, one
// held open while the player read the prompt (about 22 s) was closed before
// the download's first byte. So the download never uses the connection of
// the check the prompt came from. It asks again, checks that the server
// still offers an update of the same size (the check's reply names no
// version; the downloaded update's own header and signature are checked
// after it, and the chain checks its version), and downloads at once. A
// download the server closes before its first byte is tried once more, on
// another new connection; any other failure is not retried.

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
    // The download on that connection. `closed_at_start` is set when it
    // failed because the server closed the connection before sending a byte
    // (ctgp_download_closed_at_start), the one failure worth a retry.
    std::function<bool(bool& closed_at_start, std::string& error)> download;
    // Closes the connection.
    std::function<void()> close;
    // A line for the log.
    std::function<void(const std::string& line)> log;
};

// Whether a download failed (`error` from ctgp_download, as the plugin
// passes it on) because the server closed it before its first byte. Not a
// card that could not take the file, a receive error or timeout, or a stop.
inline bool ctgp_download_closed_at_start(const std::string& error) {
    return error.find("the server closed the download at 0 of ") != std::string::npos;
}

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
        bool closed_at_start = false;
        if (steps.download(closed_at_start, error)) return CtgpFetch::Done;
        if (!closed_at_start || attempt == 1) return CtgpFetch::Failed;
        if (steps.log) steps.log("the server sent none of the update (" + error + "); asking it again");
    }
    return CtgpFetch::Failed;
}

}  // namespace riftwii
