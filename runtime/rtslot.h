/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Slot ownership, the third of the stand-in's four layers (sources, shapes,
 * slot ownership, delivery; notes in #587): for each Wii Remote slot (WPAD's
 * "channel"), whether a real controller, the HOME Menu's stand-in, the
 * permanent stand-in or nobody holds it, and what the game's connect
 * callback has been told. The hooks report what they saw; only this module
 * decides. Free of the hardware, so the host tests run it. */
#ifndef RIFTWII_RTSLOT_H
#define RIFTWII_RTSLOT_H

#include <stdint.h>

#define RTSLOT_SLOTS 4u
#define RTSLOT_NO_SLOT 0xFFFFFFFFu
#define RTSLOT_NO_ERROR 0x7FFFFFFF
#define RTSLOT_EMPTY_READS 30

struct rtslot {
    uint32_t grace;    /* ticks a port may report no controller before its slot's goes (loader-filled) */
    uint32_t present;  /* slots with a GameCube controller behind them, one bit each */
    uint32_t real;     /* slots where a real controller was last found: the stand-in steps aside */
    uint32_t told;     /* slots the game's connect callback last heard are connected (by the stand-in) */
    uint32_t telling;  /* tellings of the game's connect callback under way (PADReads meanwhile leave it to them) */
    /* A real controller's KPAD reads with no error output that brought no
     * samples: since when (low bit set; 0: the last read brought some), how
     * many in a row, and the last one (a gap past `grace` starts over). */
    uint32_t empty_since[RTSLOT_SLOTS];
    uint32_t empty_reads[RTSLOT_SLOTS];
    uint32_t empty_last[RTSLOT_SLOTS];
    uint32_t real_checked;  /* when the real controllers' slots were last rechecked (low bit set), 0: never */
    uint32_t probe_counts[RTSLOT_SLOTS];  /* answers each slot has had (rtslot_probe_count) */
};

/* --- Who holds a slot. */

/* Decided in one place for every hook: a real controller first (its samples
 * are the game's, the HOME Menu's stand-in included: CodeRabbit on #609),
 * then the HOME Menu's stand-in on its own slot (`home`: rthome_stand_in's
 * slot, RTSLOT_NO_SLOT for none), then the permanent stand-in where a
 * controller is plugged in, else nobody. */
enum rtslot_owner { RTSLOT_NOBODY, RTSLOT_REAL, RTSLOT_HOME, RTSLOT_STANDIN };
enum rtslot_owner rtslot_owner(const struct rtslot* s, uint32_t slot, uint32_t home);

/* The slots the permanent stand-in holds (plugged in, no real controller),
 * one bit each; rtslot_on: whether it holds `slot`. */
uint32_t rtslot_standin_slots(const struct rtslot* s);
int rtslot_on(const struct rtslot* s, uint32_t slot);

/* --- A real controller's evidence. */

/* WPADProbe(`slot`) answered `result` (0 a controller, -1 none, -2 busy):
 * a real controller there wins over the stand-in. When it goes again, the
 * stand-in is announced afresh (the game's library told the game that
 * slot's controller left). */
void rtslot_probed(struct rtslot* s, uint32_t slot, int32_t result);

/* How many answers `slot` has had (rtslot_probed counts them): taken
 * before a probe whose answer is applied later, so a newer answer in
 * between isn't undone by the older one. */
uint32_t rtslot_probe_count(const struct rtslot* s, uint32_t slot);

/* A KPAD read on `slot` returned `result` samples, `error` its WPAD error
 * where the read gives one (RTSLOT_NO_ERROR otherwise): samples mean a real
 * controller is there; "no controller" (-1) means it left. A read with no
 * error output can't say so: there, a real controller whose reads bring no
 * samples for `grace` ticks and RTSLOT_EMPTY_READS reads in a row has left
 * too (a game that stops calling WPADProbe would otherwise keep the stand-in
 * away). */
void rtslot_kpad_read(struct rtslot* s, uint32_t slot, int32_t result, int32_t error, uint32_t now);

/* Whether the real controllers' slots are due a check of their own (the
 * game's WPADProbe): some games stop reading a slot once their library
 * reports it gone, so its hooked reads and probes never tell the stand-in
 * the Wii Remote turned off. Every 250 ms while a slot is real; `now` is
 * noted when it is due. */
int rtslot_real_check_due(struct rtslot* s, uint32_t now, uint32_t ticks_per_ms);

/* That recheck runs the game's WPADProbe with interrupts on: begin notes
 * where the slot's answers stand, and its answer counts only if no newer one
 * came meanwhile (CodeRabbit on #610). */
uint32_t rtslot_recheck_begin(const struct rtslot* s, uint32_t slot);
void rtslot_rechecked(struct rtslot* s, uint32_t slot, int32_t result, uint32_t begun);

/* --- Telling the game. */

/* The next change the game's connect callback should hear, one at a time
 * from what is true now (so a PADRead nested in another, from an SI
 * callback, can't leave a stale "gone" last): 1 with `slot` and `reason` (0
 * connected, -1 gone), noted as told; 0 when there is none. Slots in `skip`
 * are passed over this time; slots in `keep` are never reported gone. */
int rtslot_next_change(struct rtslot* s, uint32_t skip, uint32_t keep, uint32_t* slot, int32_t* reason);

/* rtslot_next_change with the HOME Menu's rule applied: its slot (`home`) is
 * never told "gone" while it is up (its own end tells the game). `skip`:
 * slots passed over this pass. */
int rtslot_next_tell(struct rtslot* s, uint32_t home, uint32_t skip, uint32_t* slot, int32_t* reason);

/* After the hook tried to tell `slot` `reason` (`delivered`: the game had a
 * callback to take it): what comes next. Not delivered: it stays to be told
 * at the next pass. Delivered "gone" on a slot a real controller took
 * meanwhile: "connected" again, so the game hears last that a controller is
 * there. */
enum rtslot_after { RTSLOT_AFTER_NOTHING, RTSLOT_AFTER_CONNECTED };
enum rtslot_after rtslot_told(struct rtslot* s, uint32_t slot, int32_t reason, int delivered);

/* A corrective "connected" (rtslot_told's RTSLOT_AFTER_CONNECTED) was just
 * delivered for `slot`: if the real controller has left meanwhile, the slot
 * counts as told, so the next pass tells the game "gone" (the last word it
 * heard is then the slot's real state; CodeRabbit, fork #22). */
void rtslot_corrected(struct rtslot* s, uint32_t slot);

/* The callback couldn't take a "connected" (rtslot_not_told: told again
 * next time) or a "gone" (rtslot_still_told: it counts as told connected
 * until then). */
void rtslot_not_told(struct rtslot* s, uint32_t slot);
void rtslot_still_told(struct rtslot* s, uint32_t slot);

/* All changes at once, for a caller that tells them together: the slots to
 * report gone (*leave) and connected (*join), one bit each, noted as told.
 * A slot a real controller took is neither: the game's library tells it. */
void rtslot_connect_change(struct rtslot* s, uint32_t* leave, uint32_t* join);

/* One telling at a time: the HOME Menu's news and the permanent stand-in's
 * (a PADRead nested in the game's callback leaves its own telling to the
 * one running). A count, not a flag: two threads telling at once each take
 * off only their own share. */
/* Whether the HOME Menu's stand-in, ending, may tell the game "gone" for
 * `slot`: not while a real Wii Remote holds it (WPAD tells the game when
 * that one leaves), nor once the permanent stand-in has told the game it's
 * connected (a GameCube controller there while the HOME Menu was up: its
 * own "gone" comes when that one leaves; CodeRabbit, fork #22). */
int rtslot_home_may_leave(const struct rtslot* s, uint32_t slot);
int rtslot_tell_busy(const struct rtslot* s);
void rtslot_tell_take(struct rtslot* s);
void rtslot_tell_give(struct rtslot* s);

/* --- The HOME Menu's stand-in. */

/* The slot the game should hear the HOME Menu's stand-in on (`home`, or
 * RTSLOT_NO_SLOT): not one the permanent stand-in holds or has told the
 * game of (its controller may have just been unplugged), which the game has
 * already; the permanent stand-in's telling covers its end. Nor one a real
 * Wii Remote holds, whose library tells the game of it (CodeRabbit, fork
 * #22). */
uint32_t rtslot_home_announced(const struct rtslot* s, uint32_t home);

#endif
