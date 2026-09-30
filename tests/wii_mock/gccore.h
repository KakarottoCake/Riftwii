// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
using s32 = std::int32_t;
using u32 = std::uint32_t;
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
#define MEM_VIRTUAL_TO_PHYSICAL(p) static_cast<u32>(reinterpret_cast<std::uintptr_t>(p) & 0x7fffffff)
struct ioctlv { void* data; u32 len; };
s32 IOS_Open(const char*, u32);
s32 IOS_Close(s32);
s32 IOS_Ioctl(s32, s32, void*, u32, void*, u32);
s32 IOS_Ioctlv(s32, s32, u32, u32, ioctlv*);
