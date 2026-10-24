// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace riftwii {

struct CachedDisc {
    std::string game_id;
    std::string title;
    std::uint8_t revision = 0;
    std::uint8_t disc_number = 0;
    bool burned = false;
};

// Menu identity only. Cover status reports presence, not a disc identifier:
// a swap entirely between observations can only be caught by a fresh launch
// probe. DI owns this cache so resets, read errors and IOS reloads discard it.
class DiscCache {
public:
    enum class Action { Empty, Probe, Reuse };

    void observe_cover(bool inserted) {
        if (!inserted) invalidate();
        inserted_ = inserted;
        cover_known_ = true;
    }

    Action action() const {
        if (cover_known_ && !inserted_) return Action::Empty;
        return valid_ ? Action::Reuse : Action::Probe;
    }

    void probe_succeeded(const CachedDisc& disc) {
        disc_ = disc;
        valid_ = true;
        cover_known_ = true;
        inserted_ = true;
    }

    const CachedDisc& disc() const { return disc_; }

    void invalidate() {
        disc_ = CachedDisc{};
        valid_ = false;
        cover_known_ = false;
        inserted_ = false;
    }

    // Every launch reads the physical disc again, even after repeated "in"
    // replies. A page's cached identity must never authorize a launch.
    void begin_launch() { invalidate(); }

private:
    CachedDisc disc_;
    bool valid_ = false;
    bool cover_known_ = false;
    bool inserted_ = false;
};

}  // namespace riftwii
