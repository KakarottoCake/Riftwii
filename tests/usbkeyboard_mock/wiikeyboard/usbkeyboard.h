// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gctypes.h>

// The parts of libogc's libwiikeyboard the keyboard driver uses, for
// tests/usbkeyboard_tests.cpp, which decides when a scan comes back.
typedef enum { USBKEYBOARD_PRESSED = 0, USBKEYBOARD_RELEASED, USBKEYBOARD_DISCONNECTED } USBKeyboard_eventType;
typedef struct {
    USBKeyboard_eventType type;
    u8 keyCode;
} USBKeyboard_event;
typedef void (*eventcallback)(USBKeyboard_event event);
#define USBKEYBOARD_LEDCAPS 1
s32 USBKeyboard_Initialize(void);
s32 USBKeyboard_Deinitialize(void);
s32 USBKeyboard_Open(const eventcallback cb);
void USBKeyboard_Close(void);
bool USBKeyboard_IsConnected(void);
s32 USBKeyboard_Scan(void);
s32 USBKeyboard_SetLed(const u32 led, bool on);
