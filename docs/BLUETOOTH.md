# Other Bluetooth controllers without a cIOS: research

The question: could RiftWii do for the Wii what
[MissionControl](https://github.com/ndeadly/MissionControl) does for the
Switch, letting a PS4, PS5, Xbox or Switch Pro controller connect over
Bluetooth and work as the console's own controller, without a custom IOS?

Short answer: it can be done in principle, but not as a small change. It
needs a Wii Remote emulator running inside every game, and it can only be
tested on a real Wii with the real controllers. The first piece exists
now: RiftWii's in-game screenshot blob already sits between the game and
the Bluetooth dongle and reads the Wii Remote's reports (see "What exists"
below). This page lays out the rest so it can be built in steps.

## How MissionControl works, and why the Wii is different

MissionControl is a Switch system module. The Switch has one Bluetooth host
stack, in a system service, shared by every game. MissionControl patches
that service so it accepts pairing with controllers that are not Nintendo's,
and translates their input reports into Switch Pro Controller reports
before games see them. The Switch's radio does Bluetooth 4.x with Secure
Simple Pairing, so modern controllers pair with it normally.

The Wii is built differently:

- **The Bluetooth host stack is in each game**, not in the system. IOS only
  exposes the USB Bluetooth dongle (a Broadcom BCM2045) as
  `/dev/usb/oh1/57e/305`. HCI commands go out on the control endpoint,
  events come in on the interrupt endpoint (0x81) and data (ACL) on the
  bulk endpoints (0x02 out, 0x82 in). The game's SDK runs HCI, L2CAP and
  HID itself. There is no single service to patch. The code has to sit in
  each game, between its stack and the dongle.
- **The radio is Bluetooth 2.0 + EDR**: no Secure Simple Pairing, no
  Bluetooth Low Energy. Controllers that only pair with SSP, or only speak
  BLE (Xbox Series controllers, and Xbox One controllers on current
  firmware), cannot connect at all.
- **Games accept only known devices.** The SDK connects Wii Remotes listed
  in SYSCONF (`BT.DINF`) and answers link key requests from its stored
  keys. A new device would have to be registered there, or hidden from the
  game's stack entirely.
- **Games only understand Wii Remotes.** The game sends output reports
  (LEDs, reporting mode, status requests, memory reads for calibration and
  extension identification, extension encryption setup, IR camera
  registers, speaker data) and waits for the matching input reports. A
  foreign controller must be presented as a complete Wii Remote, or the
  game's WPAD layer times out and shows "Communications with the Wii
  Remote have been interrupted".

fakemote (a d2x cIOS module, which RiftWii can already launch games with)
does the equivalent work inside IOS. It fakes the dongle for the game,
adds fake Wii Remotes backed by USB controllers, and emulates the Wii
Remote protocol for them. It uses USB rather than the radio, which avoids
pairing entirely.

## Doing it without a cIOS: the parts

All of it would run on the PowerPC, inside the game, in the IPC completion
path RiftWii's blobs already use:

1. **Tap the game's Bluetooth traffic.** Hook the game's
   `IOS_IoctlvAsync`, watch its USBV0 messages to the dongle, and read or
   rewrite HCI events and ACL data before the game's callbacks see them.
   *Done* for ACL input: `runtime/rtshot.c` and `runtime/shot/` (the
   screenshot combo reads Wii Remote buttons this way and hides HOME).
2. **Send our own HCI commands and ACL packets** through the same fd, and
   remove their replies (Command Complete and Command Status events, and
   the flow-control counts in Number Of Completed Packets) from what the
   game sees, so its HCI state stays consistent.
3. **Accept the foreign controller.** Answer its Connection Request
   ourselves (Accept Connection Request), answer its Link Key Request with
   the key it was paired with, open its HID control (PSM 0x11) and
   interrupt (PSM 0x13) channels, and keep all of that hidden from the
   game. Pairing depends on the controller:
   - DualShock 3: over USB, write the Wii's address into the controller
     (feature report 0xF5). It then connects to the Wii by itself.
   - DualShock 4 and DualSense: over USB, write the Wii's address and a
     link key into the controller. It then connects with legacy
     authentication using that key, which step 3 answers.
   - Switch Pro Controller, 8BitDo and others: pair over the air. Whether
     they accept legacy PIN pairing from a Bluetooth 2.0 host has to be
     tried controller by controller.

   The USB side of pairing would be a page in RiftWii's menu (libogc
   reaches USB HID devices there), done once per controller.
4. **Present a Wii Remote to the game.** Inject a Connection Request for a
   Wii Remote address the game accepts. To avoid writing SYSCONF (a
   corrupted SYSCONF stops the Wii Menu), the address of a Wii Remote
   already paired with the console can be borrowed while that remote is
   off. Then answer the game's L2CAP and HID traffic for it: its output
   reports (status, memory and register reads and writes, reporting mode)
   and a steady stream of input reports built from the foreign
   controller's state (buttons, and a Nunchuk or Classic Controller
   extension made from the sticks).
5. **Translate the controllers.** One small driver per family (DS3, DS4,
   DualSense, Switch Pro), mapping their HID reports onto Wii Remote,
   Nunchuk and Classic Controller buttons and sticks. Motion and pointer
   (IR) are the hard part: IR can be faked from a stick or a touchpad,
   and motion from the controller's gyro and accelerometer.

## Risks and cost

- Parts 2 to 5 are several thousand lines of code running in every game,
  inside interrupt handlers. A mistake disconnects the real Wii Remotes
  too.
- None of it can be tested in Dolphin. Dolphin's emulated Bluetooth has no
  foreign controllers, and passthrough needs a real dongle on the PC. Every
  step needs a Wii and the controllers.
- Registering devices in SYSCONF is avoided by design (step 4).

## A safer first step

**USB controllers as GameCube controllers**, in games that support the
GameCube controller. The GameCube adapter blob (`runtime/pad`) already
drives a USB HID device from inside the game through IOS 58's
`/dev/usb/hid` and feeds it into `PADRead`. A DualShock 3 or 4 on a USB
cable could be read the same way, with no Bluetooth, no Wii Remote
emulation and no cIOS. It covers fewer games (only those that take a
GameCube controller), but it is small, self-contained and follows a
pattern that works on hardware.

## The Wii U Pro Controller translator

The Wii U Pro Controller is the one foreign controller the Wii already
pairs and connects: it speaks the Wii Remote's protocol. Games still do
nothing with it. Its buttons and sticks come only in the extension bytes,
in a layout of its own, behind an extension ID no Wii game knows
(`0x0120`). Testers' Wii Remote logs also showed it disconnecting 10-40 ms
after the game's first requests.

`runtime/rtpro.c` (host-tested in `tests/pro_tests.cpp`) runs inside the
Wii Remote log blob (`runtime/wlog`), which hooks the game's
`IOS_IoctlvAsync` like the screenshot blob does. On a link whose address
is a pairing named "Nintendo RVL-CNT-01-UC", or whose extension ID reads
back as the Pro's:

- The controller is asked for reporting mode 0x34 (buttons and 19
  extension bytes, room for all 11 of the Pro's) whatever the game asked
  for. Each report is rebuilt in the game's own mode: no Wii Remote
  buttons, the accelerometer at rest, no IR points, and the extension
  bytes as a Classic Controller's. The sticks are scaled, and the button
  bytes are already in the Classic Controller's bit places.
- The extension ID reads back as a Classic Controller Pro's. Its
  calibration, and the Wii Remote's accelerometer calibration, read back
  as standard values. Errors on write acknowledgements (the Pro has no IR
  camera or speaker) read back as success.
- Encryption: a game that writes `0xAA` to `0xA400F0` and a 16-byte key at
  `0xA40040` decrypts every extension byte. The translator makes the two
  8-byte tables from that key and the game's own lookup tables, and
  encrypts what it hands the game. The tables are an answer table (7 rows
  of 6 bytes), a key box and eight table boxes; the loader finds them in
  the game's data (`find_extension_tables`), and RiftWii carries none of
  them. The Pro gets `0x55` in place of the `0xAA`, so its own bytes stay
  plain. If it encrypts them anyway, its first report shows it and they
  are decrypted.
- What a Pro cannot take never reaches it. A tester's Bluetooth trace
  (2610-407) showed why it hung up: the game's first request on a new link
  reads the Wii Remote's own memory (EEPROM `0x1770`, the firmware probe),
  and 27 ms later the Pro closed both channels and dropped the link, at
  every connection. The Pro now gets only what the Wii U asks of it:
  rumble, LEDs, the reporting mode, status, and reads and writes of the
  extension's registers (`0xA4xxxx`). A read of the Wii Remote's memory (or
  of its other registers) goes out as a read of the extension's
  calibration, and its answers come back as a Wii Remote's would: at the
  address the game asked for, the accelerometer calibration RiftWii's, the
  rest erased, and error 8 past the end of the memory (`0x1700`), which is
  how a Wii Remote answers the probe. The IR camera, the speaker and writes
  outside the extension go out as a status request when the game waits for
  an acknowledgement (the status answer comes back as it), and as the LEDs
  again when it does not.

The blob is on when the menu saw a Pro Controller (connect it in the menu
before starting the game).
`settings.txt` `pro_controller = off` leaves the traffic alone and keeps the
trace only. `pro_controller = every` takes every link as a Pro, so the
translator can run in Dolphin, which has no Pro Controller. Checked in
Dolphin (Mario Kart Wii): the game's tables found, its key checked out,
thousands of reports rebuilt with no disconnection; the firmware probe,
the memory reads and the IR camera and speaker set-up answered in the Pro's
place, and the game took the answers as a Wii Remote's. With a real Pro
Controller it is untested.

The same blob keeps a **Bluetooth trace** in the Wii Remote log
(`wpadlog.txt`). It records HCI commands and events, and the L2CAP
signalling and HID reports both ways (as they came, and as the translator
handed them to the game; and since log version 4, a report the game sent
next to what the translator sent in its place). The data report stream is
kept only at first, then counted. The text names each packet and gives a
disconnection's reason ("the controller ended it", "the Wii (the game's
Bluetooth stack) ended it"). Bluetooth addresses are cut to their last
byte, and link keys and PINs are never shown.

## What exists

- `runtime/rtshot.c`: parses HCI ACL packets from the bulk-in endpoint
  (handle, L2CAP header, HID input reports 0x20-0x22, 0x30-0x37,
  0x3E/0x3F) and can change a report in place. It is host-tested in
  `tests/shot_tests.cpp`.
- `runtime/shot/`: the `IOS_IoctlvAsync` hook that swaps in its own
  completion for the game's ACL reads, calls the game's callback
  afterwards, and chains with the resident runtime's hook of the same
  function.
- `runtime/wlog/` and `runtime/rtpro.c`: the same kind of hook on all of
  the game's traffic with the dongle (HCI commands and events, ACL both
  ways), with the Pro Controller translator and the Bluetooth trace above.

## Sources

- wiibrew: "Wiimote" (HID over L2CAP, reports, extensions),
  "/dev/usb/oh1" (the dongle's endpoints and the USBV0 ioctlvs),
  "SYSCONF" (`BT.DINF`).
- The Bluetooth Core Specification 2.0 + EDR: HCI, L2CAP, legacy pairing.
- fakemote and MissionControl: what they do, read as descriptions only.
- Dolphin's Bluetooth emulation: how the game's stack talks to the dongle.
