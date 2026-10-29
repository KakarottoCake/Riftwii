// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using signed_blob = u32;
// Opaque ticket bytes, with libogc's size to exercise array alignment.
struct tikview { u8 bytes[216]; };
s32 ES_GetStoredTMDSize(u64, u32*);
s32 ES_GetStoredTMD(u64, signed_blob*, u32);
s32 ES_GetNumTicketViews(u64, u32*);
s32 ES_GetTicketViews(u64, tikview*, u32);
s32 ES_OpenTitleContent(u64, const tikview*, u16);
s32 ES_SeekContent(s32, s32, s32);
s32 ES_ReadContent(s32, void*, u32);
s32 ES_CloseContent(s32);
