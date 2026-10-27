/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The HOME Menu from a GameCube controller (#224 part 1): L, R, Z and
 * Start held, the combo of the way back (rtleave.h), make a Classic
 * Controller appear on a free Wii Remote channel and press its HOME. The
 * game opens its own HOME Menu on that press (or, mid-save or loading,
 * shows its crossed-out HOME icon and doesn't), and while the menu is up
 * the stand-in carries the pad's stick and buttons, so the menu can be
 * used to the end: its buttons, Wii Menu, Reset, closing it. Once the menu
 * has closed the stand-in is gone, so games that read GameCube controllers
 * themselves (Brawl, Mario Kart Wii) never see an extra player.
 *
 * The decisions, kept free of the hardware so the host tests can run them;
 * the pad blob (runtime/pad) calls them from its PADRead, KPAD read,
 * WPADProbe and HBMCalc hooks, and writes the Classic Controller status
 * into the game's KPAD buffer (rthome_classic, rthome_float_bits). */
#ifndef RIFTWII_RTHOME_H
#define RIFTWII_RTHOME_H

#include <stdint.h>

/* A Classic Controller's buttons as KPAD reports them (KPADStatus's
 * extension hold, trig and release words; wiibrew, "Classic Controller"). */
#define RTHOME_CL_UP 0x0001u
#define RTHOME_CL_LEFT 0x0002u
#define RTHOME_CL_ZR 0x0004u
#define RTHOME_CL_X 0x0008u
#define RTHOME_CL_A 0x0010u
#define RTHOME_CL_Y 0x0020u
#define RTHOME_CL_B 0x0040u
#define RTHOME_CL_ZL 0x0080u
#define RTHOME_CL_R 0x0200u
#define RTHOME_CL_PLUS 0x0400u
#define RTHOME_CL_HOME 0x0800u
#define RTHOME_CL_MINUS 0x1000u
#define RTHOME_CL_L 0x2000u
#define RTHOME_CL_DOWN 0x4000u
#define RTHOME_CL_RIGHT 0x8000u

#define RTHOME_CHANNELS 4u
#define RTHOME_NO_CHANNEL 0xFFFFFFFFu

/* rthome.phase */
#define RTHOME_IDLE 0u      /* no stand-in; the combo's hold is timed */
#define RTHOME_PRESSING 1u  /* the stand-in's HOME is held down */
#define RTHOME_WAITING 2u   /* HOME let go; the game has yet to open its menu */
#define RTHOME_OPEN 3u      /* the HOME Menu runs (HBMCalc seen lately) */
#define RTHOME_DONE 4u      /* the stand-in is gone; the combo must be let go before the next */

/* All times are the time base's low word. */
struct rthome {
    uint32_t hold;      /* ticks the combo is held (loader-filled) */
    uint32_t press;     /* ticks HOME is held on the stand-in (loader-filled) */
    uint32_t wait;      /* ticks the menu may take to open after the press, else the game refused (loader-filled) */
    uint32_t linger;    /* ticks after HBMCalc's last call that the menu counts as closed (loader-filled) */
    uint32_t phase;     /* RTHOME_* */
    uint32_t since;     /* when the hold began, or the phase (low bit set), 0: not held */
    uint32_t menu_seen; /* HBMCalc's last call (low bit set), 0: none since the press */
    uint32_t probed;    /* channels the game's WPADProbe has asked about, one bit each */
    uint32_t free;      /* of those, the ones it last found empty */
    uint32_t channel;   /* the stand-in's, RTHOME_NO_CHANNEL: none */
    uint32_t port;      /* the GameCube port that held the combo */
    uint32_t stale;     /* PADStatus buttons held at the press, ignored until let go */
    uint32_t last;      /* the Classic buttons of the sample before (for trig and release) */
    uint8_t pad[12];    /* that port's PADStatus as the last PADRead had it */
    /* The Classic buttons are worked out once per PADRead sample: a game
     * may read the channel twice a frame (Naruto: Clash of Ninja Revolution
     * 3), and a second working-out would find nothing newly pressed, so the
     * HOME Menu, which acts on newly pressed buttons, never saw them. */
    uint32_t samples;   /* PADRead samples kept since the press */
    uint32_t worked;    /* the sample the words below are from */
    uint32_t hold_now, trig_now, release_now;
};

/* At each PADRead, the four ports' statuses (12 bytes each) as the game
 * will see them: the combo held `hold` ticks on a port starts the stand-in
 * on the lowest channel the game found empty, else one it never asked
 * about (channel 0 when it has asked about none yet); none at all when
 * every channel has a controller, whose own HOME button works. While the
 * stand-in is up, that port's status is kept for it. Letting the combo go
 * after the stand-in is gone arms it again. */
void rthome_pads(struct rthome* h, const uint8_t* status, uint32_t now);

/* WPADProbe(`channel`) answered `result` (0 a controller, -1 none, -2
 * busy), not for the stand-in. */
void rthome_probed(struct rthome* h, uint32_t channel, int32_t result);

/* HBMCalc ran: the HOME Menu is open. */
void rthome_menu_frame(struct rthome* h, uint32_t now);

/* The stand-in's channel at `now`, or RTHOME_NO_CHANNEL. Moves the phases
 * on with the time: the press ends, a menu that never opened (the game
 * refused) or has closed ends the stand-in. */
uint32_t rthome_stand_in(struct rthome* h, uint32_t now);

/* Whether the menu counts as open (for the status's data format: 8, with
 * the pointer, while it is, as KPAD reports it then; 7 otherwise). */
int rthome_menu_open(const struct rthome* h);

/* The stand-in's Classic Controller at `now`, worked out once per PADRead
 * sample and the same for every read until the next: HOME alone while pressing;
 * then the kept port's buttons mapped (A, B, X, Y as themselves, Start as
 * HOME to close the menu, Z as ZR, L and R as themselves, the D-pad as the
 * D-pad), with the combo's buttons ignored until each is let go. Its
 * hold, trig (newly pressed) and release words, and the left stick (the
 * control stick), the right stick (the C-stick) and the two triggers as
 * fractions: `axes` gets numerators over `RTHOME_AXIS_FULL`, a dead zone
 * at rest. */
#define RTHOME_AXIS_FULL 256
void rthome_classic(struct rthome* h, uint32_t now, uint32_t* hold, uint32_t* trig, uint32_t* release,
                    int32_t axes[6]);

/* An IEEE single's bits for `numerator` / RTHOME_AXIS_FULL (from -FULL to
 * FULL), made with integers only: the blob has no floating-point unit use
 * and no constant data. */
uint32_t rthome_float_bits(int32_t numerator);

#endif
