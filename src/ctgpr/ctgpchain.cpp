// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpchain.hpp"

namespace riftwii {

CtgpChainVerdict CtgpChain::offer(const std::string& component, std::uint32_t version, std::uint32_t installed) const {
    if (component != "CTGPR" && component != "HASH") return CtgpChainVerdict::NotOurs;
    for (const CtgpChainStep& step : steps_) {
        if (step.component == component && step.to == version) return CtgpChainVerdict::Repeat;
    }
    if (installed != 0 && version <= installed) return CtgpChainVerdict::NotNewer;
    if (steps_.size() >= kCtgpChainMaxSteps) return CtgpChainVerdict::TooManySteps;
    return CtgpChainVerdict::Take;
}

void CtgpChain::installed(const std::string& component, std::uint32_t from, std::uint32_t to) {
    steps_.push_back(CtgpChainStep{component, from, to});
}

std::uint32_t CtgpChain::ctgpr_from() const {
    for (const CtgpChainStep& step : steps_) {
        if (step.component == "CTGPR") return step.from;
    }
    return 0;
}

std::uint32_t CtgpChain::ctgpr_to() const {
    std::uint32_t to = 0;
    for (const CtgpChainStep& step : steps_) {
        if (step.component == "CTGPR") to = step.to;
    }
    return to;
}

}  // namespace riftwii
