/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The eight in-game pad builds (runtime/pad) in the plugin's constants.
 * Makefile.plugin assembles this with -Wa,-I<build-runtime>, the folder
 * Makefile.runtime writes them to, where .incbin finds them. Each is
 * 32-byte aligned; the table after them gives each one's start and size,
 * in padhook.cpp's BlobKind order (riftwii/plugin_pads.h). */

    .section .rodata.padblobs, "a"

    .balign 32
riftwii_pad_bin:
    .incbin "riftwii_pad.bin"
riftwii_pad_bin_end:

    .balign 32
riftwii_pada_bin:
    .incbin "riftwii_pada.bin"
riftwii_pada_bin_end:

    .balign 32
riftwii_padp_bin:
    .incbin "riftwii_padp.bin"
riftwii_padp_bin_end:

    .balign 32
riftwii_padn_bin:
    .incbin "riftwii_padn.bin"
riftwii_padn_bin_end:

    .balign 32
riftwii_padan_bin:
    .incbin "riftwii_padan.bin"
riftwii_padan_bin_end:

    .balign 32
riftwii_padpn_bin:
    .incbin "riftwii_padpn.bin"
riftwii_padpn_bin_end:

    .balign 32
riftwii_padc_bin:
    .incbin "riftwii_padc.bin"
riftwii_padc_bin_end:

    .balign 32
riftwii_padac_bin:
    .incbin "riftwii_padac.bin"
riftwii_padac_bin_end:

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
