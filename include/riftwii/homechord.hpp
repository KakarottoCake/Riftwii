// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Home's held-back buttons, as button masks in and decisions out, so they
// run (and are tested) on a PC; wii/menu/screenshot.cpp feeds them each
// controller's presses once a frame.
//
// To tell a tap from a hold, or one button from a pair, a button's press is
// kept back from the menu until it is let go: let go alone, its press comes
// then, late; used by a combo or a hold, it comes to nothing. Every screen
// Home hands over to must settle what is still kept back (spend it, or let
// it come late), or a stale press answers the wrong screen.
namespace riftwii {

// One button kept back.
struct HeldBack {
    bool held_back = false;  // it went down; its press is owed
    bool used = false;       // a combo or a hold took it: let go, nothing
};

// Keeps `bit`'s press out of `down` while it is held; let go, gives it
// back (unless used). True while held.
inline bool hold_back(HeldBack& c, std::uint32_t& down, std::uint32_t held, std::uint32_t bit) {
    if (down & bit) {
        down &= ~bit;
        c.held_back = true;
        c.used = false;
    }
    if (c.held_back && !(held & bit)) {
        if (!c.used) down |= bit;  // let go alone: the press, late
        c.held_back = false;
    }
    return c.held_back && (held & bit);
}

// A press still kept back comes to nothing when let go.
inline void spend(HeldBack& c) {
    if (c.held_back) c.used = true;
}

// Two buttons that together change Home's view (#355, #382), each of which
// does its own job alone: both are kept back while Home watches.
struct ViewPair {
    HeldBack first, second;
    // Went down while Home watched: a half held since before isn't one.
    bool first_watched = false, second_watched = false;
};

// One frame of a pair. True once, when the pair is completed (both down,
// both since Home watched). Home not watching: a half already kept back is
// settled, and a new press goes straight through.
inline bool watch_pair(ViewPair& v, bool watching, std::uint32_t& down, std::uint32_t held, std::uint32_t first,
                       std::uint32_t second) {
    if (watching && (down & first)) v.first_watched = true;
    if (watching && (down & second)) v.second_watched = true;
    if (!watching) {
        if (v.first.held_back) hold_back(v.first, down, held, first);
        if (v.second.held_back) hold_back(v.second, down, held, second);
        return false;
    }
    const bool a = hold_back(v.first, down, held, first);
    const bool b = hold_back(v.second, down, held, second);
    if (!(a && b && v.first_watched && v.second_watched)) return false;
    // A half already used (a press that woke the screen, or this pair made
    // while the other half was pressed again) makes the pair come to
    // nothing, and the other half with it.
    const bool fresh = !v.first.used && !v.second.used;
    v.first.used = v.second.used = true;
    return fresh;
}

// Home stopped watching, or a popup closed: neither half counts towards a
// pair any more.
inline void forget_pair(ViewPair& v) { v.first_watched = v.second_watched = false; }

inline void spend(ViewPair& v) {
    spend(v.first);
    spend(v.second);
}

// The search button's own button held `frames` frames starts a game at
// random (#357, #382); let go sooner, it is the search.
struct SearchHold {
    HeldBack button;
    std::uint32_t from = 0;  // the frame it went down, or Home watched again
};

// One frame of a hold at frame `now`. True once, when it has been held
// `frames` watched frames. Not watched (Home busy, a popup up): a press
// already kept back is settled, its count restarting, so the pause never
// counts and a release meanwhile still searches; a new press goes through.
inline bool watch_hold(SearchHold& h, bool watched, std::uint32_t now, std::uint32_t frames, std::uint32_t& down,
                       std::uint32_t held, std::uint32_t bit) {
    if (!watched && !h.button.held_back) return false;
    if (!watched) h.from = now;
    if (down & bit) h.from = now;
    if (!hold_back(h.button, down, held, bit)) return false;
    if (watched && !h.button.used && now - h.from >= frames) {
        h.button.used = true;  // let go later, it searches no more
        return true;
    }
    return false;
}

inline void spend(SearchHold& h) { spend(h.button); }

}  // namespace riftwii
