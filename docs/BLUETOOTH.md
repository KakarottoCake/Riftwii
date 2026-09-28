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

## What exists

- `runtime/rtshot.c`: parses HCI ACL packets from the bulk-in endpoint
  (handle, L2CAP header, HID input reports 0x20-0x22, 0x30-0x37,
  0x3E/0x3F) and can change a report in place. It is host-tested in
  `tests/shot_tests.cpp`.
- `runtime/shot/`: the `IOS_IoctlvAsync` hook that swaps in its own
  completion for the game's ACL reads, calls the game's callback
  afterwards, and chains with the resident runtime's hook of the same
  function.

## Sources

- wiibrew: "Wiimote" (HID over L2CAP, reports, extensions),
  "/dev/usb/oh1" (the dongle's endpoints and the USBV0 ioctlvs),
  "SYSCONF" (`BT.DINF`).
- The Bluetooth Core Specification 2.0 + EDR: HCI, L2CAP, legacy pairing.
- fakemote and MissionControl: what they do, read as descriptions only.
- Dolphin's Bluetooth emulation: how the game's stack talks to the dongle.
