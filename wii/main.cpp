// SPDX-License-Identifier: GPL-3.0-or-later
#include <fat.h>
#include <gccore.h>
#include <cstdlib>

#include "FreeTypeGX.h"
#include "audio.h"
#include "input.h"
#include "menu.h"
#include "video.h"
#include "font_ttf.h"

int ExitRequested = 0;

void ExitApp() {
    ShutoffRumble();
    ShutdownAudio();
    StopGX();
    std::exit(0);
}

int main() {
    InitVideo();
    SetupPads();
    InitAudio();
    fatInitDefault();
    InitFreeType(const_cast<u8*>(font_ttf), font_ttf_size);
    InitGUIThreads();
    MainMenu(1);
    return 0;
}
