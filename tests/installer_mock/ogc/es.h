// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gccore.h>

struct tmd_view_content {
    u32 cid;
};

struct tmd_view {
    u64 title_id;
    u16 num_contents;
    tmd_view_content contents[1];
};

s32 ES_GetTMDViewSize(u64 title, u32* size);
s32 ES_GetTMDView(u64 title, tmd_view* view, u32 size);
