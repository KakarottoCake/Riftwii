# Argument test app

This tiny Wii app writes `argc` and every `argv` to `sd:/argv-test.txt`,
closes the card, and returns through the installed loader stub. It runs
no emulator and needs no BIOS or game data.

From the repository root in devkitPro's MSYS2 shell:

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make -f tools/dolphin/argv-emulator/Makefile
```

On a Dolphin test card, copy `build_wii/argv-emulator/boot.dol` and this
folder's `riftwii-emulator.ini` to `apps/argv-test/`. Create empty files
`roms/argv-test/Power Stone.test` and `roms/argv-test/Other Game.test`.

Open RiftWii's Emulators view (1 cycles the views). Open Power Stone,
set Preset to `power stone`, Frame skip on and Number to -2, then Play.
The output must contain:

```text
argc=10
argv[0]=<sd:/apps/argv-test/boot.dol>
argv[1]=<-game>
argv[2]=<sd:/roms/argv-test/Power Stone.test>
argv[3]=<-preset>
argv[4]=<power stone>
argv[5]=<-number>
argv[6]=<-2>
argv[7]=<name=Power Stone>
argv[8]=<sd:/roms/argv-test>
argv[9]=<-frameskip>
```

There are **10** arguments including argv[0]. Each pair of angle brackets marks one
argument, including its spaces. [EMULATORS.md](../../../docs/EMULATORS.md)
describes the format. Check a second game retains its own settings, the
defaults page, and return behavior as described in the task report.
