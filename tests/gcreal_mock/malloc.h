// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdlib>
void* test_card_memalign(std::size_t alignment, std::size_t bytes);
#define memalign test_card_memalign
