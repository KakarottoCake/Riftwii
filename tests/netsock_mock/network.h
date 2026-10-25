// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

using s32 = std::int32_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using socklen_t = unsigned;

constexpr int AF_INET = 2;
constexpr int SOCK_STREAM = 1;
constexpr int SOCK_DGRAM = 2;
constexpr int IPPROTO_IP = 0;
constexpr int IPPROTO_TCP = 6;
constexpr int SOL_SOCKET = 1;
constexpr int SO_BROADCAST = 6;
constexpr int TCP_NODELAY = 1;

struct sockaddr {};
struct in_addr { u32 s_addr; };
struct sockaddr_in {
    unsigned char sin_len;
    unsigned char sin_family;
    std::uint16_t sin_port;
    in_addr sin_addr;
};
struct hostent { char** h_addr_list; };
struct pollsd { s32 socket; u32 events; u32 revents; };

inline std::uint16_t htons(std::uint16_t value) { return value; }
inline u32 htonl(u32 value) { return value; }
inline u32 ntohl(u32 value) { return value; }
s32 net_init();
inline void net_deinit() {}
inline u32 net_gethostip() { return 0; }
inline hostent* net_gethostbyname(const char*) { return nullptr; }
inline s32 net_socket(int, int, int) { return 1; }
inline s32 net_setsockopt(s32, int, int, const void*, std::size_t) { return 0; }
inline s32 net_fcntl(s32, int, u32) { return 0; }
inline s32 net_connect(s32, const sockaddr*, socklen_t) { return 0; }
inline s32 net_send(s32, const void*, s32 size, int) { return size; }
inline s32 net_recv(s32, void*, s32, int) { return 0; }

s32 net_close(s32 socket);
s32 net_poll(pollsd* sockets, unsigned count, int timeout_ms);
s32 net_sendto(s32, const void*, s32, int, const sockaddr*, socklen_t);
s32 net_recvfrom(s32, void*, s32, int, sockaddr*, socklen_t*);
