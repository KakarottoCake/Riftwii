// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "channelidentity.hpp"

#include <gccore.h>
#include <ogc/es.h>

#include "log.hpp"
#include "riftwii/nandchannel.hpp"

namespace riftwii::wii {

std::int32_t identify_channel(std::uint64_t title_id, const std::vector<std::uint8_t>& certs,
                              const std::vector<std::uint8_t>& tmd,
                              const std::vector<std::uint8_t>& ticket, bool ticket_synthetic,
                              bool emulated, std::string& attempt) {
    attempt = "IOS" + std::to_string(IOS_GetVersion()) + " rev " + std::to_string(IOS_GetRevision()) +
              ": ES_Identify ";
    const auto identify = [&](const std::vector<std::uint8_t>& offered, const char* kind) {
        u32 key = 0;
        const s32 result = ES_Identify(reinterpret_cast<const signed_blob*>(certs.data()),
                                       static_cast<u32>(certs.size()),
                                       reinterpret_cast<const signed_blob*>(tmd.data()),
                                       static_cast<u32>(tmd.size()),
                                       reinterpret_cast<const signed_blob*>(offered.data()),
                                       static_cast<u32>(offered.size()), &key);
        logf("Wii channels: on IOS%d rev %d, ES %s the %s ticket (%d%s).\n",
             IOS_GetVersion(), IOS_GetRevision(), result >= 0 ? "accepted" : "refused", kind, result,
             result == -1029 ? ", invalid ticket" : "");
        attempt += std::string(result >= 0 ? "accepted the " : "refused the ") + kind + " ticket (" +
                   std::to_string(result) + ")";
        return result;
    };

    s32 result = identify(ticket, ticket_synthetic ? "synthetic" : "installed");
    if (result < 0 && !ticket_synthetic) {
        attempt += "; ";
        result = identify(make_identify_ticket(ticket, title_id), "synthetic");
    }
    if (result < 0 && emulated) {
        // A copied VC ticket can still be refused after its console fields
        // are cleared (#437). d2x's channel Identify also accepts a generic
        // ticket without a title key, console or title of its own (the
        // channel identity used by USB Loader GX). ES takes the title from
        // the TMD and the already decrypted contents stay in the copied NAND.
        // Keep this after the existing attempts so working games keep theirs.
        logf("Wii channels: the copied NAND's ticket was refused; trying a fresh generic d2x identity so the game can keep RiftWii's extras.\n");
        attempt += "; ";
        result = identify(make_fake_ticket(), "generic");
    }
    return result;
}

void launch_emulated_channel_with_system(std::uint64_t title_id, std::string& error) {
    u32 count = 0;
    s32 result = ES_GetNumTicketViews(title_id, &count);
    if (result < 0 || count == 0) {
        error = "the copied NAND's ticket views could not be read (" + std::to_string(result) +
                ", " + std::to_string(count) + " views)";
        return;
    }

    // Only the first view is needed, even if the ticket holds many. Never
    // allocate an array sized by a file on the player's card.
    alignas(32) tikview view{};
    result = ES_GetTicketViews(title_id, &view, 1);
    if (result < 0) {
        error = "the copied NAND's first ticket view could not be read (" + std::to_string(result) + ")";
        return;
    }
    logf("Wii channels: the ticket view was read with d2x's NAND emulation active; the Wii starts %08x-%08x without RiftWii's extras because RiftWii's loader could not start it.\n",
         static_cast<unsigned>(title_id >> 32), static_cast<unsigned>(title_id));
    result = ES_LaunchTitle(title_id, &view);
    error = "the Wii's own launch returned (" + std::to_string(result) + ")";
}

}  // namespace riftwii::wii
