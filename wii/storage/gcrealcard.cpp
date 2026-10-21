// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2004 Michael Wiedenbauer (shagkur)
// SPDX-FileCopyrightText: 2004 Dave Murphy (WinterMute)
// SPDX-License-Identifier: GPL-3.0-or-later
// The card read protocol is documented in NOTICE.md. No
// save-manager code is used; all layout validation belongs to gccard.cpp.
#include "gcrealcard.hpp"

#include <gccore.h>
#include <ogc/card.h>
#include <ogc/exi.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "boot.hpp"
#include "log.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/gcreal.hpp"

// libogc's SRAM copy (libogc/system.c), not in its headers: the card IDs the
// Wii keeps for slots A and B.
extern "C" {
syssramex* __SYS_LockSramEx(void);
u32 __SYS_UnlockSramEx(u32 write);
}

namespace riftwii::wii {
namespace {

volatile bool removed[2] = {false, false};
void detached(s32 slot, s32) { removed[slot] = true; }

void* card_workarea = nullptr;
volatile int session_slot = -1;
EXICallback previous_exi_callback = nullptr;

// SRAM shares channel A with the card. If another device owns the channel
// at close, finish detaching when it unlocks rather than touching its lock.
// The workarea lives for the process, so no callback can reach freed memory.
s32 close_session(s32 slot, s32) {
    u32 level;
    _CPU_ISR_Disable(level);
    if (!EXI_Lock(slot, EXI_DEVICE_0, close_session)) {
        _CPU_ISR_Restore(level);
        return 0;
    }
    CARD_Unmount(slot);
    EXI_RegisterEXICallback(slot, previous_exi_callback);
    EXI_Unlock(slot);
    session_slot = -1;
    _CPU_ISR_Restore(level);
    return 1;
}

// The card ID the Wii keeps in its SRAM for `slot`. CARD_Mount updates it
// to the mounted card's, for slot B as well as A: the card is not written,
// the console's own memory is.
std::array<u8, 12> sram_card_id(int slot) {
    std::array<u8, 12> id{};
    syssramex* sram = __SYS_LockSramEx();
    if (sram != nullptr) std::memcpy(id.data(), sram->flash_id[slot], id.size());
    __SYS_UnlockSramEx(0);
    return id;
}

// A synchronous, read-only EXI transaction. Each DMA transfers a fixed 512
// bytes and is joined before its buffer can be reused, even on removal.
// Keeping reads synchronous avoids CARD's async removal callback cancelling
// its watchdog while a raw-read completion is still pending.
int read_sector(int slot, unsigned address, unsigned latency, void* buffer) {
    if (!gc_real_dma_range(reinterpret_cast<std::uintptr_t>(buffer), CARD_READSIZE))
        return CARD_ERROR_FATAL_ERROR;
    const u64 started = gettime();
    while (!EXI_Lock(slot, EXI_DEVICE_0, nullptr)) {
        if (removed[slot] || ticks_to_millisecs(gettime() - started) >= 1000)
            return CARD_ERROR_IOERROR;
        usleep(1000);
    }
    if (removed[slot] || !EXI_Select(slot, EXI_DEVICE_0, EXI_SPEED16MHZ)) {
        EXI_Unlock(slot);
        return CARD_ERROR_NOCARD;
    }
    // Address fields are the card's page/sector address, not an SD LBA.
    // The last two fields are zero because reads start on 512-byte boundaries.
    u8 command[5] = {0x52, static_cast<u8>(address >> 17), static_cast<u8>(address >> 9), 0, 0};
    u8 dummy[512] = {};
    DCInvalidateRange(buffer, CARD_READSIZE);
    bool ok = EXI_ImmEx(slot, command, sizeof(command), EXI_WRITE) &&
              EXI_ImmEx(slot, dummy, latency, EXI_WRITE);
    if (ok) {
        const bool dma_started = EXI_Dma(slot, buffer, CARD_READSIZE, EXI_READ, nullptr);
        ok = dma_started && EXI_Sync(slot);
    }
    if (!EXI_Deselect(slot)) ok = false;
    EXI_Unlock(slot);
    if (removed[slot] || !EXI_Probe(slot)) return CARD_ERROR_NOCARD;
    return ok ? CARD_ERROR_READY : CARD_ERROR_IOERROR;
}

}  // namespace

void InitGcRealCards() {
    if (card_workarea) return;
    void* workarea = memalign(32, CARD_WORKAREA_SIZE);
    if (!gc_real_dma_range(reinterpret_cast<std::uintptr_t>(workarea), CARD_WORKAREA_SIZE)) {
        std::free(workarea);
        return;
    }
    card_workarea = workarea;
}

GcRealCard::~GcRealCard() { Close(); }

void GcRealCard::Close() {
    if (slot_ >= 0) close_session(slot_, 0);
    slot_ = -1;
    bytes_ = 0;
    latency_ = 0;
}

int GcRealCard::Open(int slot) {
    Close();
    if (is_wii_u() || slot < 0 || slot > 1) return CARD_ERROR_NOCARD;
    const char slot_name = static_cast<char>('A' + slot);
    if (!card_workarea) {
        logf("Memory card: slot %c was not opened because no MEM1 DMA workarea is available (libogc code %d).\n",
             slot_name, CARD_ERROR_INSSPACE);
        return CARD_ERROR_INSSPACE;
    }
    // Never unmount a different client or overwrite its pending transfer.
    const unsigned busy_flags = EXI_FLAG_ATTACH | EXI_FLAG_LOCKED | EXI_FLAG_SELECT | EXI_FLAG_DMA | EXI_FLAG_IMM;
    if (session_slot >= 0 || (EXI_GetState(slot) & busy_flags)) {
        logf("Memory card: slot %c was not opened because EXI is already in use (libogc code %d).\n",
             slot_name, CARD_ERROR_BUSY);
        return CARD_ERROR_BUSY;
    }
    // libogc's own card_inited guard makes this a one-time initialization.
    CARD_Init(nullptr, nullptr);
    s32 mbits = 0;
    s32 sector = 0;
    const s32 probe = CARD_ProbeEx(slot, &mbits, &sector);
    if (probe < 0) {
        logf("Memory card: nothing answered as a memory card in slot %c (libogc's probe said %d)\n", slot_name,
             static_cast<int>(probe));
        return probe;
    }
    if (mbits < 4 || mbits > 128 || (mbits & (mbits - 1)) != 0 || sector != 0x2000) {
        logf("Memory card: the card in slot %c is not a size RiftWii reads (%d Mbit, %d-byte sectors; libogc code %d).\n",
             slot_name, static_cast<int>(mbits), static_cast<int>(sector), CARD_ERROR_LIMIT);
        return CARD_ERROR_LIMIT;
    }
    u32 device_id = 0;
    if (!EXI_GetID(slot, EXI_DEVICE_0, &device_id)) {
        logf("Memory card: slot %c failed the device ID read (libogc code %d).\n", slot_name, CARD_ERROR_NOCARD);
        return CARD_ERROR_NOCARD;
    }
    removed[slot] = false;
    session_slot = slot;
    slot_ = slot;
    previous_exi_callback = EXI_RegisterEXICallback(slot, nullptr);
    const std::array<u8, 12> sram_id_before = sram_card_id(slot);
    const s32 mounted = CARD_Mount(slot, card_workarea, detached);
    if (mounted < 0) {
        logf("Memory card: the %d Mbit card in slot %c could not be mounted (libogc said %d; the DMA workarea is %p in MEM1).\n",
             static_cast<int>(mbits), slot_name, static_cast<int>(mounted), card_workarea);
        Close();
        return mounted;
    }
    if (removed[slot]) {
        logf("Memory card: slot %c was removed during mount (libogc code %d).\n", slot_name, CARD_ERROR_NOCARD);
        Close();
        return CARD_ERROR_NOCARD;
    }
    if (sram_card_id(slot) != sram_id_before)
        logf("Memory card: mounting the card in slot %c changed the card ID the Wii keeps in its SRAM for that slot; the card itself was not written to.\n",
             slot_name);
    latency_ = 4u << ((device_id >> 8) & 7u);
    bytes_ = static_cast<unsigned>(mbits) << 17;
    logf("Memory card: the %d Mbit card in slot %c was mounted with its DMA workarea at %p in MEM1 (libogc said %d).\n",
         static_cast<int>(mbits), slot_name, card_workarea, static_cast<int>(mounted));
    return CARD_ERROR_READY;
}

int GcRealCard::ReadBlocks(unsigned bytes, std::vector<std::uint8_t>& out) {
    out.clear();
    if (slot_ < 0) return CARD_ERROR_NOCARD;
    if (removed[slot_]) {
        logf("Memory card: slot %c was removed before its flash read (libogc code %d).\n", 'A' + slot_, CARD_ERROR_NOCARD);
        Close();
        return CARD_ERROR_NOCARD;
    }
    // CARD_Mount has finished all DMA. Its MEM1 workarea is now our bounce
    // buffer; vectors and ordinary memalign allocations may be in MEM2.
    auto* buffer = static_cast<u8*>(card_workarea);
    // A big third-party card is 16 MB, read whole. The DOL is built without
    // exceptions, so a vector that cannot grow aborts the menu: ask malloc
    // first, which says no instead, and report the card as unreadable.
    void* room = std::malloc(bytes);
    if (!room) {
        logf("Memory card: no room for a copy of the %u KB card in slot %c; nothing was read (libogc code %d).\n",
             bytes >> 10, 'A' + slot_, CARD_ERROR_INSSPACE);
        Close();
        return CARD_ERROR_INSSPACE;
    }
    std::free(room);
    out.resize(bytes);
    s32 result = CARD_ERROR_READY;
    for (unsigned at = 0; at < bytes; at += CARD_READSIZE) {
        result = read_sector(slot_, at, latency_, buffer);
        if (result < 0) {
            logf("Memory card: slot %c failed its flash read at byte %u (libogc code %d).\n",
                 'A' + slot_, at, static_cast<int>(result));
            break;
        }
        std::memcpy(out.data() + at, buffer, CARD_READSIZE);
        if (progress_) progress_(at + CARD_READSIZE, bytes);
    }
    if (result < 0) {
        out.clear();
        Close();
    }
    return result;
}

int GcRealCard::Read(std::vector<std::uint8_t>& raw) { return ReadBlocks(bytes_, raw); }

int GcRealCard::ReadSystem(std::vector<std::uint8_t>& system) {
    return ReadBlocks(kGcCardSystemBlocks * kGcCardBlockBytes, system);
}

bool GcRealCard::Unchanged(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> system;
    return raw.size() >= kGcCardSystemBlocks * kGcCardBlockBytes && ReadSystem(system) >= 0 &&
           slot_ >= 0 && !removed[slot_] && std::equal(system.begin(), system.end(), raw.begin());
}

}  // namespace riftwii::wii
