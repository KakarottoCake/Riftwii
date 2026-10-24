// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii {

struct ChannelUpdateState {
    bool required = false;
    bool installer_interrupted = false;
    bool clear_marker = false;
};

// A marker survives an installer that never returns. A completed return
// or a channel that no longer needs updating retires that attempt.
ChannelUpdateState channel_update_state(bool installed, unsigned version, unsigned needed,
                                        bool installer_opened, bool installer_returned);

}  // namespace riftwii
