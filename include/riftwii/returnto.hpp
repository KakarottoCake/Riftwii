// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: giantpune
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// A game's HOME Menu "Wii Menu" button ends in the SDK's __OSLaunchMenu,
// which asks ES for the System Menu's ticket views and launches it: three
// places load its title, 00000001-00000002, as "li r4,2; li r3,1; li r5,0"
// (older SDKs: "li r6,2; li r5,1; li r7,0"). Each place becomes a call to
// a five-instruction stub that loads another title, 00010001-`title_low`,
// instead; the stub goes 0x30 bytes past the SDK's "Metrowerks T" compiler
// string, where PatchReturnTo puts it.

struct CodeSpan {
    std::uint8_t* bytes = nullptr;  // the loaded section
    std::size_t size = 0;
    std::uint32_t address = 0;      // where the game sees it
};

struct ReturnToReport {
    bool patched = false;
    bool old_sdk = false;
    unsigned sites = 0;             // places found (3 to patch)
    bool stub_place = false;        // the compiler string was found
    std::string describe() const;
};

// The string starts the SDK's debugger vector table, unused in a retail
// game, and mod loaders put their own code there too (Pulsar's loader
// takes the first 0xADC bytes, string and all). So the table's first
// 4 KiB are copied before a pack's memory patches run, and the stub
// goes where they left the copy unchanged.
struct ReturnToArea {
    std::uint32_t address = 0;          // the string's; 0: not found
    std::vector<std::uint8_t> bytes;    // the table as loaded
};

ReturnToArea find_return_area(const std::vector<CodeSpan>& spans);

// Patches the loaded game so its "Wii Menu" launches 00010001-`title_low`.
// Nothing is written unless all three places and the stub's place are
// found. The stub goes 0x30 past the string when nothing in `area`
// changed since it was copied, else as high in it as 20 unchanged bytes
// fit, away from the loader's code and whatever that code keeps after it.
ReturnToReport patch_return_to(const std::vector<CodeSpan>& spans, std::uint32_t title_low, const ReturnToArea& area);
ReturnToReport patch_return_to(const std::vector<CodeSpan>& spans, std::uint32_t title_low);

}  // namespace riftwii
