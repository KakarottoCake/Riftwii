// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ogc/disc_io.h>

// libfat's mount and unmount, as the loader calls them; a test defines them.
void fatUnmount(const char* name);
bool fatMountSimple(const char* name, const DISC_INTERFACE* interface);
