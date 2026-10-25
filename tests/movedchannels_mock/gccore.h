// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gctypes.h"

using signed_blob = u32;
struct fstats {
    u32 file_length;
};

constexpr u8 ISFS_OPEN_READ = 1;
constexpr u8 ISFS_OPEN_WRITE = 2;
constexpr u8 ISFS_OPEN_RW = 3;

s32 ES_GetStoredTMDSize(u64 title, u32* size);
s32 ES_GetStoredTMD(u64 title, signed_blob* data, u32 size);
s32 ISFS_Open(const char* path, u8 mode);
s32 ISFS_Close(s32 fd);
s32 ISFS_GetFileStats(s32 fd, fstats* stats);
s32 ISFS_GetAttr(const char* path, u32* owner, u16* group, u8* attribute,
                 u8* owner_perm, u8* group_perm, u8* other_perm);
s32 ISFS_Delete(const char* path);
s32 ISFS_CreateFile(const char* path, u8 attribute, u8 owner_perm, u8 group_perm, u8 other_perm);
s32 ISFS_Write(s32 fd, const void* data, u32 size);
