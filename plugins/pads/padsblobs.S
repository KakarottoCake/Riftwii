/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-game pad builds (runtime/pad) in the plugin's constants, each
 * one zstd frame (Makefile.runtime's PADZST: the plain builds outgrew the
 * plugin arena at fourteen, and pack to about half). Makefile.plugin
 * assembles this with -Wa,-I<build-runtime>, the folder Makefile.runtime
 * writes them to, where .incbin finds them. Each is 32-byte aligned; the
 * table after them gives each frame's start and size, in padhook.cpp's
 * BlobKind order (riftwii/plugin_pads.h); a frame holds its build's own
 * size, which the launch checks against the DOL's buffer. */

    .section .rodata.padblobs, "a"

    .balign 32
riftwii_pad_bin:
    .incbin "riftwii_pad.zst"
riftwii_pad_bin_end:

    .balign 32
riftwii_pada_bin:
    .incbin "riftwii_pada.zst"
riftwii_pada_bin_end:

    .balign 32
riftwii_padp_bin:
    .incbin "riftwii_padp.zst"
riftwii_padp_bin_end:

    .balign 32
riftwii_padn_bin:
    .incbin "riftwii_padn.zst"
riftwii_padn_bin_end:

    .balign 32
riftwii_padan_bin:
    .incbin "riftwii_padan.zst"
riftwii_padan_bin_end:

    .balign 32
riftwii_padpn_bin:
    .incbin "riftwii_padpn.zst"
riftwii_padpn_bin_end:

    .balign 32
riftwii_padc_bin:
    .incbin "riftwii_padc.zst"
riftwii_padc_bin_end:

    .balign 32
riftwii_padac_bin:
    .incbin "riftwii_padac.zst"
riftwii_padac_bin_end:

    .balign 32
riftwii_padw_bin:
    .incbin "riftwii_padw.zst"
riftwii_padw_bin_end:

    .balign 32
riftwii_padaw_bin:
    .incbin "riftwii_padaw.zst"
riftwii_padaw_bin_end:

    .balign 32
riftwii_padsw_bin:
    .incbin "riftwii_padsw.zst"
riftwii_padsw_bin_end:

    .balign 32
riftwii_padasw_bin:
    .incbin "riftwii_padasw.zst"
riftwii_padasw_bin_end:

    .balign 32
riftwii_padpt_bin:
    .incbin "riftwii_padpt.zst"
riftwii_padpt_bin_end:

    .balign 32
riftwii_padapt_bin:
    .incbin "riftwii_padapt.zst"
riftwii_padapt_bin_end:

    .balign 4
    .global pads_builds
pads_builds:
    .long riftwii_pad_bin, riftwii_pad_bin_end - riftwii_pad_bin
    .long riftwii_pada_bin, riftwii_pada_bin_end - riftwii_pada_bin
    .long riftwii_padp_bin, riftwii_padp_bin_end - riftwii_padp_bin
    .long riftwii_padn_bin, riftwii_padn_bin_end - riftwii_padn_bin
    .long riftwii_padan_bin, riftwii_padan_bin_end - riftwii_padan_bin
    .long riftwii_padpn_bin, riftwii_padpn_bin_end - riftwii_padpn_bin
    .long riftwii_padc_bin, riftwii_padc_bin_end - riftwii_padc_bin
    .long riftwii_padac_bin, riftwii_padac_bin_end - riftwii_padac_bin
    .long riftwii_padw_bin, riftwii_padw_bin_end - riftwii_padw_bin
    .long riftwii_padaw_bin, riftwii_padaw_bin_end - riftwii_padaw_bin
    .long riftwii_padsw_bin, riftwii_padsw_bin_end - riftwii_padsw_bin
    .long riftwii_padasw_bin, riftwii_padasw_bin_end - riftwii_padasw_bin
    .long riftwii_padpt_bin, riftwii_padpt_bin_end - riftwii_padpt_bin
    .long riftwii_padapt_bin, riftwii_padapt_bin_end - riftwii_padapt_bin
