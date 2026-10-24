// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

// Where an SD card's file system starts and what it is, from the card's
// first sectors, for boot.log. d2x's FAT module mounts the card with its
// own FAT driver, which answered -101 on testers' cards that libfat reads
// well (the "mount -101" lines in their boot.logs, for #128 and WiiWare
// from an emulated NAND): it mounted no file system on the card. The
// suspected causes are a read that failed (another driver on the card) or
// a layout it does not mount; the card's layout in the same log helps
// tell those apart. Facts only: the line says what is on the card, not
// what d2x makes of it.
namespace riftwii {

constexpr std::uint32_t kCardSectorBytes = 512;

struct CardVolumeStart {
    enum class Scheme {
        Mbr,         // a partition table; `partition` is the first used entry
        Gpt,         // a protective MBR entry (type 0xEE)
        NoTable,     // sector 0 is the volume's own boot sector
        Unreadable,  // no boot signature, or no used partition entry
    };
    Scheme scheme = Scheme::Unreadable;
    int partition = 0;  // 1 to 4 for an MBR, else 0
    std::uint8_t type = 0;
    std::uint32_t first_sector = 0;
    std::string problem;  // for Unreadable
};

// From the card's sector 0 (kCardSectorBytes bytes).
CardVolumeStart find_card_volume(const std::uint8_t* sector0);

// "FAT32, 512-byte sectors, 64 sectors per cluster, in MBR partition 1
// (type 0x0C) from sector 8192". `boot_sector` is the volume's first
// sector (kCardSectorBytes bytes), or null when it could not be read.
std::string describe_card_volume(const CardVolumeStart& start, const std::uint8_t* boot_sector);

}  // namespace riftwii
