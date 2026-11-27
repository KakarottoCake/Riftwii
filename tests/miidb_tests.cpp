// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Mii databases made up here: the Wii's file size, "RNOD", slots and the
// CRC-16/XMODEM at 0x1F1DE.
#include "riftwii/miidb.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

using namespace riftwii;

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

constexpr std::size_t kWiiSize = 0xBE6C0;

void store_crc(std::vector<std::uint8_t>& db) {
    const std::uint16_t crc = mii_db_crc(db.data(), kMiiCrcAt);
    db[kMiiCrcAt] = static_cast<std::uint8_t>(crc >> 8);
    db[kMiiCrcAt + 1] = static_cast<std::uint8_t>(crc);
}

std::vector<std::uint8_t> empty_db() {
    std::vector<std::uint8_t> db(kWiiSize, 0);
    std::memcpy(db.data(), "RNOD", 4);
    // The Mii Parade's part after the slots, which copies leave alone.
    std::memcpy(db.data() + 0x1CEC, "RNHD", 4);
    db[kWiiSize - 1] = 0x5A;
    store_crc(db);
    return db;
}

void put_mii(std::vector<std::uint8_t>& db, std::size_t slot, const char16_t* name, std::uint32_t id) {
    std::uint8_t* mii = db.data() + kMiiFirst + slot * kMiiSize;
    mii[0] = 0x40;  // flags: something non-zero
    for (std::size_t i = 0; name[i] && i < 10; ++i) {
        mii[2 + i * 2] = static_cast<std::uint8_t>(name[i] >> 8);
        mii[3 + i * 2] = static_cast<std::uint8_t>(name[i]);
    }
    for (int i = 0; i < 4; ++i) mii[0x18 + i] = static_cast<std::uint8_t>(id >> (24 - 8 * i));
    mii[0x30] = static_cast<std::uint8_t>(id);  // a face byte that tells copies apart
    store_crc(db);
}

void test_crc() {
    // CRC-16/XMODEM's check value.
    const char* check = "123456789";
    CHECK(mii_db_crc(reinterpret_cast<const std::uint8_t*>(check), 9) == 0x31C3);
}

void test_valid_and_list() {
    std::vector<std::uint8_t> db = empty_db();
    std::string error;
    CHECK(mii_db_valid(db, error));
    CHECK(mii_db_list(db).empty());
    put_mii(db, 0, u"Mario", 0x85F24534);
    put_mii(db, 7, u"Pokémon", 0x86110904);
    const auto miis = mii_db_list(db);
    CHECK(miis.size() == 2);
    CHECK(miis.size() == 2 && miis[0].slot == 0 && miis[0].name == "Mario");
    CHECK(miis.size() == 2 && miis[1].slot == 7 && miis[1].name == "Pok\xC3\xA9mon");
    db[100] ^= 1;  // damaged
    CHECK(!mii_db_valid(db, error));
    std::vector<std::uint8_t> small(16, 0);
    CHECK(!mii_db_valid(small, error));
    std::vector<std::uint8_t> other = empty_db();
    other[0] = 'X';
    store_crc(other);
    CHECK(!mii_db_valid(other, error));
}

void test_copy_and_remove() {
    std::vector<std::uint8_t> wii = empty_db();
    std::vector<std::uint8_t> emulated = empty_db();
    put_mii(wii, 3, u"Luigi", 0x85F2455C);
    put_mii(emulated, 0, u"Toad", 0x85F2EF9A);
    const std::vector<std::uint8_t> tail(emulated.begin() + kMiiDbMinSize, emulated.end());
    std::size_t placed = 99;
    std::string error;
    // Into the first empty slot, the check made again, the rest untouched.
    CHECK(mii_db_copy(wii, 3, emulated, placed, error));
    CHECK(placed == 1);
    CHECK(mii_db_valid(emulated, error));
    CHECK(std::equal(tail.begin(), tail.end(), emulated.begin() + kMiiDbMinSize));
    CHECK(std::memcmp(emulated.data() + kMiiFirst + kMiiSize, wii.data() + kMiiFirst + 3 * kMiiSize, kMiiSize) == 0);
    // The same Mii again (same ID, changed since): it replaces its copy.
    wii[kMiiFirst + 3 * kMiiSize + 0x31] = 0x77;
    store_crc(wii);
    CHECK(mii_db_copy(wii, 3, emulated, placed, error));
    CHECK(placed == 1);
    CHECK(mii_db_list(emulated).size() == 2);
    CHECK(emulated[kMiiFirst + kMiiSize + 0x31] == 0x77);
    // Nothing to copy from an empty slot.
    CHECK(!mii_db_copy(wii, 4, emulated, placed, error));
    // Removed: the slot is empty, the check right, the others stay.
    CHECK(mii_db_remove(emulated, 0, error));
    CHECK(mii_db_valid(emulated, error));
    const auto left = mii_db_list(emulated);
    CHECK(left.size() == 1 && left[0].name == "Luigi");
    CHECK(!mii_db_remove(emulated, 0, error));
    // A full side refuses a new Mii, but still takes its own Mii's update.
    std::vector<std::uint8_t> full = empty_db();
    for (std::size_t slot = 0; slot < kMiiSlots; ++slot) put_mii(full, slot, u"M", 0x80000000u + static_cast<std::uint32_t>(slot));
    CHECK(!mii_db_copy(wii, 3, full, placed, error));
    put_mii(full, 50, u"Luigi", 0x85F2455C);
    CHECK(mii_db_copy(wii, 3, full, placed, error) && placed == 50);
    // A damaged destination is not written to.
    std::vector<std::uint8_t> damaged = empty_db();
    damaged[10] = 1;
    CHECK(!mii_db_copy(wii, 3, damaged, placed, error));
}

void test_names() {
    std::vector<std::uint8_t> db = empty_db();
    // A control character and a lone surrogate half are left out.
    const char16_t name[] = {u'A', 0x0007, 0xD800, u'B', 0};
    put_mii(db, 0, name, 1);
    const auto miis = mii_db_list(db);
    CHECK(miis.size() == 1 && miis[0].name == "AB");
}

}  // namespace

int main() {
    test_crc();
    test_valid_and_list();
    test_copy_and_remove();
    test_names();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "miidb: all passed\n";
    return 0;
}
