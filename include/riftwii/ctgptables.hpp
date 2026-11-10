// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "riftwii/ctgpefa.hpp"

// The CTGP-R channel's own constants and tables that the native launch needs.
// They are CTGP-R's, not RiftWii's work, but they are not secrets either: the
// AES key and the chunk IV are four PPC instruction words from CTGP's own code,
// and the row table, the course table and the cup layouts are plain CTGP
// data. So the launch reads none of them
// from a file on the card (there is no efa.key, rows.bin or tables.bin): the
// keys are built in here, and the row table, course table and cup layouts are
// baked into the build (ctgprowdata.cpp and ctgpapply.cpp). The one input the
// launch still needs is the player's own copy of CTGP-R, its blob.bin and
// jelly.bin, which these keys open (docs/CTGPR.md).
namespace riftwii {

struct CtgpTables {
    CtgpChunkIv efa_iv;
    CtgpEfbKeyWords efb_key;
};

// blob.bin's AES-128 key: four CTGP-R instruction words (not a secret).
std::array<std::uint8_t, 16> ctgp_blob_key();
// blob.bin's and jelly.bin's chunk IV: words 0, 1 and 3 (word 2 is the chunk's
// first sector), the same CTGP-R instruction words.
CtgpChunkIv ctgp_blob_chunk_iv();
// jelly.bin's per-file AES key words 1 and 3 (word 0 is the file's last-write
// time, word 2 its creation time): CTGP-R's own instruction words again.
CtgpEfbKeyWords ctgp_jelly_key_words();

// CTGP-R's row table, baked into the build (src/ctgpr/ctgprowdata.cpp, made by
// tools/ctgpr/gen_rowdata.py): "CRT0", the row count, the rows and 54 region
// words, big endian, as ctgp_parse_row_table reads them (ctgpimage.hpp).
const std::uint8_t* ctgp_embedded_row_table(std::size_t& size);

}  // namespace riftwii
