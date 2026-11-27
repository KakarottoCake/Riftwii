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
#define RTSLOT_RETELLS 30u
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
    /* The HOME Menu's stand-in's slot (one bit) when the game knew it as the
     * permanent stand-in's as the HOME stand-in took it, 0: a Classic one. */
    uint32_t home_shaped;
    /* Telling a game again that may hear "connected" before its own
     * callback is set (Punch-Out!!): a slot told and not read since is told
     * again every retell_ticks (loader-filled, 0: never), at most
     * RTSLOT_RETELLS times while its controller stays. */
    uint32_t retell_ticks;
    uint32_t heard;                  /* slots the game read since they were told, one bit each */
    uint32_t told_at[RTSLOT_SLOTS];  /* when each was told (low bit set), 0: not noted yet */
    uint32_t retells[RTSLOT_SLOTS];  /* times told again since its controller came */
    uint32_t retell;                 /* slots due to be told "connected" again; `told` stays set meanwhile */
    /* When the game last read a Wii controller through a hook (low bit set),
     * 0: never; when the first sleep that asked was; quiet already reached
     * (kept until the next read, so the time base wrapping round can't make
     * an old time look recent). */
    uint32_t wii_read, quiet_from;
    uint32_t quiet_reached;
    uint32_t probe_counts[RTSLOT_SLOTS];  /* answers each slot has had (rtslot_probe_count) */
    /* Slots whose "connected" being told now is a retell: the first one was
     * delivered, so a retell the game's callback can't take yet leaves the
     * slot told (and the retell due), not unannounced (CodeRabbit CLI). */
    uint32_t retelling;
    /* "Connected only" (a pack does the controls: it reads the GameCube
     * ports itself): the stand-in's slots are connected and told as usual,
     * but a controller found there (the pack answering WPADProbe or filling
     * KPAD) is never a real Wii Remote taking the slot back. The other
     * layers keep the input at rest (rtgcwii_pads) and leave the pack's
     * samples to the game (pad_hook). Loader-filled. */
    uint32_t inputless;
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

/* The game's own WPADRead of `slot` answered `error` (0: a report, -1: no
 * controller), its report blank (all zeros) or not. Returns 1 when a real
 * Wii Remote holds the slot by that answer (its report stays). */
int rtslot_wpad_read(struct rtslot* s, uint32_t slot, int32_t error, int blank);

/* Whether the real controllers' slots are due a check of their own (the
 * game's WPADProbe): some games stop reading a slot once their library
 * reports it gone, so its hooked reads and probes never tell the stand-in
 * the Wii Remote turned off. Every 250 ms while a slot is real; `now` is
 * noted when it is due. */
int rtslot_real_check_due(struct rtslot* s, uint32_t now, uint32_t ticks_per_ms);

/* That recheck runs the game's WPADProbe, with interrupts as the hook found
 * them (on at a read, off at a sleep; WPADProbe turns them off itself
 * around the channel's state): begin notes where the slot's answers stand,
 * and its answer counts only if no newer one came meanwhile (CodeRabbit on
 * #610). */
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
 * never told "gone" while it is up, and where it holds the slot in another
 * shape than the permanent stand-in's (`remote_shapes`: the Wii Remote
 * forms, whose HOME stand-in may be a Classic Controller), the permanent
 * stand-in's "connected" there waits until it ends (CodeRabbit on #610 and
 * #616). `skip`: slots passed over this pass. */
int rtslot_next_tell(struct rtslot* s, uint32_t home, int remote_shapes, uint32_t skip, uint32_t* slot,
                     int32_t* reason);

/* After the hook tried to tell `slot` `reason` (`delivered`: the game had a
 * callback to take it): what comes next. Not delivered: it stays to be told
 * at the next pass. Delivered "connected" on a slot still announced as the
 * stand-in's: the attachment is told too. Delivered "gone" on a slot a real
 * controller took meanwhile: "connected" again, so the game hears last that
 * a controller is there. */
enum rtslot_after { RTSLOT_AFTER_NOTHING, RTSLOT_AFTER_EXTENSION, RTSLOT_AFTER_CONNECTED };
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
int rtslot_tell_busy(const struct rtslot* s);
void rtslot_tell_take(struct rtslot* s);
void rtslot_tell_give(struct rtslot* s);

/* The game read the stand-in on `slot` (a KPAD read or WPADRead gave it): it
 * has heard of it, and isn't told again. */
void rtslot_heard(struct rtslot* s, uint32_t slot);

/* At each PADRead: a slot told and not read since, retell_ticks after the
 * telling, is due to be told "connected" again (at most RTSLOT_RETELLS
 * times while its controller stays); it still counts as told. */
void rtslot_retell(struct rtslot* s, uint32_t now);

/* --- The HOME Menu's stand-in. */

/* The slots the game knows as the permanent stand-in's: held now, told of
 * and not yet told gone, and the HOME stand-in's own when it took one of
 * these. */
uint32_t rtslot_shaped(const struct rtslot* s);

/* The HOME Menu's stand-in takes `slot` (RTSLOT_SLOTS or more: none): the
 * shape it takes it in (the permanent stand-in's, or a Classic
 * Controller's) is kept until it next takes one, whatever is plugged in or
 * unplugged meanwhile (Cake's review of #610); rtslot_home_keeps_shape:
 * whether it has the permanent stand-in's shape on `slot`. */
void rtslot_home_took(struct rtslot* s, uint32_t slot);
int rtslot_home_keeps_shape(const struct rtslot* s, uint32_t slot);

/* The slot the game should hear the HOME Menu's stand-in on (`home`, or
 * RTSLOT_NO_SLOT): not one the permanent stand-in holds or has told the
 * game of, which the game has already. In the Wii Remote forms
 * (`remote_shapes`) a HOME stand-in that took a free slot in its own
 * Classic shape keeps that claim while it lasts, though a GameCube
 * controller plugs in there: the game hears the Classic one gone only when
 * the HOME Menu ends (CodeRabbit on #610). Nor one a real Wii Remote
 * holds, whose library tells the game of it (CodeRabbit, fork #22). */
uint32_t rtslot_home_announced(const struct rtslot* s, uint32_t home, int remote_shapes);

/* The HOME Menu's stand-in leaving `slot`: "gone" is told unless a real
 * controller is there now (CodeRabbit on #610), or the permanent stand-in
 * has told the game it's connected (a GameCube controller there while the
 * HOME Menu was up: its own "gone" comes when that one leaves; CodeRabbit,
 * fork #22). */
int rtslot_home_may_leave(const struct rtslot* s, uint32_t slot);

/* --- The quiet rule (later-WPAD games' sleeps). */

/* The game read a Wii controller through a hook at `now`. */
void rtslot_wii_read(struct rtslot* s, uint32_t now);

/* Whether a sleep should read the pads in a game whose connect callback is
 * the later WPAD's: only while the game itself is quiet, so one that reads
 * nothing until its callback hears of a controller (Mario Strikers
 * Charged) gets the read that announces the stand-in, and one that reads
 * WPAD every frame (LEGO Star Wars: The Complete Saga) never does. Quiet:
 * no Wii controller read in 3 s since its last, or none at all 5 s after
 * the first sleep that asked. */
int rtslot_quiet_due(struct rtslot* s, uint32_t now, uint32_t ticks_per_ms);

#endif
