// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>

constexpr int ISFS_OPEN_READ = 1;
s32 ISFS_Open(const char* path, int mode);
s32 ISFS_Read(s32 fd, void* data, u32 size);
s32 ISFS_Close(s32 fd);
