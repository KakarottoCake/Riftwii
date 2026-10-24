// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/returnstub.h"

// RiftWii's return stub for homebrew apps (riftwii/returnstub.h has the
// layout, runtime/stub the stub): the loader's half that needs no Wii.
// wii/launch/channel.cpp reads the titles' ticket views and copies the
// filled image to 0x80001800 before it starts an app.
namespace riftwii {

struct ReturnStubTitle {
    std::uint64_t title = 0;
    std::array<std::uint8_t, RTSTUB_VIEW_BYTES> view{};  // its first ticket view, as ES gave it
};

// The stub image (as built, `size` bytes) with `titles` filled in, tried
// in that order: ready to copy to RTSTUB_BASE. Empty, with `error` saying
// why, when the image is not the stub riftwii/returnstub.h describes, or
// when there is no title or more than RTSTUB_MAX_TITLES.
std::vector<std::uint8_t> fill_return_stub(const std::uint8_t* image, std::size_t size,
                                           const std::vector<ReturnStubTitle>& titles, std::string& error);

// What sits at RTSTUB_BASE, from its first RTSTUB_OFFSET_VERSION bytes:
// nothing libogc's exit() would use, the Homebrew Channel's stub
// ("STUBHAXX"), or RiftWii's own ("STUBHAXX" then "RWRS").
enum class ReturnStubKind { None, HomebrewChannel, RiftWii };
ReturnStubKind return_stub_kind(const std::uint8_t* first_bytes);

// Whether the DOL leaves the stub alone: none of its sections, nor its
// BSS, between RTSTUB_BASE and RTSTUB_LIMIT. A DOL that cannot be read
// counts as not clear.
bool dol_clear_of_return_stub(const std::uint8_t* dol, std::size_t size);

}  // namespace riftwii
