/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Observe coverage and yield points in the real decoder without adding
 * counters, callbacks or branches to the installed runtime. */
extern void rtzstd_test_observe(unsigned kind, unsigned value);
extern void rtzstd_test_checkpoint(void);
#define RTZSTD_OBSERVE(kind, value) rtzstd_test_observe(kind, value)
#define RTZSTD_CHECKPOINT() rtzstd_test_checkpoint()
#include "../runtime/rtzstd.c"
