// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using EXICallback = s32 (*)(s32, s32);
using cardcallback = void (*)(s32, s32);

constexpr unsigned CARD_WORKAREA_SIZE = 5 * 8192;
constexpr unsigned CARD_READSIZE = 512;
constexpr int CARD_ERROR_READY = 0;
constexpr int CARD_ERROR_BUSY = -1;
constexpr int CARD_ERROR_NOCARD = -3;
constexpr int CARD_ERROR_IOERROR = -5;
constexpr int CARD_ERROR_BROKEN = -6;
constexpr int CARD_ERROR_INSSPACE = -9;
constexpr int CARD_ERROR_LIMIT = -11;
constexpr int CARD_ERROR_FATAL_ERROR = -128;
constexpr unsigned EXI_FLAG_DMA = 1;
constexpr unsigned EXI_FLAG_IMM = 2;
constexpr unsigned EXI_FLAG_SELECT = 4;
constexpr unsigned EXI_FLAG_ATTACH = 8;
constexpr unsigned EXI_FLAG_LOCKED = 16;
constexpr int EXI_DEVICE_0 = 0;
constexpr int EXI_SPEED16MHZ = 4;
constexpr int EXI_READ = 0;
constexpr int EXI_WRITE = 1;

// libogc's SRAM-ex copy (libogc/system.c), as wii/storage/gcrealcard.cpp
// declares it: the card IDs the Wii keeps for slots A and B.
struct syssramex {
    u8 flash_id[2][12];
};
extern "C" syssramex* __SYS_LockSramEx(void);
extern "C" u32 __SYS_UnlockSramEx(u32 write);

#define _CPU_ISR_Disable(level) ((level) = 0)
#define _CPU_ISR_Restore(level) static_cast<void>(level)

int CARD_Init(const char*, const char*);
int CARD_ProbeEx(int slot, s32* mbits, s32* sector);
int CARD_Mount(int slot, void* workarea, cardcallback detach);
int CARD_Unmount(int slot);
s32 EXI_GetState(s32 slot);
s32 EXI_GetID(s32 slot, s32 device, u32* id);
s32 EXI_Lock(s32 slot, s32 device, EXICallback callback);
s32 EXI_Unlock(s32 slot);
s32 EXI_Select(s32 slot, s32 device, s32 speed);
s32 EXI_Deselect(s32 slot);
s32 EXI_ImmEx(s32 slot, void* data, u32 bytes, u32 direction);
s32 EXI_Dma(s32 slot, void* data, u32 bytes, u32 direction, EXICallback callback);
s32 EXI_Sync(s32 slot);
s32 EXI_Probe(s32 slot);
EXICallback EXI_RegisterEXICallback(s32 slot, EXICallback callback);
void DCInvalidateRange(void* buffer, unsigned bytes);
u64 gettime();
u64 ticks_to_millisecs(u64 ticks);
