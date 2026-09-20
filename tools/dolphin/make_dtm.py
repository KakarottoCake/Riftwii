# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a DTM movie for the Kirby save-creation run.
Header is the confident 137-byte prefix (filetype through
recordingStartTime) plus zero padding to exactly 256 bytes; every field
past the prefix is ignored by Dolphin when bSaveConfig is false.
Body: Wiimote1 only (controllers 0x10), length-prefixed frames:
  empty  -> 01 00
  2 held -> 03 01 20 00   (buttons present, bit5 = two)
  A tap  -> 03 01 01 00   (buttons present, bit0 = A)
Layout: --empty EMPTY_N frames, then 2 held with an A tap every 120
frames (60 on, 60 off) so both the hold-screen and the Yes/No confirm
are covered no matter when they appear.
"""
import struct
import sys
import time

out_path = sys.argv[1] if len(sys.argv) > 1 else "kirby2.dtm"
total = int(sys.argv[2]) if len(sys.argv) > 2 else 36000
empty_frames = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
game_id = sys.argv[4].encode("ascii") if len(sys.argv) > 4 else b"RK5E01"
assert len(game_id) == 6, "game id must be 6 chars"

hdr = bytearray(256)
hdr[0:4] = b"DTM\x1a"
hdr[4:10] = game_id
hdr[10] = 1      # bWii
hdr[11] = 0x10   # controllers: Wiimote1 only
hdr[12] = 0      # bFromSaveState
struct.pack_into("<Q", hdr, 13, total)   # frameCount
struct.pack_into("<Q", hdr, 21, total)   # inputCount
struct.pack_into("<Q", hdr, 129, int(time.time()))  # recordingStartTime
assert len(hdr) == 256

body = bytearray()
body += b"\x01\x00" * empty_frames
rest = total - empty_frames
# Cycle 2-tap, A-tap, Down-tap (40 frames each on 120-frame wheel):
# every confirm candidate gets fresh edges forever, so whenever a
# prompt appears one of them lands on it.
for i in range(rest):
    slot = (i % 360) // 120
    on = (i % 120) < 40
    b = d = 0
    if on:
        if slot == 0:
            b = 0x20  # 2
        elif slot == 1:
            b = 0x01  # A
        else:
            d = 0x02  # Down
    if b or d:
        body += bytes((3, 1, b, d))
    else:
        body += b"\x01\x00"

with open(out_path, "wb") as f:
    f.write(hdr)
    f.write(body)
print(f"wrote {out_path}: header 256 + body {len(body)} for {total} frames")
