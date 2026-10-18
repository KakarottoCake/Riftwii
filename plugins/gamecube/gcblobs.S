/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube runtime blobs in the plugin's constants (gcblobs.h).
 * Makefile.plugin assembles this with -Wa,-I<build-runtime>, the folder
 * Makefile.runtime writes them to, where .incbin finds them. Each is
 * 32-byte aligned, as the loader reads the blob's header in place; its
 * size follows as a word. */

    .section .rodata.gcblobs, "a"

    .balign 32
    .global riftwii_gc_bin
riftwii_gc_bin:
    .incbin "riftwii_gc.bin"
riftwii_gc_bin_end:

    .balign 32
    .global riftwii_gc_rvz_zst
riftwii_gc_rvz_zst:
    .incbin "riftwii_gc_rvz.zst"
riftwii_gc_rvz_zst_end:

    .balign 32
    .global riftwii_gc_rvz_rel
riftwii_gc_rvz_rel:
    .incbin "riftwii_gc_rvz.rel"
riftwii_gc_rvz_rel_end:

    .balign 4
    .global riftwii_gc_bin_size
riftwii_gc_bin_size:
    .long riftwii_gc_bin_end - riftwii_gc_bin
    .global riftwii_gc_rvz_zst_size
riftwii_gc_rvz_zst_size:
    .long riftwii_gc_rvz_zst_end - riftwii_gc_rvz_zst
    .global riftwii_gc_rvz_rel_size
riftwii_gc_rvz_rel_size:
    .long riftwii_gc_rvz_rel_end - riftwii_gc_rvz_rel
