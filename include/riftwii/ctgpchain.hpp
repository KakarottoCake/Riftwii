// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The rules of a chain of CTGP-R updates: the server offers one update at a
// time (a CTGPR pack update, or a HASH update with new hash lists for
// jelly.bin) in whatever order it likes, and RiftWii installs them one after
// another until the server has nothing more, so CTGP-R ends fully current.
// The player is asked once for the whole chain. After every step the
// installed versions are read again from the card (PACKAGES.BIN) and sent
// with the next check. The chain stops:
//  - when the server offers nothing (up to date);
//  - when it offers a package RiftWii doesn't install (only CTGPR and HASH);
//  - when the offered version is not newer than what that package has now;
//  - when the same update is offered twice;
//  - after kCtgpChainMaxSteps installs;
//  - at the first step that fails (what is installed stays, and CTGP-R
//    starts on it: a failed step never stops the game).
namespace riftwii {

constexpr std::size_t kCtgpChainMaxSteps = 10;

enum class CtgpChainVerdict {
    Take,          // install it
    NotOurs,       // a package other than CTGPR and HASH
    NotNewer,      // not newer than the package installed now
    Repeat,        // this update was offered (and installed) already
    TooManySteps,  // kCtgpChainMaxSteps installed already
};

struct CtgpChainStep {
    std::string component;
    std::uint32_t from = 0;  // the package's version before the step
    std::uint32_t to = 0;    // the version the update brings
};

class CtgpChain {
public:
    // Whether to install the update the server offers now: `component` and
    // `version` from its header, `installed` what that package's version is
    // on the card now (read again after every step).
    CtgpChainVerdict offer(const std::string& component, std::uint32_t version, std::uint32_t installed) const;
    // A step went in.
    void installed(const std::string& component, std::uint32_t from, std::uint32_t to);
    // The steps done, in order.
    const std::vector<CtgpChainStep>& steps() const { return steps_; }
    // The step about to start, counting from 1 (for "Update 2").
    std::size_t next_number() const { return steps_.size() + 1; }
    // The first and the last CTGPR version of the chain (0 when no CTGPR
    // step went in): the range a summary or What's new covers.
    std::uint32_t ctgpr_from() const;
    std::uint32_t ctgpr_to() const;

private:
    std::vector<CtgpChainStep> steps_;
};

}  // namespace riftwii
