// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/channelupdate.hpp"

namespace riftwii {

ChannelUpdateState channel_update_state(bool installed, unsigned version, unsigned needed,
                                        bool installer_opened, bool installer_refused) {
    ChannelUpdateState state;
    state.required = installed && version < needed;
    state.clear_marker = installer_opened && (installer_refused || !state.required);
    state.installer_interrupted = state.required && installer_opened && !installer_refused;
    return state;
}

}  // namespace riftwii
