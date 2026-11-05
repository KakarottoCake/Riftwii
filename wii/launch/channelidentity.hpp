// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace riftwii::wii {

// Keep working installed tickets first. On a copied NAND, also try a fresh
// generic d2x identity: removing a ticket's console binding alone can leave
// other fields that ES rejects. `attempt` records each answer for boot.log.
std::int32_t identify_channel(std::uint64_t title_id, const std::vector<std::uint8_t>& certs,
                              const std::vector<std::uint8_t>& tmd,
                              const std::vector<std::uint8_t>& ticket, bool ticket_synthetic,
                              bool emulated, std::string& attempt);

// Called only with full d2x NAND emulation active, before any game content
// has been copied over memory. Read the view from that NAND now, rather
// than keeping one from the real NAND before the IOS reload. A successful
// ES_LaunchTitle never returns; any return is recorded as a failed launch.
void launch_emulated_channel_with_system(std::uint64_t title_id, std::string& error);

}  // namespace riftwii::wii
