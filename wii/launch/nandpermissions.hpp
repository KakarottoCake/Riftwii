// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

bool open_nand_permissions(const char* who);
void close_nand_permissions(const char* who);
void RecoverNandPermissions();
void LogNandPermissionRecovery();

}  // namespace riftwii::wii
