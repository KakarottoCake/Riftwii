// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual channel identity and system-launch clients against ES.
#include "channelidentity.hpp"

#include <gccore.h>
#include <ogc/es.h>

#include <cstdarg>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "riftwii/nandchannel.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

constexpr u64 kTitle = 0x000100014E414345ull;
const std::vector<u8> certs(32, 0x11);
const std::vector<u8> tmd(32, 0x22);
std::vector<s32> replies;
std::vector<std::vector<u8>> offered;
u32 view_count = 1;
s32 count_reply = 0;
s32 view_reply = 0;
s32 launch_reply = -1029;
unsigned view_reads = 0;
unsigned launches = 0;

std::vector<u8> installed_ticket() {
    std::vector<u8> ticket(riftwii::kSignedTicketBytes, 0x5A);
    ticket[0] = 0;
    ticket[1] = 1;
    ticket[2] = 0;
    ticket[3] = 1;
    return ticket;
}

void test_identity() {
    const auto installed = installed_ticket();
    const auto synthetic = riftwii::make_identify_ticket(installed, kTitle);
    const auto generic = riftwii::make_fake_ticket();
    std::string attempt;
    const auto run = [&](const std::vector<s32>& answers, bool emulated, bool missing_ticket = false) {
        replies = answers;
        offered.clear();
        return riftwii::wii::identify_channel(kTitle, certs, tmd, missing_ticket ? synthetic : installed,
                                             missing_ticket, emulated, attempt);
    };

    // Working WiiWare and real-NAND channels keep the exact installed
    // ticket and stop without creating or offering another identity.
    for (bool emulated : {false, true}) {
        CHECK(run({0}, emulated) == 0);
        CHECK(offered == std::vector<std::vector<u8>>{installed});
        CHECK(attempt.find("accepted the installed ticket (0)") != std::string::npos);
        CHECK(run({-1029, 0}, emulated) == 0);
        CHECK((offered == std::vector<std::vector<u8>>{installed, synthetic}));
    }

    // The #437 failure: both the original and its console-free derivative
    // are refused. Try a clean generic identity on the same IOS and NAND.
    CHECK(run({-1029, -1029, 0}, true) == 0);
    CHECK((offered == std::vector<std::vector<u8>>{installed, synthetic, generic}));
    CHECK(attempt.find("refused the installed ticket (-1029)") != std::string::npos);
    CHECK(attempt.find("refused the synthetic ticket (-1029)") != std::string::npos);
    CHECK(attempt.find("accepted the generic ticket (0)") != std::string::npos);
    // No key, ticket ID, console binding or title survives into the generic
    // identity; it has only the issuer, signature type and content mask.
    for (std::size_t offset = 0x180; offset < 0x222; ++offset) CHECK(generic[offset] == 0);
    for (std::size_t offset = 0x222; offset < 0x262; ++offset) CHECK(generic[offset] == 0xFF);

    CHECK(run({-1029, -1029, -1017}, true) == -1017);
    CHECK(offered.size() == 3);
    CHECK(attempt.find("refused the generic ticket (-1017)") != std::string::npos);
    CHECK(run({-1029, -1029}, false) == -1029);
    CHECK(offered.size() == 2);  // real-NAND failure behavior is unchanged

    // A missing ticket was already replaced by a synthetic one. Do not
    // offer it twice, and still try the generic one on a copied NAND.
    CHECK(run({0}, true, true) == 0);
    CHECK(offered == std::vector<std::vector<u8>>{synthetic});
    CHECK(run({-1029, 0}, true, true) == 0);
    CHECK((offered == std::vector<std::vector<u8>>{synthetic, generic}));
    CHECK(run({-1029}, false, true) == -1029);
    CHECK(offered.size() == 1);
}

void test_system_launch() {
    std::string error;
    view_reads = 0;
    launches = 0;
    count_reply = -106;
    riftwii::wii::launch_emulated_channel_with_system(kTitle, error);
    CHECK(view_reads == 0 && launches == 0);
    CHECK(error.find("-106") != std::string::npos);

    count_reply = 0;
    view_count = 0;
    riftwii::wii::launch_emulated_channel_with_system(kTitle, error);
    CHECK(view_reads == 0 && launches == 0);
    CHECK(error.find("0 views") != std::string::npos);

    view_count = 1;
    view_reply = -1028;
    riftwii::wii::launch_emulated_channel_with_system(kTitle, error);
    CHECK(view_reads == 1 && launches == 0);
    CHECK(error.find("-1028") != std::string::npos);

    // ES can report any count; the client requests room for just one view.
    // Its exact bytes, from this call rather than a cached real-NAND view,
    // must be passed to LaunchTitle.
    view_reply = 0;
    for (u32 count : {1u, 4u, std::numeric_limits<u32>::max()}) {
        view_count = count;
        riftwii::wii::launch_emulated_channel_with_system(kTitle, error);
        CHECK(error.find("-1029") != std::string::npos);
    }
    CHECK(view_reads == 4 && launches == 3);
    // Any return, even zero, means the title did not take over.
    launch_reply = 0;
    riftwii::wii::launch_emulated_channel_with_system(kTitle, error);
    CHECK(error == "the Wii's own launch returned (0)");
}

}  // namespace

s32 IOS_GetVersion() { return 249; }
s32 IOS_GetRevision() { return 21011; }

s32 ES_Identify(const signed_blob* certificates, u32 cert_size, const signed_blob* metadata, u32 tmd_size,
                const signed_blob* ticket, u32 ticket_size, u32* key) {
    CHECK(cert_size == certs.size());
    CHECK(std::memcmp(certificates, certs.data(), cert_size) == 0);
    CHECK(tmd_size == tmd.size());
    CHECK(std::memcmp(metadata, tmd.data(), tmd_size) == 0);
    CHECK(ticket_size == riftwii::kSignedTicketBytes);
    CHECK(key != nullptr);
    const auto* bytes = reinterpret_cast<const u8*>(ticket);
    offered.emplace_back(bytes, bytes + ticket_size);
    if (offered.size() > replies.size()) {
        CHECK(false);
        return -1;
    }
    return replies[offered.size() - 1];
}

s32 ES_GetNumTicketViews(u64 title, u32* count) {
    CHECK(title == kTitle);
    *count = view_count;
    return count_reply;
}

s32 ES_GetTicketViews(u64 title, tikview* view, u32 count) {
    CHECK(title == kTitle);
    CHECK(count == 1);
    CHECK((reinterpret_cast<std::uintptr_t>(view) & 31) == 0);
    ++view_reads;
    std::memset(view, 0xA5, sizeof(*view));
    return view_reply;
}

s32 ES_LaunchTitle(u64 title, const tikview* view) {
    CHECK(title == kTitle);
    CHECK((reinterpret_cast<std::uintptr_t>(view) & 31) == 0);
    for (u8 byte : view->bytes) CHECK(byte == 0xA5);
    ++launches;
    return launch_reply;
}

namespace riftwii::wii {
void logf(const char*, ...) {}
}  // namespace riftwii::wii

int main() {
    test_identity();
    test_system_launch();
    if (failures == 0) std::cout << "channelidentity: all passed\n";
    return failures == 0 ? 0 : 1;
}
