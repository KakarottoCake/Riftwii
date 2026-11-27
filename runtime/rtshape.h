/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shapes, the second of the stand-in's four layers (sources, shapes, slot
 * ownership, delivery; notes in #587): a controller's samples (rtsrc) as
 * the Wii controller the game expects. Only the shapes know a layout;
 * ownership and delivery never look at a controller's buttons. Free of the
 * hardware, so the host tests run it. */
#ifndef RIFTWII_RTSHAPE_H
#define RIFTWII_RTSHAPE_H

#include <stdint.h>

#include "rtsrc.h"

/* The Classic Controller's buttons in #587's default layout, as KPADStatus'
 * hold word has them: the control stick and C-stick as the two sticks, A B
 * X Y as a b x y, L and R as themselves, Z as ZR, Start as +, the D-pad as
 * the D-pad. */
uint32_t rtshape_classic_buttons(const struct rtsrc_sample* p);

#endif
