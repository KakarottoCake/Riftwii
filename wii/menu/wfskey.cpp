// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "otpkey.hpp"

namespace riftwii::wii {

// Where IOS59 keeps the key, and how to read it from this Wii, is the key
// dump's to say: it lands as a change of its own. Until then there is no
// key, a WFS drive reads as an encrypted drive RiftWii leaves alone, and
// Dragon Quest X still starts (its own boot program finds the drive); only
// the check that its install is on the drive is skipped.
bool ConsoleWfsKey(std::uint8_t out[16]) {
    (void)out;
    return false;
}

}  // namespace riftwii::wii
