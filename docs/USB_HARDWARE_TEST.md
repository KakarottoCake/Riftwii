# USB d2x hardware test

This milestone boots Wii images only from a FAT32 USB device through a
user-installed d2x cIOS. It does not support IOS58-only USB boot, NTFS, RVZ,
or a cIOS-free path. Riftwii never writes the USB device; XML packages,
replacement files, and redirected saves remain on SD.

Use a known-good FAT32 drive with 512-byte logical sectors. Put a Wii `.wbfs`
file (and every consecutive `.wbf1`, `.wbf2`, … piece when split) in
`usb:/wbfs`, either directly or one level below it, for example
`usb:/wbfs/Game [RMCE01]/RMCE01.wbfs`. Raw `.iso` files belong in
`usb:/games`. Install d2x cIOS (v11 beta3 is the latest) in 249, or use
250/251; Riftwii tries those slots in that order when no slot is supplied.
When no slot holds a cIOS ticket the GUI marks the USB status line with
`[no cIOS: install d2x for USB boot]` and the log names the missing slots;
a slot holding a stub, or an IOS that fails the d2x probe, fails at launch
with the same remedy. Revision numbers are not compared: the guided
installer stamps 65535 whatever the version, so the F9/FA probe after the
reload stays the real capability check.

Read-only-first sequence:

1. Keep the SD card inserted with `sd:/riftwii/autorun.txt` containing
   `usb RMCE01`, `probe`, and `layout`. Start Riftwii and save
   `sd:/riftwii/autorun.log` after it returns or powers off.
2. Confirm the log identifies `USB: RMCE01`, reports a d2x cIOS, opens the
   virtual game partition, and shows the same ID after the virtual probe.
3. Add `xml sd:/riivolution/example.xml` and `launch` to exercise the existing
   package/runtime path. Only after that succeeds, use `boot` for an
   unmodified game.
4. Test the GUI Source button: cycle among USB games and Disc, verify the
   status line and package filtering change with the selected ID, then boot.

Dolphin does not validate d2x's USB ownership, IOS reload behavior, F9 fragment
DMA, or a physical drive's sector geometry. Those are hardware-only checks.
The implementation validates container headers and mappings on the host; it
does not decrypt game partitions or include Nintendo keys.

Protocol provenance: the command numbers and native fragment layout were
checked against the public GPL-3.0-or-later d2x-cIOS DIP plugin source,
`source/dip-plugin/{ioctl.h,frag.h,plugin.c}` (local public source checkout
used for protocol facts only). Riftwii contains an independent implementation.
