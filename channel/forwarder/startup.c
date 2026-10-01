// SPDX-License-Identifier: GPL-3.0-or-later
//
// libogc's start-up (SYS_PreMain, before main) closes every IOS handle
// from 0 to 31, open or not, for a program started by another under the
// same IOS. The channel never is: the system starts it under an IOS just
// loaded for it, with nothing open. And straight after a game was left
// while still loading (Wii Sports lets HOME through that early), one of
// those closes can go unanswered, so the channel stopped on a black screen
// before main. Those closes are skipped here; every other IOS_Close goes
// through. The linker's --wrap sends libogc's calls to these
// (Makefile.channel).

#include <gccore.h>

static int g_skip_closes;

s32 __real_IOS_Close(s32 fd);
s32 __wrap_IOS_Close(s32 fd) {
    if (g_skip_closes) return 0;
    return __real_IOS_Close(fd);
}

// SYS_PreMain makes the closes first, then calls __IOS_LoadStartupIOS.
void __real_SYS_PreMain(void);
void __wrap_SYS_PreMain(void) {
    g_skip_closes = 1;
    __real_SYS_PreMain();
    g_skip_closes = 0;
}

s32 __real___IOS_LoadStartupIOS(void);
s32 __wrap___IOS_LoadStartupIOS(void) {
    g_skip_closes = 0;
    return __real___IOS_LoadStartupIOS();
}
