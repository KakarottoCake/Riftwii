// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

// The NAND (/dev/fs), shared by every part of RiftWii that reads it.
// libogc keeps one handle and does not count its users: ISFS_Deinitialize
// closes it for everyone. The screenshot import closed it under the
// menu's channel scan, and channels dropped off Home for the session
// (ISFS -101, BUG-04). So it is opened and closed through here, counted.
namespace riftwii::wii {

// ISFS_Initialize, counted when it works. What ISFS_Initialize returns.
s32 NandOpen();
// One user done; the last one closes /dev/fs. Nothing when none is open.
void NandClose();
// Closed whatever the count, before an IOS reload or a launch: a handle
// kept open across a reload damages the heap.
void NandCloseAll();

}  // namespace riftwii::wii
