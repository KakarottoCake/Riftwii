# The RiftWii channel

A Wii Menu channel that starts RiftWii without the Homebrew Channel.

It holds no copy of RiftWii. Its program (the forwarder,
`channel/forwarder/main.c`) shows the RiftWii logo for about half a second,
then loads `apps/riftwii/boot.dol` from the SD card, or from the USB drive when
the card has none. It hands the file's path over as `argv[0]`, as the Homebrew
Channel does, so RiftWii's own updates keep replacing that file and the channel
never needs reinstalling.

## Installing

RiftWii offers the channel once, on the first start where it can be installed.
The answer is remembered in `sd:/riftwii/channel_offered.txt`. Settings has
**RiftWii channel on the Wii Menu** to add, update or remove it later.

Installing needs a d2x cIOS in slot 249, 250 or 251. RiftWii closes the menu,
reloads the cIOS (releasing the drives as a launch does), installs, and starts
again with the result on Home. The steps are logged in `session.log` under
`Channel:`.

The channel is title `00010001-52465457` (`RFTW`), runs under IOS 58, and has
two contents:

- **0**, the banner: the icon, banner and sound shown in the Wii Menu.
- **1**, the forwarder.

The forwarder is linked at `0x81300000`, clear of RiftWii (`0x80a00000` up)
and of ordinary homebrew (`0x80004000` up).

## Building

`Makefile.channel` builds the forwarder, then `tools/make_channel.py build`
makes the package that `wii/channel.cpp` installs. `Makefile.wii` runs it and
embeds the package, zstd-compressed.

- **Banner and icon:** drawn by the script from `hbc/icon.png`, over a navy
  gradient. The light breathes, the logo swells a little, and a shine crosses
  it every four seconds.
- **Sound:** a short two-note chime, generated and DSP-ADPCM encoded by the
  script.
- **Preview:** a fifth argument to `build` writes still images of the banner
  and icon to that folder, to check the art without a Wii Menu.

No Nintendo data is used. The formats were written from their public
descriptions and checked against a retail banner's layout.

## Signing and encryption, on the console

The package's TMD and ticket are unsigned, and its contents unencrypted.
RiftWii does both on the console:

- **Title key:** a fixed key, encrypted with the console's common key by ES
  itself (`ES_Encrypt` with key handle 4). The common key never leaves IOS and
  is not in this repository. RiftWii checks the step by decrypting the key
  again.
- **Contents:** AES-128-CBC with that key, with the content index as the IV.
- **Signatures:** the TMD and ticket are fakesigned: a zeroed signature, and a
  counter in unused bytes chosen so the SHA-1 starts with a zero byte. The
  d2x cIOS's signature patches accept this.
- **Certificates:** read from the console's own `/sys/cert.sys`.

Dolphin checks real signatures, so the install only works on a Wii; there, the
Settings row explains why. Dolphin can still boot `build-channel/forwarder.dol`
directly, which tests the forwarder: the session log then says
`Started from sd:/apps/riftwii/boot.dol`.
