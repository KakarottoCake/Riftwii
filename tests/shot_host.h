// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* Only the PowerPC instructions and SDK entry addresses are replaced.
 * The production NAND state machine is compiled unchanged. */
static uintptr_t shot_host_api(uint32_t address);
#define SHOT_API(type, address) ((type)shot_host_api(address))
static uint32_t interrupts_off(void) { return 0; }
static void interrupts_restore(uint32_t msr) { (void)msr; }
static uint32_t now(void) { return 1; }
static void flush(const void* data, uint32_t length) {
    (void)data;
    (void)length;
}
