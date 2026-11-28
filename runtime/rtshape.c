/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Samples as the Wii controller the game expects (rtshape.h). */
#ifndef RT_PAD_STANDIN
#define RT_PAD_STANDIN
#endif
#include "rtshape.h"

#include "rthome.h"

uint32_t rtshape_classic_buttons(const struct rtsrc_sample* p) { return rthome_map_classic(p->buttons, RTHOME_CL_PLUS); }
