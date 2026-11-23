// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../movedchannels_mock/gccore.h"

s32 ES_GetNumTitles(u32* count);
s32 ES_GetTitles(u64* titles, u32 count);
s32 ISFS_ReadDir(const char* path, char* names, u32* count);
s32 ISFS_CreateDir(const char* path, u8 flags, u8 owner, u8 group, u8 other);
s32 ISFS_SetAttr(const char* path, u32 owner, u16 group, u8 flags, u8 owner_perm, u8 group_perm, u8 other_perm);
s32 ISFS_Rename(const char* from, const char* to);
