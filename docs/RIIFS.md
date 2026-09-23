# Network packs (RiiFS)

RiiFS is the file-sharing protocol of Riivolution's PC companion
servers: a Wii reads mod files from a folder on a PC instead of the SD
card. RiftWii speaks it as a client, read-only, so the servers people
already run work unchanged. This file records the protocol as this
project understands it and how RiftWii uses it. The client
(`src/riifs.cpp`) was written for this project; no server or client code
was copied.

## How RiftWii uses it

RiftWii copies instead of streaming. Riivolution reads the PC's files
live while the game runs, from inside IOS; RiftWii's runtime lives in
the game's own memory, where a network stack would cost the game memory
and break games that reset the network themselves. So:

1. **Finding servers.** Any XML in `sd:/riivolution` may contain
   `<network protocol="riifs" address="192.168.1.20" port="1137"/>`, the
   way Riivolution is pointed at a PC. An empty address, or Settings >
   *Find network packs* set to On, also sends a broadcast ping to find
   servers on the local network. An XML that only names a server is not
   listed as a pack.
2. **The pack list.** When the menu reads the drives, it connects to each
   server and copies its `/riivolution/*.xml` into
   `sd:/riftwii/riifs/<address>_<port>/riivolution/`. Those packs show
   beside the card's ones, named `mod.xml @ 192.168.1.20:1137`.
3. **A launch.** Before the compile, RiftWii plans each network pack with
   the chosen options and copies the files and folders the plan names
   into the cache, laid out as on the server. The plan's paths are then
   rebased onto the cache (`RebasePlan`) and everything after (file
   lookup, folder expansion, the resident runtime) runs as for a pack on
   the card. A server that cannot be reached leaves the last copy in use,
   with a warning.

Change detection: the protocol reports a file's size but no modification
time, so a cached file of the same size counts as current. Settings >
*Copy network packs again* (or the autorun command `resync`) makes the
next launch copy every file again. Files the server no longer has are
removed from a copied folder, so a folder patch never applies a stale
file.

Saves: a pack's `<savegame>` folder is copied from the PC once, when the
cache has none, and lives on the card after that; nothing is written
back to the PC.

## The protocol

TCP, port 1137 by default. Every number is a big-endian 32-bit word
unless noted.

The client sends two kinds of message:

- **Set an argument**: `1, option, length, bytes[length]`. No answer. The
  server keeps the latest value of each option for the next commands.
- **Run a command**: `2, command`. The server answers, always ending with
  a signed 32-bit result word.

Options used by RiftWii: `0` handshake text, `1` file or directory
handle, `2` path (text, no terminator, absolute from the served folder's
root), `3` open mode, `4` length.

Commands used by RiftWii:

| Command | Arguments | Answer |
| --- | --- | --- |
| `0x00` handshake | option 0 = `"1.03"` | result: the server's protocol version (4; 3 for older servers; negative when refused) |
| `0x01` goodbye | | result |
| `0x17` stat | path | a 24-byte stat, then result (negative when the path does not exist) |
| `0x10` open | path, mode (0 = read) | result: a handle, negative on failure |
| `0x11` read | handle, length | exactly `length` bytes (padded with zeros past the end of the file), then result: how many of them are the file's |
| `0x16` close | handle | result |
| `0x21` open directory | path | result: a handle, negative when missing |
| `0x23` next name | handle | a 1024-byte field holding the name, then result: its length, negative at the end |
| `0x24` next stat | handle | the 24-byte stat of the entry just named, then result |
| `0x22` close directory | handle | result |

A stat is: identifier (64-bit), size (64-bit), device (32-bit), mode
(32-bit). A directory has mode bit `0x4000` set (servers set `0x8000`
too); a file has only `0x8000`. Listings may include `.` and `..`, which
RiftWii skips.

Finding a server: a UDP datagram holding the word `0x10` sent to the
broadcast address on the server's port; a server answers with one word,
its TCP port, from its own address.

The protocol also has write, seek, create, delete, rename and logging
commands, which RiftWii does not use.

## Testing

`tools/dolphin/riifs_server.py ROOT` is a small read-only server for the
Dolphin harness (it follows this document). In Dolphin, the emulated Wii
reaches the PC at `127.0.0.1`. The autorun command `netscan` fetches the
pack lists, as the menu's scan does.
