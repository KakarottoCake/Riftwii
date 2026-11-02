# Nintendont's controller layouts

The `.ini` files here are Nintendont's `controllerconfigs/*.ini`, copied
unchanged from https://github.com/FIX94/Nintendont at commit
`c683db301c941a64e6fe5cef6ac079bd1a726074`. They are by FIX94 and the
Nintendont contributors. Nintendont is GPL-2.0, and its developers allow
RiftWii to use its work with attribution (see `NOTICE.md`).

`tools/make_builtin_pads.py` turns them into `src/data/builtinpads_data.cpp`,
the USB controller layouts RiftWii uses when the SD card has no file for a
pad. Run it again after changing anything here.
