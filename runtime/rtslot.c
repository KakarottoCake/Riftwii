/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Who holds each Wii Remote slot, and what the game has been told
 * (rtslot.h): the stand-ins' one place for those decisions. */
#include "rtslot.h"

/* Ticks since a time noted with its low bit set (so 0 can mean "none"). */
static uint32_t rtslot_elapsed(uint32_t now, uint32_t noted) { return now - (noted & ~1u); }

uint32_t rtslot_probe_count(const struct rtslot* s, uint32_t channel) {
    return channel < RTSLOT_SLOTS ? s->probe_counts[channel] : 0u;
}

void rtslot_probed(struct rtslot* s, uint32_t channel, int32_t result) {
    uint32_t bit;
    if (channel >= RTSLOT_SLOTS || result == -2) return;  /* busy: no answer either way */
    ++s->probe_counts[channel];
    bit = 1u << channel;
    if (result == 0) {
        s->real |= bit;
        /* A controller there now: any count of empty reads starts over. */
        s->empty_since[channel] = 0;
        s->empty_reads[channel] = 0;
    } else if (s->real & bit) {
        /* The real controller left, and the game's library told the game
         * so: the stand-in is announced again. */
        s->real &= ~bit;
        s->told &= ~bit;
        s->empty_since[channel] = 0;
        s->empty_reads[channel] = 0;
    }
}

int rtslot_on(const struct rtslot* s, uint32_t channel) {
    if (channel >= RTSLOT_SLOTS) return 0;
    return ((s->present & ~s->real) >> channel) & 1u;
}

uint32_t rtslot_shaped(const struct rtslot* s) { return (s->present & ~s->real) | s->told | s->home_shaped; }

void rtslot_home_took(struct rtslot* s, uint32_t channel) {
    s->home_shaped = 0;
    if (channel < RTSLOT_SLOTS) s->home_shaped = rtslot_shaped(s) & (1u << channel);
}

int rtslot_home_keeps_shape(const struct rtslot* s, uint32_t channel) {
    return channel < RTSLOT_SLOTS && ((s->home_shaped >> channel) & 1u);
}

void rtslot_connect_change(struct rtslot* s, uint32_t* leave, uint32_t* join) {
    const uint32_t now_on = s->present & ~s->real;
    *join = now_on & ~s->told;
    /* Only channels it was told about and no real controller holds. */
    *leave = s->told & ~now_on & ~s->real;
    s->told = (s->told | *join) & ~*leave;
}

void rtslot_wii_read(struct rtslot* s, uint32_t now) {
    s->wii_read = now | 1u;
    s->quiet_reached = 0;
}

int rtslot_quiet_due(struct rtslot* s, uint32_t now, uint32_t ticks_per_ms) {
    if (s->quiet_reached) return 1;
    if (s->wii_read != 0) {
        s->quiet_reached = rtslot_elapsed(now, s->wii_read) >= 3000u * ticks_per_ms;
        return (int)s->quiet_reached;
    }
    if (s->quiet_from == 0) {
        s->quiet_from = now | 1u;
        return 0;
    }
    s->quiet_reached = rtslot_elapsed(now, s->quiet_from) >= 5000u * ticks_per_ms;
    return (int)s->quiet_reached;
}

int rtslot_real_check_due(struct rtslot* s, uint32_t now, uint32_t ticks_per_ms) {
    if (s->real == 0) return 0;
    if (s->real_checked != 0 && rtslot_elapsed(now, s->real_checked) < 250u * ticks_per_ms) return 0;
    s->real_checked = now | 1u;
    return 1;
}

int rtslot_next_change(struct rtslot* s, uint32_t skip, uint32_t keep, uint32_t* channel, int32_t* reason) {
    uint32_t leave = 0, join = 0, i;
    const uint32_t now_on = s->present & ~s->real;
    /* A channel due to be told again is told "connected" once more; one
     * that left before that is told it's gone (it still counts as told). */
    /* A retell the game has heard since it came due (a read in between)
     * is dropped, not told (CodeRabbit, fork #24); a first "connected"
     * never waits on `heard`. */
    join = now_on & (~s->told | (s->retell & ~s->heard)) & ~skip;
    s->retell &= ~s->heard;
    leave = s->told & ~now_on & ~s->real & ~skip & ~keep;
    s->retell &= now_on;
    for (i = 0; i < RTSLOT_SLOTS; ++i) {
        const uint32_t bit = 1u << i;
        if (!((join | leave) & bit)) continue;
        *channel = i;
        *reason = (join & bit) ? 0 : -1;
        if (join & bit) {
            if ((s->told & bit) && (s->retell & bit)) {
                s->retelling |= bit;
            } else {
                s->retelling &= ~bit;
            }
            s->told |= bit;
            s->retell &= ~bit;
            /* A new "connected": only a read after it says the game heard
             * it (a WPADRead before the callback was told doesn't). */
            s->heard &= ~bit;
        } else {
            s->told &= ~bit;
        }
        return 1;
    }
    return 0;
}

void rtslot_kpad_read(struct rtslot* s, uint32_t channel, int32_t result, int32_t error, uint32_t now) {
    uint32_t bit;
    if (channel >= RTSLOT_SLOTS) return;
    bit = 1u << channel;
    if (result > 0) {
        s->empty_since[channel] = 0;
        s->empty_reads[channel] = 0;
        rtslot_probed(s, channel, 0);
    } else if (error == -1) {
        s->empty_since[channel] = 0;
        s->empty_reads[channel] = 0;
        rtslot_probed(s, channel, -1);
    } else if (error == RTSLOT_NO_ERROR && (s->real & bit)) {
        /* A read with no error output can't say "no controller": a real
         * controller that brings no samples for a while (and many reads in
         * a row, so a pause in the game's reading doesn't count) has left. */
        /* Reads that keep coming: a pause longer than `grace` between two
         * empty ones starts the count over (a burst, a pause and one more
         * read isn't a controller gone; CodeRabbit on #609). */
        if (s->empty_since[channel] != 0 && now - s->empty_last[channel] > s->grace)
            s->empty_since[channel] = 0;
        /* The tick itself (valid while a run is counted, empty_since not
         * 0), and the gap from it as it is (not rtslot_elapsed, which drops
         * a tagged tick's low bit), so a gap of exactly `grace` keeps the
         * run and one tick more doesn't, from odd ticks too (CodeRabbit,
         * fork #22). */
        s->empty_last[channel] = now;
        if (s->empty_since[channel] == 0) {
            s->empty_since[channel] = now | 1u;
            s->empty_reads[channel] = 1;
        } else if (++s->empty_reads[channel] >= RTSLOT_EMPTY_READS &&
                   rtslot_elapsed(now, s->empty_since[channel]) >= s->grace) {
            s->empty_since[channel] = 0;
            s->empty_reads[channel] = 0;
            rtslot_probed(s, channel, -1);
        }
    }
}

void rtslot_still_told(struct rtslot* s, uint32_t channel) {
    if (channel < RTSLOT_SLOTS) s->told |= 1u << channel;
}

void rtslot_not_told(struct rtslot* s, uint32_t channel) {
    if (channel < RTSLOT_SLOTS) s->told &= ~(1u << channel);
}

void rtslot_heard(struct rtslot* s, uint32_t channel) {
    if (channel < RTSLOT_SLOTS) s->heard |= 1u << channel;
}

void rtslot_retell(struct rtslot* s, uint32_t now) {
    uint32_t i;
    if (s->retell_ticks == 0) return;
    for (i = 0; i < RTSLOT_SLOTS; ++i) {
        const uint32_t bit = 1u << i;
        if (!rtslot_on(s, i)) {
            /* Gone (or a real controller's): the next controller starts afresh. */
            s->heard &= ~bit;
            s->retell &= ~bit;
            s->told_at[i] = 0;
            s->retells[i] = 0;
            continue;
        }
        if (!(s->told & bit) || (s->heard & bit) || (s->retell & bit)) continue;  /* still to tell, heard, or due */
        if (s->told_at[i] == 0) {
            s->told_at[i] = now | 1u;
            continue;
        }
        if (rtslot_elapsed(now, s->told_at[i]) < s->retell_ticks || s->retells[i] >= RTSLOT_RETELLS) continue;
        s->retell |= bit;
        s->told_at[i] = 0;
        ++s->retells[i];
    }
}

enum rtslot_owner rtslot_owner(const struct rtslot* s, uint32_t slot, uint32_t home) {
    uint32_t bit;
    if (slot >= RTSLOT_SLOTS) return RTSLOT_NOBODY;
    bit = 1u << slot;
    if (s->real & bit) return RTSLOT_REAL;
    if (slot == home) return RTSLOT_HOME;
    if (s->present & bit) return RTSLOT_STANDIN;
    return RTSLOT_NOBODY;
}

uint32_t rtslot_standin_slots(const struct rtslot* s) { return s->present & ~s->real; }

int rtslot_next_tell(struct rtslot* s, uint32_t home, int remote_shapes, uint32_t skip, uint32_t* slot,
                     int32_t* reason) {
    const uint32_t keep = home < RTSLOT_SLOTS ? 1u << home : 0u;
    /* A HOME Menu stand-in holding the slot in another shape: the permanent
     * stand-in's "connected" there waits until it ends (the "gone" for the
     * HOME stand-in's own shape is told first, by its end). */
    const uint32_t held = remote_shapes && keep != 0 && !rtslot_home_keeps_shape(s, home) ? keep : 0u;
    return rtslot_next_change(s, skip | held, keep, slot, reason);
}

enum rtslot_after rtslot_told(struct rtslot* s, uint32_t slot, int32_t reason, int delivered) {
    const uint32_t bit = slot < RTSLOT_SLOTS ? 1u << slot : 0u;
    int retold;
    if (slot >= RTSLOT_SLOTS) return RTSLOT_AFTER_NOTHING;
    retold = reason == 0 && (s->retelling & bit) != 0;
    s->retelling &= ~bit;
    if (!delivered) {
        /* Still to tell, at the next pass. A retell keeps the first,
         * delivered "connected" (a "gone" is still owed if the controller
         * leaves) and stays due. */
        if (retold) {
            s->retell |= bit;
        } else if (reason == 0) {
            rtslot_not_told(s, slot);
        } else {
            rtslot_still_told(s, slot);
        }
        return RTSLOT_AFTER_NOTHING;
    }
    /* The attachment only for an announcement that still stands: a probe
     * during the callback may have found a real Wii Remote, and a later
     * one its leaving, which unannounces the slot (CodeRabbit CLI). */
    if (reason == 0) return rtslot_on(s, slot) && (s->told & bit) ? RTSLOT_AFTER_EXTENSION : RTSLOT_AFTER_NOTHING;
    /* A real controller that came while "gone" was told: its own "connected"
     * may have come first, so the game hears last that one is there. */
    if ((s->real >> slot) & 1u) return RTSLOT_AFTER_CONNECTED;
    return RTSLOT_AFTER_NOTHING;
}

int rtslot_wpad_read(struct rtslot* s, uint32_t slot, int32_t error, int blank) {
    if (slot >= RTSLOT_SLOTS) return 0;
    if (error == 0) {
        /* A real Wii Remote's report: the stand-in steps aside, as when
         * WPADProbe finds one. Not a blank report on a slot no real one is
         * known on: a WPAD whose slot never had one copies its untouched
         * entry, error 0 (Mario Strikers Charged), and no real Wii Remote's
         * report is blank (its accelerometer never reads 0, 0, 0). */
        if (!blank || ((s->real >> slot) & 1u)) {
            rtslot_probed(s, slot, 0);
            return 1;
        }
    } else if (error == -1) {
        /* "No controller": a real Wii Remote that was there has left, which
         * a game reading only WPAD learns no other way; the stand-in can
         * come back. */
        rtslot_probed(s, slot, -1);
    }
    return 0;
}

uint32_t rtslot_recheck_begin(const struct rtslot* s, uint32_t slot) { return rtslot_probe_count(s, slot); }

void rtslot_rechecked(struct rtslot* s, uint32_t slot, int32_t result, uint32_t begun) {
    if (rtslot_probe_count(s, slot) == begun) rtslot_probed(s, slot, result);
}

uint32_t rtslot_home_announced(const struct rtslot* s, uint32_t home, int remote_shapes) {
    uint32_t covered;
    if (home >= RTSLOT_SLOTS) return RTSLOT_NO_SLOT;
    covered = ((rtslot_standin_slots(s) | s->told) >> home) & 1u;
    /* A real Wii Remote there: its library tells the game of it
     * (CodeRabbit, fork #22). */
    if ((s->real >> home) & 1u) return RTSLOT_NO_SLOT;
    if (covered && (!remote_shapes || rtslot_home_keeps_shape(s, home))) return RTSLOT_NO_SLOT;
    return home;
}

int rtslot_home_may_leave(const struct rtslot* s, uint32_t slot) {
    return slot >= RTSLOT_SLOTS || !(((s->real | s->told) >> slot) & 1u);
}

int rtslot_tell_busy(const struct rtslot* s) { return s->telling != 0; }
void rtslot_tell_take(struct rtslot* s) { s->telling += 1; }
void rtslot_tell_give(struct rtslot* s) { s->telling -= 1; }

void rtslot_corrected(struct rtslot* s, uint32_t slot) {
    if (slot >= RTSLOT_SLOTS) return;
    /* The game's last word for the slot is now "connected": with the real
     * controller gone, that's told, so "gone" follows if nothing stands in
     * there by the next pass, and nothing does if a GameCube controller is
     * there (CodeRabbit CLI). */
    if (!((s->real >> slot) & 1u)) s->told |= 1u << slot;
}
