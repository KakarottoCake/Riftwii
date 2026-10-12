// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/extrasbudget.hpp"

#include <algorithm>

namespace riftwii {

void ExtrasBudget::add(const std::string& name, std::uint32_t mem1, std::uint32_t mem2) {
    if (mem1 == 0 && mem2 == 0) return;
    pieces.push_back(ExtraPiece{name, mem1, mem2});
}

std::uint32_t ExtrasBudget::mem1() const {
    std::uint32_t sum = 0;
    for (const ExtraPiece& p : pieces) sum += p.mem1;
    return sum;
}

std::uint32_t ExtrasBudget::mem2() const {
    std::uint32_t sum = 0;
    for (const ExtraPiece& p : pieces) sum += p.mem2;
    return sum;
}

std::uint32_t whole_kb(std::uint32_t bytes) { return (bytes + 1023) / 1024; }

std::string ExtrasBudget::list() const {
    std::vector<ExtraPiece> sorted = pieces;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ExtraPiece& a, const ExtraPiece& b) {
        return a.mem1 + a.mem2 > b.mem1 + b.mem2;
    });
    std::string out;
    for (const ExtraPiece& p : sorted) {
        if (!out.empty()) out += ", ";
        out += p.name + " " + std::to_string(whole_kb(p.mem1 + p.mem2)) + " KB";
    }
    return out;
}

}  // namespace riftwii
