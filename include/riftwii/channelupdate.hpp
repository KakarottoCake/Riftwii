// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii {

enum class ChannelInstallerAttempt { None, Opened, Returned };

struct ChannelUpdateState {
    bool required = false;
    bool installer_interrupted = false;
    bool installer_returned = false;
    bool offer_not_now = false;
    bool clear_marker = false;
};

// The installer records Returned when leaving without reaching its package version.
// A refused hand-off or a channel that needs no update retires a stale marker.
ChannelUpdateState channel_update_state(bool installed, unsigned version, unsigned needed,
                                        ChannelInstallerAttempt attempt, bool installer_refused);

}  // namespace riftwii
