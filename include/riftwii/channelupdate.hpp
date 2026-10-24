// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii {

struct ChannelUpdateState {
    bool required = false;
    bool installer_interrupted = false;
    bool clear_marker = false;
};

// The installer removes its marker before leaving, even without an update.
// A refused hand-off or a channel that needs no update retires a stale marker.
ChannelUpdateState channel_update_state(bool installed, unsigned version, unsigned needed,
                                        bool installer_opened, bool installer_refused);

}  // namespace riftwii
