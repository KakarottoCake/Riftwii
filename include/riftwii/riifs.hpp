// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A client for RiiFS, the file-sharing protocol Riivolution's companion
// servers speak on a PC (TCP, port 1137 by default; docs/RIIFS.md has the
// protocol as this project understands it). RiftWii uses it read-only, to
// copy the files a pack needs from the PC into a cache on the SD card
// before a launch (see riifs_sync). The byte transport is abstract so the
// host tests drive the client against an in-memory server.
namespace riftwii::riifs {

constexpr std::uint16_t kDefaultPort = 1137;

// A connected byte stream: both calls move exactly `length` bytes or fail
// (a timeout or a closed connection).
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool send(const void* data, std::size_t length) = 0;
    virtual bool receive(void* data, std::size_t length) = 0;
};

struct Stat {
    std::uint64_t identifier = 0;
    std::uint64_t size = 0;
    std::uint32_t device = 0;
    std::uint32_t mode = 0;
    bool is_directory() const { return (mode & 0x4000u) != 0; }
};

struct DirEntry {
    std::string name;
    Stat stat;
};

class Client {
public:
    explicit Client(Transport& transport) : transport_(transport) {}

    // Agrees on the protocol version; must come first. Fails when the
    // server does not speak a version this client knows.
    bool handshake(std::string& error);
    // `missing` is set (and the call fails) when the path does not exist.
    bool stat(const std::string& path, Stat& out, bool& missing, std::string& error);
    // The entries of a directory, without "." and "..".
    bool list(const std::string& path, std::vector<DirEntry>& out, bool& missing, std::string& error);
    // Reads a whole file into `sink`, in pieces of at most `chunk` bytes.
    // The server's size is `expected` (from stat); a shorter or longer file
    // fails the call.
    bool fetch(const std::string& path, std::uint64_t expected,
               const std::function<bool(const std::uint8_t*, std::size_t)>& sink, std::string& error,
               std::size_t chunk = 0x10000);
    // Tells the server the session is over. Best effort.
    void goodbye();

private:
    bool set_option(std::uint32_t option, const void* data, std::size_t length);
    bool set_word(std::uint32_t option, std::uint32_t value);
    bool command(std::uint32_t command);
    bool result(std::int32_t& value);
    bool read_stat(Stat& out);
    bool open_read(const std::string& path, std::int32_t& fd, std::string& error);
    void close_file(std::int32_t fd);

    Transport& transport_;
};

// Server paths are absolute ("/riivolution/mod.xml"); names from a listing
// are joined onto them. Any "\\" is a separator too (Windows servers).
std::string join_path(const std::string& directory, const std::string& name);

}  // namespace riftwii::riifs
