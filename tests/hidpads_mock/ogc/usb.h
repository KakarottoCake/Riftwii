// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

#define USB_CLASS_HID 3

typedef struct _usb_device_entry {
    int32_t device_id;
    uint16_t vid;
    uint16_t pid;
} usb_device_entry;

extern "C" {

int32_t USB_Initialize(void);

}
