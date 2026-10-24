// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/channelupdate.hpp"

namespace riftwii {

ChannelUpdateState channel_update_state(bool installed, unsigned version, unsigned needed,
                                        ChannelInstallerAttempt attempt, bool installer_refused) {
    ChannelUpdateState state;
    state.required = installed && version < needed;
    state.clear_marker = attempt != ChannelInstallerAttempt::None && (installer_refused || !state.required);
    state.installer_interrupted = state.required && !installer_refused && attempt == ChannelInstallerAttempt::Opened;
    state.installer_returned = state.required && !installer_refused && attempt == ChannelInstallerAttempt::Returned;
    state.offer_not_now = state.required && (installer_refused || state.installer_interrupted || state.installer_returned);
    return state;
}

}  // namespace riftwii
