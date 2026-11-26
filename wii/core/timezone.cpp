// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// newlib's time zone set-up, without its TZ parser. localtime, mktime and
// libfat's file times call _tzset_unlocked_r, which reads the TZ variable
// with siscanf: that brought in newlib's whole formatted input engine, its
// multibyte reader and a 14 KiB table of Unicode categories (27 KiB of
// MEM1 together). A Wii program has no environment, so TZ is never set and
// newlib always took this branch: no offset, no daylight saving, "GMT". The
// Wii's clock already keeps the console's local time (libogc's time()).
// Makefile.wii links with --wrap=_tzset_unlocked_r.
#include <time.h>

extern "C" void __wrap__tzset_unlocked_r(struct _reent*) {
    static char gmt[] = "GMT";
    _timezone = 0;
    _daylight = 0;
    _tzname[0] = gmt;
    _tzname[1] = gmt;
}
