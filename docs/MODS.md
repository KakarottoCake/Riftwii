# Riivolution mods from GameBanana

A Wii game's **Mods** page has **Get more mods...**. It opens the Rift
Store on the Riivolution mods GameBanana has for that game. Pick one, press
Install, and it goes on the SD card; back on the Mods page it is listed and
switched on. Mods packed as 7z or rar can't be unpacked on the Wii: they
show **Get it on a PC**, with a QR code to the mod's GameBanana page.

GameBanana's mods are downloaded from GameBanana's own servers; RiftWii
publishes only the list of them and small preview icons. The mod's page in
the store links (QR code) to its GameBanana page, where its author is
credited and its description, updates and comments live.

A few Super Mario Galaxy and Galaxy 2 mods are shared only on MEGA, Google
Drive or MediaFire, which a Wii can't download from. With their makers'
agreement through Luma's Workshop, `RiftWii/mods` keeps a copy of each in
its `files` release and lists it in `hosted.json` (`--hosted`): every
maker credited, the page linking to the mod's own page or trailer. Their
ids start with `rw-`; one can stand in for a GameBanana listing the Wii
can't install (`replaces`, e.g. a rar). The RiftWii/mods README says how
to add one.

## The list

`tools/mod_list.py build <out>` writes `<out>/mods.json` and
`<out>/icons/<id>.png`. The `RiftWii/mods` repository runs it every night
and publishes the result at `https://riftwii.com/mods/mods.json`
(the repository's GitHub Pages; RiftWii fetches it from riftwii.com
itself, where riftwii.github.io redirects). How it finds the mods:

1. **Which games.** Every game a GameBanana search for "riivolution" names,
   and a list of Wii games with modding scenes (`SEED_GAMES`).
2. **Which mods.** Every mod of those games. A game with more than 600
   mods (Mario Kart Wii, Brawl: thousands, nearly all single-file
   replacements) has only the mods a search for Riivolution finds looked
   at, or a night would not be enough.
3. **Which files.** A mod's files are read from its GameBanana page. A zip
   is looked into first by reading its last 64 KiB (its file list) with an
   HTTP range request; only a zip that holds a `riivolution/*.xml` is
   downloaded whole. 7z and rar files of up to 64 MB are downloaded and
   listed with the `7z` command.
4. **What is checked.** GameBanana's virus scan must have passed; the size
   and MD5 must be GameBanana's; every name in the zip must be one the Wii
   can write; the pack may bring at most four folders beside
   `riivolution/` (the store's data folders), none of them one the Wii,
   RiftWii or a loader owns. Mods GameBanana hides or warns about (mature
   content) and private, trashed or withheld ones are left out.
5. **What is kept.** The SHA-256 of the file, its size, file count and
   unpacked size, the game IDs its XMLs name (`<id game="RMG">`), and where
   the pack is inside the archive.

Everything learned is cached (`--cache`, default `.mod-cache`): a mod's
page until GameBanana says it changed, a file's verdict for good, since a
GameBanana file never changes (a new version is a new file). A nightly run
only looks at what is new.

## The format

`mods.json` is the app list's schema (docs/APPSTORE.md), read by the same
parser (`src/net/applist.cpp`), with `"kind": "mod"` and these fields:

| Field | |
| --- | --- |
| `category` | `mods`, which only mods may use. |
| `game` | The game's name on GameBanana, for the menu. |
| `games` | The game IDs the mod's XMLs name, at most 4, 3 to 6 capital letters and digits. A mod is shown for a game whose ID starts with one of them. A mod whose XML names none is listed but matches no game. |
| `program` | The mod's XML on the card, `riivolution/<name>.xml`. The mod counts as installed while it is there. |
| `root` | The folder in the archive that holds `riivolution/` (`"Beta v1/"`), or absent for the archive's top. The install rules are under it; nothing outside it is installed. |
| `install` | `riivolution/` to `riivolution/`, and each of the pack's folders to itself at the card's root. |
| `data_folders` | The pack's folders: removing the mod asks before deleting them whole. |
| `page` | The mod's GameBanana page. |
| `download.url` | `https://gamebanana.com/dl/<file id>`, which redirects to GameBanana's file servers. |

The Wii's rules for mods: the XML must be in `riivolution/` and end in
`.xml`; `root` must be a plain relative folder; only mods may install into
`riivolution/`, and an app may not use the `mods` category; an unknown
`kind` is left out, so a later list can add kinds an older RiftWii skips.

## On the Wii

`wii/menu/rift_menu.cpp` (MenuMods) adds the row, and opens the store with
the game's ID and name in `rwp_appstore_request` (`plugin_appstore.h`). The
store (`plugins/appstore/`) then downloads `mods.json` instead of the app
list and opens straight on the game's mods; B there leaves the store. A
mod installs, updates and removes as an app does. What an install replaces:

- the mod's own XML (`program`), always: an XML left from an older
  version would not match the files beside it;
- files the store put there and nobody changed since;
- in the mod's own folders, a file the store didn't put there, when the
  store has no note of installing the mod (or a note without its file
  list). That is the case for a copy of the mod put there by hand (the
  store then asks Replace first, since the mod's XML is there) and for a
  first Install whose folders already hold files of the same name.

Everything else is kept: a file the player changed since the store put it
there, any file in `riivolution/` the store didn't put there (it may be
another pack's), and anything in `riivolution/save/`, where Riivolution
keeps the saves it redirects, which the store never writes.

A removal deletes the files the store put there that nobody changed since.
Two exceptions: when the store's note of the install has no file list (an
older note), it deletes the mod's XML whatever its state; and when the
player then also answers **Remove them** to the question about the mod's
folders, those folders are deleted whole, with everything in them.

When the store
closes, the game's packs are read again and a mod that is new on the card
is switched on.

GameBanana's file servers send their TLS certificate chain out of order
(the root's cross-certificate before the intermediate). BearSSL checks a
chain in the order sent, so `wii/online/tls.cpp` keeps the certificates and,
only when BearSSL finds a name mismatch, checks the other orders of the
ones after the server's own. The chain must still end at one of RiftWii's
roots; nothing is trusted that the right order wouldn't be.

## Asking for a mod to be taken off

A mod's author can ask for it to be left out of the list by opening an
issue in `RiftWii/mods`. Its GameBanana id then goes in that repository's
`excluded.txt`, which the nightly build passes as `--exclude`.
