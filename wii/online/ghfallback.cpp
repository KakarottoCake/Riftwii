// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ghfallback.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <vector>

#include "httpmem.hpp"
#include "log.hpp"
#include "skin.hpp"
#include "textfile.hpp"
#include "tls.hpp"
#include "zipinflate.hpp"
#include "riftwii/ghcheats.hpp"

namespace riftwii::wii {
namespace {

// What the answer's buffer holds over the body: the HTTP headers.
constexpr std::size_t kHeaderRoom = 8192;
// The index is a few KB (449 games); anything near this is not ours.
constexpr std::size_t kIndexCap = 256u << 10;

// The site was asked, or was about to be, this session. A failure is not
// tried again until RiftWii is restarted: the archive asks not to be hit.
bool g_asked = false;

// One block of MEM2 for everything: TLS state, the download or the zip read
// from the card, zlib's state and the unpack chunk. Kept between uses when
// it cannot be given back (the arena only moves one way: skin::Mem2Release).
struct Block {
    std::uint8_t* data = nullptr;
    std::size_t bytes = 0;
};
Block g_block;

struct Parts {
    std::uint8_t* tls = nullptr;
    std::uint8_t* raw = nullptr;
    std::size_t raw_bytes = 0;
};

std::size_t TlsBytes() { return (TlsStream::memory_needed() + 31) & ~std::size_t(31); }

// Lays the block out for a `raw_bytes` buffer, taking more MEM2 only when
// what is held is too small.
bool Arrange(std::size_t raw_bytes, Parts& parts) {
    // zlib's block follows the buffer and wants 8-byte alignment.
    raw_bytes = (raw_bytes + 31) & ~std::size_t(31);
    const std::size_t need = TlsBytes() + raw_bytes + kZipZlibBytes + kZipChunkBytes;
    if (!g_block.data || g_block.bytes < need) {
        if (g_block.data && skin::Mem2Release(g_block.data, g_block.bytes)) g_block = Block{};
        g_block.data = skin::Mem2Alloc(need);
        g_block.bytes = g_block.data ? need : 0;
        if (!g_block.data) return false;
    }
    std::uint8_t* at = g_block.data;
    parts.tls = at;
    at += TlsBytes();
    parts.raw = at;
    // Whatever the block holds beyond the other parts is the buffer.
    parts.raw_bytes = g_block.bytes - TlsBytes() - kZipZlibBytes - kZipChunkBytes;
    at += parts.raw_bytes;
    ZipInflateUse(at, at + kZipZlibBytes);
    return true;
}

void GiveBack() {
    if (g_block.data && skin::Mem2Release(g_block.data, g_block.bytes)) g_block = Block{};
}

std::string IndexPath() { return std::string(kGameHackingDir) + "/index.txt"; }
std::string ZipPath(const std::string& name) { return std::string(kGameHackingDir) + "/" + name; }

// "RiftWii/<version>": the site asks to be told who is asking.
const char* UserAgent() {
    static const std::string agent = std::string("RiftWii/") + RIFTWII_VERSION;
    return agent.c_str();
}

bool WriteFileAtomic(const std::string& path, const std::uint8_t* data, std::size_t size, std::string& error) {
    const std::string temp = path + ".part";
    FILE* file = std::fopen(temp.c_str(), "wb");
    if (!file) {
        error = "cannot write " + temp;
        return false;
    }
    const bool written = std::fwrite(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::remove(temp.c_str());
        error = "cannot write " + temp + " (card full?)";
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        error = "cannot rename " + temp;
        return false;
    }
    return true;
}

bool WriteIndex(const GameHackingIndex& index, std::string& error) {
    const std::string text = gamehacking_index_text(index);
    return WriteFileAtomic(IndexPath(), reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), error);
}

bool LoadIndex(GameHackingIndex& index) {
    std::string text, why;
    if (!ReadTextFile(IndexPath(), text, kIndexCap)) return false;
    if (!gamehacking_parse_index(text, index, why)) {
        logf("Cheats: the GameHacking.org index on the card is not usable (%s); it will be fetched again\n", why.c_str());
        index = GameHackingIndex{};
        return false;
    }
    return true;
}

// One GET into the block's buffer. The body is at `get.buffer + get.body_offset`.
bool Fetch(const std::string& url, const Parts& parts, MemoryGet& get, std::string& error) {
    get = MemoryGet{};
    get.buffer = parts.raw;
    get.capacity = parts.raw_bytes;
    get.tls_block = parts.tls;
    get.tls_bytes = TlsStream::memory_needed();
    get.user_agent = UserAgent();
    return HttpGetInMemory(url, get, error, 20000);
}

// The two requests: the listing, then (unless the cached zip is already the
// newest) the zip. On success `index` is the cache's new state, on the card.
bool Refresh(GameHackingIndex& index, bool have_cache, std::string& error) {
    const GameHackingLimits limits;
    Parts parts;
    if (!Arrange(limits.zip_bytes + kHeaderRoom, parts)) {
        error = "no memory for the GameHacking.org download";
        return false;
    }
    MemoryGet get;
    logf("Cheats: asking %s for its newest Wii zip (one request for the listing, one for the zip)\n",
         kGameHackingTextUrl);
    if (!Fetch(kGameHackingTextUrl, parts, get, error)) return false;
    if (get.body_length > limits.listing_bytes) {
        error = "GameHacking.org's listing is bigger than RiftWii takes (128 KB)";
        return false;
    }
    std::string name;
    const std::string listing(reinterpret_cast<const char*>(get.buffer + get.body_offset), get.body_length);
    if (!gamehacking_newest_wii_zip(listing, name, error)) return false;
    logf("Cheats: GameHacking.org's newest Wii zip is %s\n", name.c_str());

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    struct stat st;
    if (have_cache && index.zip == name && stat(ZipPath(name).c_str(), &st) == 0) {
        // Nothing newer: keep the copy and count 30 days from now.
        index.fetched = now;
        logf("Cheats: the copy on the card (%s) is still the newest; kept\n", name.c_str());
        return WriteIndex(index, error);
    }

    if (!Fetch(std::string(kGameHackingTextUrl) + name, parts, get, error)) return false;
    if (get.body_length > limits.zip_bytes) {
        error = "the GameHacking.org zip is bigger than RiftWii takes (4 MB)";
        return false;
    }
    const std::uint8_t* zip = get.buffer + get.body_offset;
    GameHackingIndex fresh;
    if (!gamehacking_build_index(zip, get.body_length, limits, &ZipInflate, fresh, error)) return false;
    if (fresh.entry.empty()) {
        error = "the GameHacking.org zip has no cheat files RiftWii can read";
        return false;
    }
    fresh.zip = name;
    fresh.fetched = now;

    mkdir("sd:/riftwii", 0777);
    mkdir("sd:/riftwii/cheats", 0777);
    mkdir(kGameHackingDir, 0777);
    if (!WriteFileAtomic(ZipPath(name), zip, get.body_length, error)) return false;
    // The index follows the zip, so one that names a zip is never left without it.
    if (!WriteIndex(fresh, error)) return false;
    if (have_cache && index.zip != name) std::remove(ZipPath(index.zip).c_str());
    logf("Cheats: kept %s from %s (%u bytes): %u games, %u entries left out (too big, no game ID, or a second file for a game)\n",
         name.c_str(), kGameHackingName, static_cast<unsigned>(get.body_length), static_cast<unsigned>(fresh.entry.size()),
         static_cast<unsigned>(fresh.left_out));
    index = fresh;
    return true;
}

// Reads the cached zip into the block's buffer.
bool ReadZip(const GameHackingIndex& index, Parts& parts, std::size_t& size, std::string& error) {
    const GameHackingLimits limits;
    const std::string path = ZipPath(index.zip);
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        error = "cannot read " + path;
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0 || static_cast<std::size_t>(length) > limits.zip_bytes) {
        std::fclose(file);
        error = path + " is not a size RiftWii takes";
        return false;
    }
    size = static_cast<std::size_t>(length);
    if (!Arrange(size, parts)) {
        std::fclose(file);
        error = "no memory to read " + path;
        return false;
    }
    const bool read = std::fread(parts.raw, 1, size, file) == size;
    std::fclose(file);
    if (!read) {
        error = "cannot read " + path;
        return false;
    }
    return true;
}

}  // namespace

GameHackingResult GameHackingCheats(const std::string& game_id, std::string& text, std::string& error,
                                    std::string* zip) {
    text.clear();
    GameHackingIndex index;
    const bool have_cache = LoadIndex(index);
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    struct stat st;
    const bool zip_there = have_cache && stat(ZipPath(index.zip).c_str(), &st) == 0;
    const bool due = !zip_there || gamehacking_stale(index.fetched, now);

    if (due && !g_asked) {
        g_asked = true;
        if (zip_there) {
            logf("Cheats: the GameHacking.org copy on the card (%s) is over 30 days old; looking for a newer one\n",
                 index.zip.c_str());
        }
        std::string why;
        if (!Refresh(index, zip_there, why)) {
            logf("Cheats: GameHacking.org could not be fetched: %s\n", why.c_str());
            GiveBack();
            // An old copy still answers; with none there is nothing to say.
            if (!zip_there) {
                error = why;
                return GameHackingResult::Unavailable;
            }
        }
    } else if (!zip_there) {
        error = "GameHacking.org was already tried this session";
        return GameHackingResult::Unavailable;
    }

    if (index.entry.find(game_id) == index.entry.end()) {
        GiveBack();
        return GameHackingResult::NoCheats;
    }
    Parts parts;
    std::size_t zip_size = 0;
    if (!ReadZip(index, parts, zip_size, error)) {
        GiveBack();
        return GameHackingResult::Unavailable;
    }
    const bool read = gamehacking_read_cheats(parts.raw, zip_size, index, game_id, GameHackingLimits(), &ZipInflate,
                                              text, error);
    GiveBack();
    if (!read) return GameHackingResult::Unavailable;
    if (zip) *zip = index.zip;
    logf("Cheats: %s for %s read from %s on the card (no request made)\n", kGameHackingName, game_id.c_str(),
         index.zip.c_str());
    return GameHackingResult::Found;
}

}  // namespace riftwii::wii
