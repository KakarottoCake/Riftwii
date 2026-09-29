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
// instead. The stub goes where the caller reserved 20 bytes the game and
// its mods never touch: not the SDK's unused debugger table (0x30 past
// "Metrowerks T", where PatchReturnTo puts it), which mod loaders fill
// with their own code (Pulsar's at load, CTGP's as it runs).

struct CodeSpan {
    std::uint8_t* bytes = nullptr;  // the loaded section
    std::size_t size = 0;
    std::uint32_t address = 0;      // where the game sees it
};

struct ReturnToReport {
    bool patched = false;
    bool old_sdk = false;
    unsigned sites = 0;             // places found (3 to patch)
    bool stub_place = false;        // a place for the stub was given
    std::string describe() const;
};

// Patches the loaded game so its "Wii Menu" launches 00010001-`title_low`,
// with the stub at `stub` (20 bytes the game sees at `stub_address`,
// within a branch's reach of the game's code). Nothing is written unless
// all three places are found.
ReturnToReport patch_return_to(const std::vector<CodeSpan>& spans, std::uint32_t title_low, std::uint8_t* stub,
                               std::uint32_t stub_address);

}  // namespace riftwii
