// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace riftwii::rpc {

// Starting RiftWii-RPC, in the background, when the player logs in: a
// launch agent on macOS, the Run key on Windows, an autostart entry on
// Linux desktops. Each points at this copy of the app, wherever it is.
bool autostartEnabled();
// False with `error` when the entry could not be written or removed.
bool setAutostart(bool on, QString& error);

}  // namespace riftwii::rpc
