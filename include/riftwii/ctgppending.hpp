// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the menu does with a CTGP-R update already waiting on the card
// (sd:/ctgpr/update.dif) before it asks the server for one, and how a file is
// moved out of the way. Kept apart from the menu so the host tests can check
// it.

#include <cstdint>
#include <functional>
#include <string>

namespace riftwii {

enum class CtgpPendingStep {
    Install,   // offer it
    Stop,      // it could not be looked at now (the card, memory): leave it, try next time
    Retire,    // its version is already installed: move it to update.dif.installed
    SetAside,  // it can never be installed here: move it to update.dif.bad
};

// `inspected`: the plugin read the update; `bad_update`: when it could not,
// the file itself is at fault (CtgpUpdRequest::bad_update). The versions are
// the update's, the one it rebuilds from and blob.bin's (0: unknown).
inline CtgpPendingStep ctgp_pending_step(bool inspected, bool bad_update, std::uint32_t pack_version,
                                         std::uint32_t version, std::uint32_t base_version) {
    if (!inspected) return bad_update ? CtgpPendingStep::SetAside : CtgpPendingStep::Stop;
    if (pack_version != 0 && pack_version == version) return CtgpPendingStep::Retire;
    if (pack_version != 0 && pack_version != base_version) return CtgpPendingStep::SetAside;
    return CtgpPendingStep::Install;
}

// Moves `from` to `to`, an older `to` going first (FAT does not rename onto a
// file that is there). True only when the rename worked: a card that is
// locked, or a file that can't be moved, leaves `from` where it was, and the
// caller must not say it was moved.
inline bool ctgp_move_aside(const std::string& from, const std::string& to,
                            const std::function<int(const char*)>& remove,
                            const std::function<int(const char*, const char*)>& rename) {
    remove(to.c_str());
    return rename(from.c_str(), to.c_str()) == 0;
}

}  // namespace riftwii
