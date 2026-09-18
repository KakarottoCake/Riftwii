# Riftwii

Independent Wii disc mod loader with an original libgui frontend and a host-tested
XML/overlay core. Not affiliated with Riivolution; the runtime backend is not
implemented yet.

## Status

- `riftwii` static library: XML patch package parsing, disc filtering, patch
  planning, and read overlay composition (host-buildable).
- `vendor-pugixml`: MIT-licensed XML parser, pinned at v1.15.
- `vendor-libgui`: pinned GPL libwiigui 1.07 snapshot, used only by the Wii
  frontend (not built by the host build).

## Host build

Requires CMake 3.20+, Ninja, and a C++17 compiler.

```
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Wii frontend

Requires devkitPPC, libogc, libfat, and the Wii/PPC portlibs used by
`Makefile.wii`. Run from the project root in the devkitPro shell:

```
make -f Makefile.wii
```

This produces `riftwii.dol` and `riftwii.elf`. The build uses relative paths
and supports a project directory containing spaces. The frontend links the
XML/overlay core and libwiigui; it is not part of the host test build.

The current frontend scans `sd:/riivolution` for XML files (up to 150),
displays validation results, and shows details when a package is selected.
Rescan uses the on-screen button, Wii Remote Plus, or GameCube X. Home exits.
Files larger than 1 MiB are rejected rather than silently truncated.
Validation covers the supported XML subset, not external file availability
or whether a game can launch. XML declarations and unsupported patch features
are currently rejected.

### Manual testing

Copy `riftwii.dol` to `sd:/apps/riftwii/boot.dol` and launch it from the
Homebrew Channel, or load the DOL in Dolphin with an SD image configured.
Verify startup, pointer/controller navigation, scrolling, package details,
rescan, and exit. Test missing/empty package directories, malformed XML,
uppercase `.XML` extensions, files larger than 16 KiB, and the 1 MiB limit.
Test more than eight packages to exercise scrolling. Hardware/emulator
behavior has not been verified by the automated host tests.

## Scope

The loader validates and plans Riivolution-format XML patches and composes
read overlays. Milestone 1 (complete): one full file-replacement path from
a real game source through patch planning to a consumed replacement —
`DirectoryProvider` (game dir + SD dir) feeds `build_replacement()`, which
turns a planned `FilePatch` into an owned `AppliedFile` view backed by the
existing `ReadOverlay`. Covered and tested: `offset` (low 2 bits cleared to
match console behaviour), `fileoffset`, `length` (0 = rest of external),
`resize` (truncate/extend vs keep tail), `create` (missing disc becomes an
empty original), short-external zero padding, past-EOF zero gaps, overflow
checks, and a 256 MiB per-file cap. The end-to-end test runs XML text →
`parse_package` → `plan_files` → provider → split `read()` consumption and
compares every byte against an oracle.

Still future: Wii DVD/DI disc source (the provider interface is ready for
one), folder/memory/savegame patch kinds, and booting a game with the
overlays on hardware. The frontend remains an SD XML validator until then.
