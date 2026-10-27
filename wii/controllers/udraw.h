/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: 2026 quatric (libwiiinput) <https://github.com/quatric/libwiiinput> */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The uDraw GameTablet's side of the TB_* calls (udraw.c), for
 * pentablet.c, which answers for both kinds of tablet. */
#ifndef RIFTWII_UDRAW_H
#define RIFTWII_UDRAW_H

#include "pentablet.h"

/* TB_SetHooks: off, the wiiuse wrappers only call wiiuse. */
extern volatile bool tb_hooks_on;

bool tb_udraw_pop(tb_sample *out);
bool tb_udraw_state(tb_sample *out);
bool tb_udraw_info(tb_info *info);
void tb_udraw_extent(s32 *w, s32 *h);

#endif /* RIFTWII_UDRAW_H */
