// SPDX-License-Identifier: GPL-3.0-or-later
#include "restart.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/audio.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>

#include <cstring>

#include "log.hpp"
#include "memlimits.hpp"

// crt0's pieces (libogc's linker script and tuxedo/common_crt0.S).
extern "C" {
extern char __CTOR_LIST__[];
extern char __sbss_start[];
extern void __app_start();
void __real___CheckARGV();
void __wrap___CheckARGV();
}

namespace riftwii::wii {
namespace {

constexpr u32 kSnapshotMagic = 0x52575350;  // "RWSP"
constexpr u32 kHandoffMagic = 0x52574844;   // "RWHD"
constexpr u32 kSnapshotMax = 0x8000;
constexpr u32 kArgvMax = 512;
constexpr u32 kMessageMax = 1024;

struct Snapshot {
    u32 magic;
    u32 size;
    u8 data[kSnapshotMax];
};

struct Handoff {
    u32 magic;
    u32 kind;
    u32 image;  // the image's data size: a different build ignores the record
    u32 crash_restarts;
    u32 argv_length;
    char argv[kArgvMax];
    char message[kMessageMax];
};

static_assert(sizeof(Snapshot) + sizeof(Handoff) <= mem::kRestartBytes, "restart area too small");

Snapshot* snapshot() { return reinterpret_cast<Snapshot*>(mem::kRestartArea); }
Handoff* handoff() { return reinterpret_cast<Handoff*>(mem::kRestartArea + sizeof(Snapshot)); }
u32 data_size() { return static_cast<u32>(__sbss_start - __CTOR_LIST__); }

RestartNote g_note;

}  // namespace
}  // namespace riftwii::wii

// Runs inside crt0, on the exception stack, before .bss is cleared and
// before libogc is up: fixed addresses and memcpy only.
extern "C" void __wrap___CheckARGV() {
    using namespace riftwii::wii;
    Snapshot* s = snapshot();
    const u32 size = data_size();
    if (size <= kSnapshotMax) {
        // On a restart the data was just put back from this very copy, so
        // taking it again changes nothing.
        std::memcpy(s->data, __CTOR_LIST__, size);
        s->size = size;
        s->magic = kSnapshotMagic;
    } else {
        s->magic = 0;
    }
    Handoff* h = handoff();
    if (h->magic == kHandoffMagic && h->image == size) {
        // The old run's argv strings lived in its heap; give crt0 the copy.
        if (h->argv_length != 0) {
            __system_argv->argvMagic = ARGV_MAGIC;
            __system_argv->commandLine = h->argv;
            __system_argv->length = static_cast<int>(h->argv_length);
        } else {
            __system_argv->argvMagic = 0;
        }
    }
    __real___CheckARGV();
}

namespace riftwii::wii {

RestartNote TakeRestartNote() {
    Handoff* h = handoff();
    if (h->magic == kHandoffMagic && h->image == data_size()) {
        g_note.kind = static_cast<RestartKind>(h->kind);
        h->message[kMessageMax - 1] = '\0';
        g_note.message = h->message;
        g_note.crash_restarts = h->crash_restarts;
    }
    h->magic = 0;
    return g_note;
}

const RestartNote& CurrentRestartNote() { return g_note; }

bool CanRestart() { return snapshot()->magic == kSnapshotMagic && snapshot()->size == data_size(); }

bool WarmRestart(RestartKind kind, const std::string& message, bool unmount) {
    if (!CanRestart()) return false;
    Handoff* h = handoff();
    h->kind = static_cast<u32>(kind);
    h->image = data_size();
    h->crash_restarts = kind == RestartKind::Crashed ? g_note.crash_restarts + 1 : 0;
    std::strncpy(h->message, message.c_str(), kMessageMax - 1);
    h->message[kMessageMax - 1] = '\0';
    h->argv_length = 0;
    if (__system_argv->argvMagic == ARGV_MAGIC && __system_argv->commandLine != nullptr &&
        __system_argv->length > 0 && static_cast<u32>(__system_argv->length) <= kArgvMax) {
        std::memcpy(h->argv, __system_argv->commandLine, __system_argv->length);
        h->argv_length = static_cast<u32>(__system_argv->length);
    }
    h->magic = kHandoffMagic;

    logf("Restarting RiftWii\n");
    LogClose();
    if (unmount) fatUnmount("sd:");
    AUDIO_StopDMA();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level;
    _CPU_ISR_Disable(level);
    (void)level;
    const Snapshot* s = snapshot();
    std::memcpy(__CTOR_LIST__, s->data, s->size);
    DCFlushRange(__CTOR_LIST__, s->size);
    ICInvalidateRange(__CTOR_LIST__, s->size);
    __app_start();
    return false;  // not reached
}

}  // namespace riftwii::wii
