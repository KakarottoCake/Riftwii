#!/bin/bash
# Rebuilds codehandleronly.bin from codehandleronly.s and checks that it is
# the file RiftWii ships, byte for byte. Needs devkitPPC. Run from the
# repository root:   vendor-gecko/build.sh
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
P="${DEVKITPPC:-/opt/devkitpro/devkitPPC}/bin/powerpc-eabi-"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
"${P}as" -mregnames -mgekko "$DIR/codehandleronly.s" -o "$OUT/handler.o"
# Linked where RiftWii loads it (0x80001800), so its own addresses are right.
"${P}ld" -Ttext 0x80001800 -e 0x80001800 "$OUT/handler.o" -o "$OUT/handler.elf"
"${P}objcopy" -O binary "$OUT/handler.elf" "$OUT/codehandleronly.bin"
if cmp -s "$OUT/codehandleronly.bin" "$DIR/codehandleronly.bin"; then
    echo "codehandleronly.bin matches codehandleronly.s"
else
    echo "codehandleronly.bin differs from what codehandleronly.s builds" >&2
    exit 1
fi
