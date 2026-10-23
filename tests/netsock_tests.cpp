// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netsock.hpp"

#include <network.h>
#include <poll.h>

#include <cstring>
#include <iostream>

u64 netsock_test_time = 0;

namespace {
enum class Reply { Quiet, Valid, InvalidPort, Short };
Reply reply = Reply::Quiet;
unsigned received = 0;
unsigned sent = 0;
unsigned closed = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}
}  // namespace

s32 net_close(s32) {
    ++closed;
    return 0;
}

s32 net_poll(pollsd* sockets, unsigned, int timeout_ms) {
    // Stop the hostile stream eventually so the old code fails assertions
    // rather than hanging the test runner. A real sender need never stop.
    if (reply != Reply::Quiet && received < 10000) {
        ++netsock_test_time;
        sockets[0].revents = POLLIN;
        return 1;
    }
    netsock_test_time += timeout_ms;
    sockets[0].revents = 0;
    return 0;
}

s32 net_sendto(s32, const void*, s32 size, int, const sockaddr*, socklen_t) {
    ++sent;
    return size;
}

s32 net_recvfrom(s32, void* data, s32, int, sockaddr* address, socklen_t*) {
    ++received;
    auto* from = reinterpret_cast<sockaddr_in*>(address);
    from->sin_addr.s_addr = htonl(0xC0A80114u);
    const unsigned char port[4] = {0, 0, 4, 0x71};  // 1137
    std::memcpy(data, port, sizeof(port));
    if (reply == Reply::InvalidPort) std::memset(data, 0, sizeof(port));
    return reply == Reply::Short ? 3 : 4;
}

namespace riftwii::wii {
void logf(const char*, ...) {}
}  // namespace riftwii::wii

int main() {
    for (Reply scenario : {Reply::Quiet, Reply::Valid, Reply::InvalidPort, Reply::Short}) {
        reply = scenario;
        netsock_test_time = 0;
        received = sent = closed = 0;
        const auto servers = riftwii::wii::DiscoverServers(1137, 1500);
        // poll_for may overrun each round by one 100 ms polling slice.
        expect(netsock_test_time <= 2000, "Discovery exceeded its timeout under a stream of replies");
        expect(closed == 1, "Discovery did not close its socket");
        if (scenario == Reply::Valid) {
            expect(servers.size() == 1, "Duplicate replies were not reduced to one server");
            if (!servers.empty()) expect(servers[0].port == 1137, "Discovery lost the advertised port");
            expect(sent == 1, "Discovery retried after finding a server");
        } else {
            expect(servers.empty(), "An invalid reply became a server");
            expect(sent == 3, "Discovery did not retry unanswered pings");
        }
    }
    return failures ? 1 : 0;
}
