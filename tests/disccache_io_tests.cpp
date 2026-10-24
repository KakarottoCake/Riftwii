// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "di.hpp"
#include <gccore.h>

#include <iostream>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    ++failures; \
} } while (0)

static s32 reply = 1;
static bool inserted = true;
static unsigned disc_reads = 0;
static unsigned cover_checks = 0;

s32 IOS_Open(const char*, u32) { return 42; }
s32 IOS_Close(s32 descriptor) {
    CHECK(descriptor == 42);
    return 0;
}
s32 IOS_Ioctl(s32 descriptor, s32 command, void*, u32, void* output, u32) {
    CHECK(descriptor == 42);
    if (command == 0x88) {
        ++cover_checks;
        *static_cast<u32*>(output) = inserted ? 2 : 0;
    }
    if (command == 0x70 || command == 0x71 || command == 0x8D) ++disc_reads;
    return reply;
}
s32 IOS_Ioctlv(s32, s32, u32, u32, ioctlv*) { return reply; }

int main() {
    namespace di = riftwii::di;
    using Action = riftwii::DiscCache::Action;
    const riftwii::CachedDisc disc{"RFTE01", "Test disc", 2, 1, true};
    std::string error;
    bool present = false;
    CHECK(di::open(error));
    di::disc_cache().probe_succeeded(disc);
    for (unsigned visit = 0; visit < 4; ++visit) {
        CHECK(di::cover_status(present, error));
        CHECK(present);
        CHECK(di::disc_cache().action() == Action::Reuse);
    }
    CHECK(cover_checks == 4);
    CHECK(disc_reads == 0);

    inserted = false;
    CHECK(di::cover_status(present, error));
    CHECK(di::disc_cache().action() == Action::Empty);
    inserted = true;
    CHECK(di::cover_status(present, error));
    CHECK(di::disc_cache().action() == Action::Probe);

    di::disc_cache().probe_succeeded(disc);
    CHECK(di::reset(true, error));
    CHECK(di::disc_cache().action() == Action::Probe);

    for (s32 failure : {-1, 2}) {
        reply = failure;
        di::disc_cache().probe_succeeded(disc);
        CHECK(!di::cover_status(present, error));
        CHECK(di::disc_cache().action() == Action::Probe);
        di::disc_cache().probe_succeeded(disc);
        alignas(32) std::uint8_t buffer[32]{};
        CHECK(!di::read_disc_id(buffer, error));
        CHECK(di::disc_cache().action() == Action::Probe);
        di::disc_cache().probe_succeeded(disc);
        CHECK(!di::read_unencrypted(buffer, sizeof(buffer), 0, error));
        CHECK(di::disc_cache().action() == Action::Probe);
        di::disc_cache().probe_succeeded(disc);
        CHECK(!di::read(buffer, sizeof(buffer), 0, error));
        CHECK(di::disc_cache().action() == Action::Probe);
    }

    reply = 1;
    di::disc_cache().probe_succeeded(disc);
    di::close();  // reload_ios closes DI before touching IOS
    CHECK(di::disc_cache().action() == Action::Probe);
    CHECK(di::open(error));
    CHECK(di::cover_status(present, error));
    CHECK(di::disc_cache().action() == Action::Probe);
    di::close();
    if (failures != 0) return 1;
    std::cout << "DI cover polling performs no reads; removal, reset, close and read failures discard identity\n";
}
