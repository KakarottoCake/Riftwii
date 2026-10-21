// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gcrealcard.hpp"
#include "riftwii/gcreal.hpp"
#include <gccore.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iostream>
#include <string>

namespace {
unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

alignas(32) std::array<u8, CARD_WORKAREA_SIZE> workarea;
struct Bus {
    unsigned flags = 0;
    unsigned address = 0;
    EXICallback callback = nullptr;
    EXICallback unlock_callback = nullptr;
    cardcallback detach = nullptr;
};
Bus buses[2];
int probe_result = 0;
int mount_result = 0;
int card_mbits = 4;
bool id_ok = true;
bool select_ok = true;
bool sync_ok = true;
bool dma_ok = true;
bool remove_during_dma = false;
bool remove_during_mount = false;
bool allocation_fails = false;
bool wii_u = false;
unsigned dma_calls = 0;
unsigned mount_calls = 0;
unsigned unmount_calls = 0;
std::string g_log;
// What libogc's SRAM holds for each slot's card, and whether a mount changes it.
syssramex fake_sram{};
bool mount_changes_sram_id = false;

void reset_faults() {
    probe_result = mount_result = 0;
    card_mbits = 4;
    id_ok = select_ok = sync_ok = dma_ok = true;
    remove_during_dma = remove_during_mount = wii_u = false;
    CHECK(buses[0].flags == 0 && buses[1].flags == 0);
}

void closed(int slot) {
    CHECK(buses[slot].flags == 0);
    CHECK(buses[slot].unlock_callback == nullptr);
}

s32 previous_callback(s32, s32) { return 1; }
}  // namespace

// The real numeric address checks run in gcreal_tests. Here the backing
// array represents MEM1 so the actual Wii adapter can run on either host.
namespace riftwii {
bool gc_real_dma_range(std::uintptr_t address, std::size_t bytes) {
    return address == reinterpret_cast<std::uintptr_t>(workarea.data()) &&
           (bytes == CARD_WORKAREA_SIZE || bytes == CARD_READSIZE);
}
}  // namespace riftwii

namespace riftwii::wii {
bool is_wii_u() { return wii_u; }
void logf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log += line;
}
}  // namespace riftwii::wii

void* test_card_memalign(std::size_t alignment, std::size_t bytes) {
    CHECK(alignment == 32 && bytes == CARD_WORKAREA_SIZE);
    if (allocation_fails) return nullptr;
    return workarea.data();
}

int CARD_Init(const char*, const char*) { return 0; }
int CARD_ProbeEx(int, s32* mbits, s32* sector) {
    *mbits = card_mbits;
    *sector = 8192;
    return probe_result;
}
extern "C" syssramex* __SYS_LockSramEx(void) { return &fake_sram; }
extern "C" u32 __SYS_UnlockSramEx(u32) { return 1; }
int CARD_Mount(int slot, void* buffer, cardcallback detach) {
    ++mount_calls;
    if (mount_changes_sram_id) ++fake_sram.flash_id[slot][0];
    CHECK(buffer == workarea.data());
    buses[slot].flags |= EXI_FLAG_ATTACH;
    buses[slot].detach = detach;
    buses[slot].callback = nullptr;
    if (remove_during_mount) detach(slot, CARD_ERROR_NOCARD);
    return mount_result;
}
int CARD_Unmount(int slot) {
    ++unmount_calls;
    CHECK(buses[slot].flags & EXI_FLAG_LOCKED);
    buses[slot].flags &= ~EXI_FLAG_ATTACH;
    buses[slot].detach = nullptr;
    buses[slot].callback = nullptr;
    return 0;
}
s32 EXI_GetState(s32 slot) { return buses[slot].flags; }
s32 EXI_GetID(s32, s32, u32* id) {
    *id = 4;
    return id_ok;
}
s32 EXI_Lock(s32 slot, s32, EXICallback callback) {
    Bus& bus = buses[slot];
    if (bus.flags & EXI_FLAG_LOCKED) {
        if (callback) {
            CHECK(bus.unlock_callback == nullptr);
            bus.unlock_callback = callback;
        }
        return 0;
    }
    bus.flags |= EXI_FLAG_LOCKED;
    return 1;
}
s32 EXI_Unlock(s32 slot) {
    Bus& bus = buses[slot];
    CHECK(bus.flags & EXI_FLAG_LOCKED);
    CHECK(!(bus.flags & EXI_FLAG_SELECT));
    bus.flags &= ~EXI_FLAG_LOCKED;
    const EXICallback callback = bus.unlock_callback;
    bus.unlock_callback = nullptr;
    if (callback) callback(slot, 0);
    return 1;
}
s32 EXI_Select(s32 slot, s32, s32) {
    CHECK(buses[slot].flags & EXI_FLAG_LOCKED);
    if (!select_ok) return 0;
    buses[slot].flags |= EXI_FLAG_SELECT;
    return 1;
}
s32 EXI_Deselect(s32 slot) {
    CHECK(buses[slot].flags & EXI_FLAG_SELECT);
    buses[slot].flags &= ~EXI_FLAG_SELECT;
    return 1;
}
s32 EXI_ImmEx(s32 slot, void* data, u32 bytes, u32 direction) {
    CHECK(buses[slot].flags & EXI_FLAG_SELECT);
    CHECK(direction == EXI_WRITE);
    if (bytes == 5) {
        const auto* command = static_cast<const u8*>(data);
        CHECK(command[0] == 0x52 && command[3] == 0 && command[4] == 0);
        buses[slot].address = (unsigned(command[1]) << 17) | (unsigned(command[2]) << 9);
    }
    return 1;
}
s32 EXI_Dma(s32 slot, void* data, u32 bytes, u32 direction, EXICallback callback) {
    CHECK(buses[slot].flags & EXI_FLAG_SELECT);
    CHECK(data == workarea.data() && bytes == 512 && direction == EXI_READ && callback == nullptr);
    ++dma_calls;
    if (!dma_ok) return 0;
    buses[slot].flags |= EXI_FLAG_DMA;
    auto* buffer = static_cast<u8*>(data);
    for (unsigned i = 0; i < bytes; ++i)
        buffer[i] = static_cast<u8>((buses[slot].address + i) / 512);
    if (remove_during_dma) buses[slot].detach(slot, CARD_ERROR_NOCARD);
    return 1;
}
s32 EXI_Sync(s32 slot) {
    CHECK(buses[slot].flags & EXI_FLAG_DMA);
    buses[slot].flags &= ~EXI_FLAG_DMA;
    return sync_ok;
}
s32 EXI_Probe(s32) { return 1; }
EXICallback EXI_RegisterEXICallback(s32 slot, EXICallback callback) {
    const EXICallback before = buses[slot].callback;
    buses[slot].callback = callback;
    return before;
}
void DCInvalidateRange(void* buffer, unsigned bytes) {
    CHECK(buffer == workarea.data() && bytes == 512);
}
u64 gettime() {
    static u64 clock = 0;
    clock += 1000;
    return clock;
}
u64 ticks_to_millisecs(u64 ticks) { return ticks; }
void test_card_sleep(unsigned) {}

int main(int argc, char**) {
    using riftwii::wii::GcRealCard;
    GcRealCard card;
    if (argc > 1) {
        allocation_fails = true;
        riftwii::wii::InitGcRealCards();
        CHECK(card.Open(0) == CARD_ERROR_INSSPACE);
        CHECK(dma_calls == 0 && mount_calls == 0 && unmount_calls == 0);
        return failures == 0 ? 0 : 1;
    }
    riftwii::wii::InitGcRealCards();
    riftwii::wii::InitGcRealCards();
    std::vector<std::uint8_t> raw;
    for (int slot : {0, 1}) {
        buses[slot].callback = previous_callback;
        CHECK(card.Open(slot) == 0);
        CHECK(card.Read(raw) == 0 && raw.size() == (4u << 17));
        for (unsigned at = 0; at < raw.size(); ++at)
            CHECK(raw[at] == static_cast<u8>(at / 512));
        CHECK(card.Unchanged(raw));
        card.Close();
        card.Close();
        closed(slot);
        CHECK(buses[slot].callback == previous_callback);
        buses[slot].callback = nullptr;
    }

    // A failed mount owns its attachment and must clean it up on every retry.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        mount_result = CARD_ERROR_BROKEN;
        CHECK(card.Open(0) == CARD_ERROR_BROKEN);
        closed(0);
    }
    reset_faults();
    remove_during_mount = true;
    CHECK(card.Open(0) == CARD_ERROR_NOCARD);
    closed(0);
    reset_faults();
    const unsigned before_probe_failure = unmount_calls;
    probe_result = CARD_ERROR_NOCARD;
    CHECK(card.Open(0) == CARD_ERROR_NOCARD);
    CHECK(unmount_calls == before_probe_failure);
    reset_faults();
    id_ok = false;
    CHECK(card.Open(0) == CARD_ERROR_NOCARD);
    CHECK(unmount_calls == before_probe_failure);
    reset_faults();
    card_mbits = 256;
    CHECK(card.Open(0) == CARD_ERROR_LIMIT);
    reset_faults();
    wii_u = true;
    CHECK(card.Open(0) == CARD_ERROR_NOCARD);
    reset_faults();

    // Failed select, DMA start and completion each release only owned locks.
    for (int fault : {0, 1, 2, 3}) {
        CHECK(card.Open(0) == 0);
        select_ok = fault != 0;
        dma_ok = fault != 1;
        sync_ok = fault != 2;
        remove_during_dma = fault == 3;
        CHECK(card.ReadSystem(raw) < 0 && raw.empty());
        closed(0);
        reset_faults();
    }
    CHECK(card.Open(0) == 0);
    buses[0].detach(0, CARD_ERROR_NOCARD);
    CHECK(card.ReadSystem(raw) == CARD_ERROR_NOCARD && raw.empty());
    closed(0);

    // A lock owned by SRAM is neither selected nor unlocked by a failed read.
    CHECK(card.Open(0) == 0);
    buses[0].flags |= EXI_FLAG_LOCKED;
    CHECK(card.ReadSystem(raw) == CARD_ERROR_IOERROR && raw.empty());
    CHECK(buses[0].flags == (EXI_FLAG_ATTACH | EXI_FLAG_LOCKED));
    CHECK(card.Open(1) == CARD_ERROR_BUSY);
    EXI_Unlock(0);  // pending close now detaches the card
    closed(0);

    // Preexisting attachments and transfers are never unmounted or stolen.
    for (unsigned flag : {EXI_FLAG_ATTACH, EXI_FLAG_LOCKED, EXI_FLAG_SELECT, EXI_FLAG_DMA, EXI_FLAG_IMM}) {
        const unsigned before = unmount_calls;
        buses[0].flags = flag;
        CHECK(card.Open(0) == CARD_ERROR_BUSY);
        CHECK(buses[0].flags == flag && unmount_calls == before);
        buses[0].flags = 0;
    }
    {
        GcRealCard other;
        CHECK(card.Open(0) == 0);
        CHECK(other.Open(1) == CARD_ERROR_BUSY);
        CHECK(card.Open(1) == 0);  // re-open closes the previous owned slot
        closed(0);
        card.Close();
    }
    closed(1);
    CHECK(card.Open(0) == 0);
    card.Close();
    // The SRAM's card ID is logged when a mount changes it, and only then.
    CHECK(g_log.find("card ID the Wii keeps in its SRAM") == std::string::npos);
    mount_changes_sram_id = true;
    CHECK(card.Open(1) == 0);
    card.Close();
    mount_changes_sram_id = false;
    CHECK(g_log.find("changed the card ID the Wii keeps in its SRAM for that slot") != std::string::npos);
    // The progress callback sees every 512-byte read, ending at the total.
    CHECK(card.Open(0) == 0);
    unsigned calls = 0, last_done = 0, last_total = 0;
    card.SetProgress([&](unsigned done, unsigned total) {
        ++calls;
        last_done = done;
        last_total = total;
    });
    CHECK(card.ReadSystem(raw) == 0);
    CHECK(calls == raw.size() / 512 && last_done == raw.size() && last_total == raw.size());
    card.Close();
    CHECK(g_log.find("libogc said -6") != std::string::npos);
    CHECK(g_log.find("failed its flash read") != std::string::npos);
    CHECK(g_log.find("in MEM1") != std::string::npos);
    return failures == 0 ? 0 : 1;
}
