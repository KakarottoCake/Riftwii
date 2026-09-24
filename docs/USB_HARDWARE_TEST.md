# SD and USB d2x hardware test

RiftWii boots Wii images from an SD card (FAT32) or a USB drive (FAT32 or
NTFS) through a user-installed d2x cIOS. It does not support IOS58-only image
boot, RVZ, or a cIOS-free path. RiftWii never writes USB. The SD catalog and image mapper
are read-only; because SD also holds XML packages, logs, and redirected saves,
those normal RiftWii files may be written there.

Use known-good media with 512-byte logical sectors. Put a Wii `.wbfs`
file (and every consecutive `.wbf1`, `.wbf2`, … piece when split) in
`usb:/wbfs`, either directly or one level below it, for example
`usb:/wbfs/Game [RMCE01]/RMCE01.wbfs`. Raw `.iso` files belong in
`usb:/games`; the equivalent SD paths are `sd:/wbfs` and `sd:/games`. Install d2x cIOS (v11 beta3 is the latest) in 249, or use
250/251; RiftWii tries those slots in that order when no slot is supplied.
When no slot holds a cIOS ticket the GUI marks the selected SD or USB image
status line with a no-cIOS warning and the log names the missing slots;
a slot holding a stub, or an IOS that fails the d2x probe, fails at launch
with the same remedy. Revision numbers are not compared: the guided
installer stamps 65535 whatever the version, so the F9/FA probe after the
reload stays the real capability check.

Read-only-first sequence:

1. Keep the SD card inserted with `sd:/riftwii/autorun.txt` containing
   `usb RMCE01`, `probe`, and `layout`. For SD images use `sd RMCE01`.
   Start RiftWii and save
   `sd:/riftwii/autorun.log` after it returns or powers off.
2. Confirm the log identifies `USB: RMCE01` or `SD: RMCE01`, reports a d2x cIOS, opens the
   virtual game partition, and shows the same ID after the virtual probe.
3. Add `xml sd:/riivolution/example.xml` and `launch` to exercise the existing
   package/runtime path. Only after that succeeds, use `boot` for an
   unmodified game.
4. Test the menu: the home screen lists SD and USB games and the disc
   drive; pick each, verify the game page lists the packs for that ID,
   then boot.

Dolphin does not validate d2x's SD/USB ownership, IOS reload behavior, F9 fragment
DMA, or a physical device's sector geometry. Those are hardware-only checks.
The implementation validates container headers and mappings on the host; it
does not decrypt game partitions or include Nintendo keys.

Protocol provenance: the command numbers and native fragment layout were
checked against the public GPL-3.0-or-later d2x-cIOS DIP plugin source,
`source/dip-plugin/{ioctl.h,frag.h,plugin.c}` (local public source checkout
used for protocol facts only). RiftWii contains an independent implementation.
