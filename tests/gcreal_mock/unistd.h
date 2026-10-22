// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
void test_card_sleep(unsigned microseconds);
#define usleep test_card_sleep
