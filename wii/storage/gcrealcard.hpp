// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace riftwii::wii {

// Called before the menu's heap spills into MEM2. The shared CARD workarea
// also supplies the bounce buffer for raw EXI DMA; both must be in MEM1.
void InitGcRealCards();

// One read-only session. Removal is sticky until the next Open, even when
// another card is inserted while the player reads a confirmation popup.
class GcRealCard {
public:
    GcRealCard() = default;
    ~GcRealCard();
    GcRealCard(const GcRealCard&) = delete;
    GcRealCard& operator=(const GcRealCard&) = delete;

    int Open(int slot);
    void Close();
    int Read(std::vector<std::uint8_t>& raw);
    int ReadSystem(std::vector<std::uint8_t>& system);
    bool Unchanged(const std::vector<std::uint8_t>& raw);
    unsigned Bytes() const { return bytes_; }

private:
    int ReadBlocks(unsigned bytes, std::vector<std::uint8_t>& out);
    int slot_ = -1;
    unsigned bytes_ = 0;
    unsigned latency_ = 0;
};

}  // namespace riftwii::wii
