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

Requires devkitPPC with libogc, libfat, and freetype. The frontend links
`vendor-libgui` and is excluded from the host test build until its runtime
backend exists.

## Scope

The loader validates and plans Riivolution-format XML patches and composes
read overlays. Launching a game with those overlays on real hardware is a
future milestone.
