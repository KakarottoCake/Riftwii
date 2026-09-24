# WiiLink WFC launcher pieces

From WiiLink's `wfc-patcher-wii` (https://github.com/WiiLink24/wfc-patcher-wii,
commit `0d9d7a697d7fb299ecc629f2c5da071d116369f2`, directory `launcher/source`),
used under the GNU GPL version 2 or later, which that project offers beside its
own licence (`LICENSE`). Online communications credit to WiiLink WFC -
https://wfc.wiilink.ca

Altered from the originals:

- `Patch.S` is `wwfcPatch.S`, `ASM.h` is `wwfcAsm.h`, `GameAddresses.*` are
  `wwfcGameAddresses.*` and `Stage1Payload.hpp` is `wwfcStage1Payload.hpp`
  (RiftWii's build needs file names that differ from its own, ignoring case);
  the `#include` lines follow the new names. Nothing else is changed.

`wii/wfc.cpp` does what the launcher's `PatchAndLaunchDol` does to a game it
has loaded, in RiftWii's own code.
