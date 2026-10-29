// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netsock.hpp"

#include <network.h>
#include <ogc/lwp.h>
#include <poll.h>

#include <cerrno>
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
s32 init_result = 0;
unsigned init_calls = 0;
std::string cleanup_calls;
unsigned launch_log_calls = 0;
bool cancel_during_init = false;
lwp_t current_thread = 1;
void* (*pending_job)(void*) = nullptr;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}

void start_network() {
    std::string error;
    expect(!riftwii::wii::NetStart(error, true), "Cancelled network start succeeded");
}

void test_cancelled_retry() {
    using namespace riftwii::wii;
    // Both cancellation between retries and cancellation during the final
    // failing net_init must leave the next caller free to try again.
    for (s32 cancelled_result : {-EAGAIN, -EIO}) {
        NetStop();
        std::string error;
        init_result = -EIO;
        expect(!NetStart(error, true), "Initial network failure succeeded");
        expect(NetFailed(), "Initial network failure was not recorded");

        init_result = cancelled_result;
        cancel_during_init = true;
        expect(NetRunInBackground(start_network, true, false), "Could not start mock network job");
        current_thread = 2;
        pending_job(nullptr);
        current_thread = 1;
        NetWaitForBackground();
        expect(!NetFailed(), "Cancelled retry left the previous network failure cached");

        init_result = 0;
        init_calls = 0;
        expect(NetStart(error), "Network start after a cancelled retry was held back");
        expect(init_calls == 1, "Network start after cancellation did not try net_init");
    }
    NetStop();
}

void test_launch_cleanup() {
    using namespace riftwii::wii;
    // IOS's socket service can survive even a start that failed to get
    // an address. Both outcomes must be cleaned before keeping this IOS.
    for (s32 result : {0, -EIO}) {
        NetStop();
        init_result = result;
        std::string error;
        expect(NetStart(error, true) == (result == 0), "Unexpected network start result");
        cleanup_calls.clear();
        launch_log_calls = 0;

        NetStopForLaunch();
        expect(cleanup_calls == "handle;service;", "Launch did not close the handle and then clean IOS sockets");
        expect(launch_log_calls == 1, "Launch did not log that the network was closed");
        NetStop();
        expect(cleanup_calls == "handle;service;", "Repeated stop cleaned IOS sockets twice");

        // A failed launch may return to the menu and start networking again.
        init_result = 0;
        init_calls = 0;
        expect(NetStart(error, true), "Network could not start again after launch cleanup");
        expect(init_calls == 1, "Launch cleanup left the network marked as running");
        NetStop();
    }
}
}  // namespace

s32 net_init() {
    ++init_calls;
    if (cancel_during_init) {
        cancel_during_init = false;
        riftwii::wii::NetCancelBackground();
    }
    return init_result;
}

void net_deinit() { cleanup_calls += "handle;"; }

void net_wc24cleanup() { cleanup_calls += "service;"; }

lwp_t LWP_GetSelf() { return current_thread; }

int LWP_CreateThread(lwp_t* thread, void* (*job)(void*), void*, void*, std::size_t, int) {
    *thread = 2;
    pending_job = job;
    return 0;
}

int LWP_JoinThread(lwp_t, void**) {
    pending_job = nullptr;
    return 0;
}

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
void logf(const char* format, ...) {
    if (std::strcmp(format, "Network: closed before the launch\n") == 0) ++launch_log_calls;
}
}  // namespace riftwii::wii

int main() {
    test_cancelled_retry();
    test_launch_cleanup();
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
