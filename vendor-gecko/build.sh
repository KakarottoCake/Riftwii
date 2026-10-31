#!/bin/bash
# Rebuilds the three Gecko code handlers from their sources and checks that
# each is the file RiftWii ships, byte for byte:
#   codehandleronly   the cheats alone
#   codehandler       with the WiiRD debugger, USB Gecko in slot B
#   codehandlerslota  with the WiiRD debugger, USB Gecko in slot A
# Needs devkitPPC. Run from the repository root:   vendor-gecko/build.sh
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
P="${DEVKITPPC:-/opt/devkitpro/devkitPPC}/bin/powerpc-eabi-"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
failed=0
for name in codehandleronly codehandler codehandlerslota; do
    # The debugger handlers' sources name some registers in a way newer
    # binutils warns about ("invalid register expression"); the bytes it
    # makes are still the shipped ones, which the cmp below proves.
    "${P}as" -mregnames -mgekko "$DIR/$name.s" -o "$OUT/$name.o" 2>/dev/null
    # Linked where RiftWii loads it (0x80001800), so its own addresses are right.
    "${P}ld" -Ttext 0x80001800 -e 0x80001800 "$OUT/$name.o" -o "$OUT/$name.elf"
    "${P}objcopy" -O binary "$OUT/$name.elf" "$OUT/$name.bin"
    if cmp -s "$OUT/$name.bin" "$DIR/$name.bin"; then
        echo "$name.bin matches $name.s"
    else
        echo "$name.bin differs from what $name.s builds" >&2
        failed=1
    fi
done
exit $failed
