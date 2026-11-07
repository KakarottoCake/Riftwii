// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static int32_t call(struct rt_fault_context* context, uint32_t* request);
static uint32_t physical(const void* pointer) { return (uint32_t)(uintptr_t)pointer & 0x3fffffffu; }
static void flush(const void* pointer, uint32_t length) {
    (void)pointer;
    (void)length;
}
static void fault_time_base(uint32_t* hi, uint32_t* lo) {
    *hi = 1;
    *lo = 2;
}
