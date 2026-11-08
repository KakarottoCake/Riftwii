// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static uint32_t rtstub_read32(uint32_t address);
static void rtstub_write32(uint32_t address, uint32_t value);
static uint32_t rtstub_physical(const volatile void* pointer);
static void rtstub_flush(const volatile void* pointer, uint32_t length) {
    (void)pointer;
    (void)length;
}
static void rtstub_invalidate(const volatile void* pointer, uint32_t length) {
    (void)pointer;
    (void)length;
}
