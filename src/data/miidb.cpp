// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/miidb.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {
namespace {

constexpr std::size_t kNameAt = 0x02;
constexpr std::size_t kNameChars = 10;
constexpr std::size_t kIdAt = 0x18;

const std::uint8_t* slot_bytes(const std::vector<std::uint8_t>& db, std::size_t slot) {
    return db.data() + kMiiFirst + slot * kMiiSize;
}

bool slot_empty(const std::vector<std::uint8_t>& db, std::size_t slot) {
    const std::uint8_t* mii = slot_bytes(db, slot);
    return std::all_of(mii, mii + kMiiSize, [](std::uint8_t byte) { return byte == 0; });
}

std::array<std::uint8_t, 4> slot_id(const std::vector<std::uint8_t>& db, std::size_t slot) {
    std::array<std::uint8_t, 4> id{};
    std::memcpy(id.data(), slot_bytes(db, slot) + kIdAt, id.size());
    return id;
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// The name: up to the first zero, a surrogate pair joined, a lone half or a
// control character left out (the name goes on screen and into the log).
std::string slot_name(const std::vector<std::uint8_t>& db, std::size_t slot) {
    const std::uint8_t* at = slot_bytes(db, slot) + kNameAt;
    std::string name;
    for (std::size_t i = 0; i < kNameChars; ++i) {
        std::uint32_t unit = static_cast<std::uint32_t>(at[i * 2] << 8 | at[i * 2 + 1]);
        if (unit == 0) break;
        if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < kNameChars) {
            const std::uint32_t low = static_cast<std::uint32_t>(at[i * 2 + 2] << 8 | at[i * 2 + 3]);
            if (low >= 0xDC00 && low < 0xE000) {
                append_utf8(name, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                ++i;
            }
            continue;
        }
        if (unit < 0x20 || (unit >= 0x7F && unit < 0xA0) || (unit >= 0xD800 && unit < 0xE000)) continue;
        append_utf8(name, unit);
    }
    return name;
}

void store_crc(std::vector<std::uint8_t>& db) {
    const std::uint16_t crc = mii_db_crc(db.data(), kMiiCrcAt);
    db[kMiiCrcAt] = static_cast<std::uint8_t>(crc >> 8);
    db[kMiiCrcAt + 1] = static_cast<std::uint8_t>(crc);
}

}  // namespace

std::uint16_t mii_db_crc(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ (data[i] << 8));
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

bool mii_db_valid(const std::vector<std::uint8_t>& db, std::string& error) {
    if (db.size() < kMiiDbMinSize || db.size() > kMiiDbMaxSize) {
        error = "the Mii database is " + std::to_string(db.size()) + " bytes, not a size the Wii writes";
        return false;
    }
    if (std::memcmp(db.data(), "RNOD", 4) != 0) {
        error = "the file does not start as a Mii database does";
        return false;
    }
    const std::uint16_t stored = static_cast<std::uint16_t>(db[kMiiCrcAt] << 8 | db[kMiiCrcAt + 1]);
    if (stored != mii_db_crc(db.data(), kMiiCrcAt)) {
        error = "the Mii database's check does not match (the file is damaged)";
        return false;
    }
    return true;
}

std::vector<MiiEntry> mii_db_list(const std::vector<std::uint8_t>& db) {
    std::vector<MiiEntry> miis;
    if (db.size() < kMiiDbMinSize) return miis;
    for (std::size_t slot = 0; slot < kMiiSlots; ++slot) {
        if (slot_empty(db, slot)) continue;
        MiiEntry entry;
        entry.slot = slot;
        entry.name = slot_name(db, slot);
        entry.id = slot_id(db, slot);
        miis.push_back(entry);
    }
    return miis;
}

int mii_db_target_slot(const std::vector<std::uint8_t>& from, std::size_t slot, const std::vector<std::uint8_t>& to) {
    if (from.size() < kMiiDbMinSize || to.size() < kMiiDbMinSize || slot >= kMiiSlots) return -1;
    const std::array<std::uint8_t, 4> id = slot_id(from, slot);
    for (std::size_t other = 0; other < kMiiSlots; ++other)
        if (!slot_empty(to, other) && slot_id(to, other) == id) return static_cast<int>(other);
    for (std::size_t other = 0; other < kMiiSlots; ++other)
        if (slot_empty(to, other)) return static_cast<int>(other);
    return -1;
}

bool mii_db_copy(const std::vector<std::uint8_t>& from, std::size_t slot, std::vector<std::uint8_t>& to,
                 std::size_t& placed, std::string& error) {
    if (!mii_db_valid(from, error) || !mii_db_valid(to, error)) return false;
    if (slot >= kMiiSlots || slot_empty(from, slot)) {
        error = "there is no Mii in that place";
        return false;
    }
    const int target = mii_db_target_slot(from, slot, to);
    if (target < 0) {
        error = "the other side already has 100 Miis";
        return false;
    }
    placed = static_cast<std::size_t>(target);
    std::memcpy(to.data() + kMiiFirst + placed * kMiiSize, slot_bytes(from, slot), kMiiSize);
    store_crc(to);
    return true;
}

bool mii_db_remove(std::vector<std::uint8_t>& db, std::size_t slot, std::string& error) {
    if (!mii_db_valid(db, error)) return false;
    if (slot >= kMiiSlots || slot_empty(db, slot)) {
        error = "there is no Mii in that place";
        return false;
    }
    std::memset(db.data() + kMiiFirst + slot * kMiiSize, 0, kMiiSize);
    store_crc(db);
    return true;
}

}  // namespace riftwii
