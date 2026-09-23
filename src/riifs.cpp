// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riifs.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii::riifs {
namespace {

// Every number on the wire is a big-endian 32-bit word, stats aside.
constexpr std::uint32_t kActionSet = 1;     // an option (argument) for the next command
constexpr std::uint32_t kActionCall = 2;    // run a command; the server answers

constexpr std::uint32_t kOptHandshake = 0x00;
constexpr std::uint32_t kOptFile = 0x01;
constexpr std::uint32_t kOptPath = 0x02;
constexpr std::uint32_t kOptMode = 0x03;
constexpr std::uint32_t kOptLength = 0x04;

constexpr std::uint32_t kCmdHandshake = 0x00;
constexpr std::uint32_t kCmdGoodbye = 0x01;
constexpr std::uint32_t kCmdOpen = 0x10;
constexpr std::uint32_t kCmdRead = 0x11;
constexpr std::uint32_t kCmdClose = 0x16;
constexpr std::uint32_t kCmdStat = 0x17;
constexpr std::uint32_t kCmdOpenDir = 0x21;
constexpr std::uint32_t kCmdCloseDir = 0x22;
constexpr std::uint32_t kCmdNextName = 0x23;
constexpr std::uint32_t kCmdNextStat = 0x24;

constexpr const char* kClientVersion = "1.03";
constexpr std::int32_t kMinServerVersion = 3;
constexpr std::size_t kNameBytes = 1024;  // a listing's name comes in a fixed field
constexpr std::size_t kStatBytes = 24;
constexpr std::size_t kMaxChunk = 0x100000;

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::uint64_t get64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(get32(p)) << 32) | get32(p + 4);
}

}  // namespace

bool Client::set_option(std::uint32_t option, const void* data, std::size_t length) {
    std::uint8_t head[12];
    put32(head, kActionSet);
    put32(head + 4, option);
    put32(head + 8, static_cast<std::uint32_t>(length));
    return transport_.send(head, sizeof(head)) && (length == 0 || transport_.send(data, length));
}

bool Client::set_word(std::uint32_t option, std::uint32_t value) {
    std::uint8_t word[4];
    put32(word, value);
    return set_option(option, word, sizeof(word));
}

bool Client::command(std::uint32_t cmd) {
    std::uint8_t head[8];
    put32(head, kActionCall);
    put32(head + 4, cmd);
    return transport_.send(head, sizeof(head));
}

bool Client::result(std::int32_t& value) {
    std::uint8_t word[4];
    if (!transport_.receive(word, sizeof(word))) return false;
    value = static_cast<std::int32_t>(get32(word));
    return true;
}

bool Client::read_stat(Stat& out) {
    std::uint8_t raw[kStatBytes];
    if (!transport_.receive(raw, sizeof(raw))) return false;
    out.identifier = get64(raw);
    out.size = get64(raw + 8);
    out.device = get32(raw + 16);
    out.mode = get32(raw + 20);
    return true;
}

bool Client::handshake(std::string& error) {
    std::int32_t version = -1;
    if (!set_option(kOptHandshake, kClientVersion, std::strlen(kClientVersion)) || !command(kCmdHandshake) ||
        !result(version)) {
        error = "RiiFS: no answer to the handshake";
        return false;
    }
    if (version < kMinServerVersion) {
        error = "RiiFS: the server does not speak protocol " + std::string(kClientVersion) + " (answered " +
                std::to_string(version) + ")";
        return false;
    }
    return true;
}

bool Client::stat(const std::string& path, Stat& out, bool& missing, std::string& error) {
    missing = false;
    std::int32_t rc = -1;
    Stat st;
    if (!set_option(kOptPath, path.data(), path.size()) || !command(kCmdStat) || !read_stat(st) || !result(rc)) {
        error = "RiiFS: connection lost at stat " + path;
        return false;
    }
    if (rc < 0) {
        missing = true;
        error = "RiiFS: " + path + " does not exist on the server";
        return false;
    }
    out = st;
    return true;
}

bool Client::list(const std::string& path, std::vector<DirEntry>& out, bool& missing, std::string& error) {
    missing = false;
    std::int32_t dir = -1;
    if (!set_option(kOptPath, path.data(), path.size()) || !command(kCmdOpenDir) || !result(dir)) {
        error = "RiiFS: connection lost at listing " + path;
        return false;
    }
    if (dir < 0) {
        missing = true;
        error = "RiiFS: folder " + path + " does not exist on the server";
        return false;
    }
    std::vector<DirEntry> entries;
    std::vector<char> name(kNameBytes + 1, 0);
    for (;;) {
        std::int32_t length = -1;
        if (!set_word(kOptFile, static_cast<std::uint32_t>(dir)) || !command(kCmdNextName) ||
            !transport_.receive(name.data(), kNameBytes) || !result(length)) {
            error = "RiiFS: connection lost while listing " + path;
            return false;
        }
        if (length < 0) break;  // no more entries
        DirEntry e;
        std::int32_t rc = -1;
        if (!command(kCmdNextStat) || !read_stat(e.stat) || !result(rc)) {
            error = "RiiFS: connection lost while listing " + path;
            return false;
        }
        name[kNameBytes] = 0;
        const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(length), std::strlen(name.data()));
        e.name.assign(name.data(), n);
        if (e.name.empty() || e.name == "." || e.name == "..") continue;
        entries.push_back(std::move(e));
    }
    std::int32_t ignored = 0;
    if (!set_word(kOptFile, static_cast<std::uint32_t>(dir)) || !command(kCmdCloseDir) || !result(ignored)) {
        error = "RiiFS: connection lost while listing " + path;
        return false;
    }
    out = std::move(entries);
    return true;
}

bool Client::open_read(const std::string& path, std::int32_t& fd, std::string& error) {
    fd = -1;
    if (!set_option(kOptPath, path.data(), path.size()) || !set_word(kOptMode, 0) || !command(kCmdOpen) ||
        !result(fd)) {
        error = "RiiFS: connection lost at opening " + path;
        return false;
    }
    if (fd < 0) {
        error = "RiiFS: the server cannot open " + path;
        return false;
    }
    return true;
}

void Client::close_file(std::int32_t fd) {
    std::int32_t ignored = 0;
    if (set_word(kOptFile, static_cast<std::uint32_t>(fd)) && command(kCmdClose)) result(ignored);
}

bool Client::fetch(const std::string& path, std::uint64_t expected,
                   const std::function<bool(const std::uint8_t*, std::size_t)>& sink, std::string& error,
                   std::size_t chunk) {
    if (chunk == 0 || chunk > kMaxChunk) chunk = kMaxChunk;
    std::int32_t fd = -1;
    if (!open_read(path, fd, error)) return false;
    std::vector<std::uint8_t> buffer(chunk);
    std::uint64_t done = 0;
    bool ok = true;
    for (;;) {
        // Ask for one byte past the expected end at the last piece, so a
        // file that grew since the stat is noticed.
        const std::uint64_t left = expected - done;
        const std::size_t want = left >= chunk ? chunk : static_cast<std::size_t>(left) + 1;
        std::int32_t got = -1;
        // The server always sends `want` bytes (padded past the end), then
        // how many of them are the file's.
        if (!set_word(kOptFile, static_cast<std::uint32_t>(fd)) ||
            !set_word(kOptLength, static_cast<std::uint32_t>(want)) || !command(kCmdRead) ||
            !transport_.receive(buffer.data(), want) || !result(got)) {
            error = "RiiFS: connection lost while reading " + path;
            return false;  // the stream is out of step; no close
        }
        if (got < 0 || static_cast<std::size_t>(got) > want) {
            error = "RiiFS: the server failed reading " + path;
            ok = false;
            break;
        }
        if (static_cast<std::uint64_t>(got) > left) {
            error = "RiiFS: " + path + " is larger than the server said; it changed during the copy";
            ok = false;
            break;
        }
        if (got > 0 && !sink(buffer.data(), static_cast<std::size_t>(got))) {
            error = "RiiFS: cannot store " + path;
            ok = false;
            break;
        }
        done += static_cast<std::uint64_t>(got);
        if (static_cast<std::size_t>(got) < want) break;  // end of file
    }
    if (ok && done != expected) {
        error = "RiiFS: " + path + " is shorter than the server said; it changed during the copy";
        ok = false;
    }
    close_file(fd);
    return ok;
}

void Client::goodbye() {
    std::int32_t ignored = 0;
    if (command(kCmdGoodbye)) result(ignored);
}

std::string join_path(const std::string& directory, const std::string& name) {
    std::string out = directory;
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    if (out.empty() || out.back() != '/') out += '/';
    out += name;
    return out;
}

}  // namespace riftwii::riifs
