#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# The "Would You Look at the Time" easter-egg theme's song: an original,
# driving minor-key rock loop for two distorted rhythm guitars, a lead
# guitar, a bass guitar and a drum kit. Everything here (the riffs, the
# hook, the bass line, the drum parts) is written for RiftWii; nothing is
# sampled or transcribed. The guitars are plucked strings (Karplus-Strong)
# through a distortion and a speaker cabinet's filter.
#   timesong.py <out.wav>     then: ffmpeg -i out.wav -c:a libvorbis -q:a 4 music.ogg
import sys
import wave

import numpy as np
from scipy.signal import lfilter

SR = 44100
BPM = 112
S16 = 60.0 / BPM / 4          # one sixteenth, seconds
BARS = 32
N = int(round(BARS * 16 * S16 * SR))
rng = np.random.default_rng(2610)

L = np.zeros(N)
R = np.zeros(N)


def at(step):
    return int(round(step * S16 * SR))


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    return lfilter([1 - a], [1, -a], x)


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def add(sig, step, pan=0.0, gain=1.0):
    s = at(step)
    e = min(N, s + len(sig))
    if e <= s:
        return
    g = sig[: e - s] * gain
    L[s:e] += g * np.sqrt(0.5 * (1 - pan))
    R[s:e] += g * np.sqrt(0.5 * (1 + pan))


def fade(n, attack=0.002, release=0.015):
    out = np.ones(n)
    a, r = int(attack * SR), int(release * SR)
    if a:
        out[:a] = np.linspace(0, 1, a)
    if r and r < n:
        out[-r:] *= np.linspace(1, 0, r)
    return out


# --- the instruments --------------------------------------------------------
def pluck(f, n, decay=0.996, bright=0.6):
    """A plucked string at exactly `f` Hz, `n` samples long."""
    period = max(2, int(round(SR / f)))
    local = period * f                      # the rate at which the loop is in tune
    m = int(n * local / SR) + 2
    x = np.zeros(m)
    burst = rng.uniform(-1, 1, period)
    x[:period] = burst * bright + lowpass(burst, 1500) * (1 - bright)
    den = np.zeros(period + 2)
    den[0] = 1
    den[period] = den[period + 1] = -0.5 * decay
    y = lfilter([1.0], den, x)
    return np.interp(np.arange(n) * local / SR, np.arange(m), y)


def cabinet(x, drive, top=4800):
    """Distortion, then a 4x12's filter: no lows below 90 Hz, a mid push, no fizz."""
    y = np.tanh(highpass(x, 120) * drive)
    y = highpass(y, 90)
    y = lowpass(lowpass(y, top), top * 1.4)
    return y + 0.35 * (lowpass(y, 1600) - lowpass(y, 500))


def power_chord(root, n, muted=False):
    """Root, fifth and octave, picked together, distorted together."""
    decay = 0.94 if muted else 0.9975
    x = np.zeros(n)
    for k, interval in enumerate((0, 7, 12)):
        lag = int(0.004 * SR * k)            # a downstroke: low string first
        s = pluck(hz(root + interval), n - lag, decay, 0.35 if muted else 0.6)
        x[lag:] += s
    y = cabinet(x, 9.0 if muted else 7.0, 2600 if muted else 4800)
    return y * fade(n, 0.001, 0.01)


def lead_note(m, n, bend_from=None):
    """The lead guitar: one string, driven hard, a bend in and a vibrato."""
    f = hz(m)
    raw = pluck(f, int(n * 1.06) + 64, 0.9993, 0.7)
    t = np.arange(n) / SR
    rate = np.ones(n)
    if bend_from is not None:
        rate *= 2 ** ((bend_from - m) / 12 * np.exp(-t / 0.035))
    rate *= 1 + 0.012 * np.sin(2 * np.pi * 6.2 * t) * np.clip(t * 5 - 0.6, 0, 1)
    pos = np.cumsum(rate)
    y = np.interp(pos, np.arange(len(raw)), raw)
    return cabinet(y, 14.0, 5200) * fade(n, 0.002, 0.03)


def bass_note(m, n, muted=False):
    x = pluck(hz(m), n, 0.995 if not muted else 0.97, 0.25)
    y = np.tanh(lowpass(x, 1800) * 2.2)
    return (y + 0.4 * lowpass(y, 220)) * fade(n, 0.002, 0.01)


# --- the drums ----------------------------------------------------------------
def kick_sound():
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    f = 48 + 120 * np.exp(-t * 35)
    k = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 8)
    click = rng.uniform(-1, 1, n) * np.exp(-t * 400)
    return np.tanh((k + 0.35 * highpass(click, 2000)) * 1.5)


def snare_sound():
    n = int(0.32 * SR)
    t = np.arange(n) / SR
    body = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 22) + 0.5 * np.sin(2 * np.pi * 330 * t) * np.exp(-t * 30)
    wires = highpass(rng.uniform(-1, 1, n), 1800) * np.exp(-t * 13)
    return np.tanh((0.8 * body + wires) * 1.3)


def cymbal(n_sec, decay, cut):
    n = int(n_sec * SR)
    t = np.arange(n) / SR
    x = rng.uniform(-1, 1, n)
    # Metallic: a few square partials ring under the noise.
    for f in (3150, 4370, 5480, 6900):
        x += 0.25 * np.sign(np.sin(2 * np.pi * f * t))
    return highpass(x, cut) * np.exp(-t * decay)


def tom_sound(f0):
    n = int(0.4 * SR)
    t = np.arange(n) / SR
    return np.sin(2 * np.pi * np.cumsum(f0 * (1 + 0.6 * np.exp(-t * 25))) / SR) * np.exp(-t * 9)


KICK, SNARE = kick_sound(), snare_sound()
HAT, OHAT = cymbal(0.06, 70, 7000), cymbal(0.3, 10, 6000)
RIDE, CRASH = cymbal(0.6, 6, 4500), cymbal(2.0, 2.2, 3500)

# --- the song's plan ------------------------------------------------------------
# One chord a bar in F minor: i - VI - III - VII, and in the last chorus
# iv - VI - i - VII.
F2, Db2, Ab2, Eb2, Bb2 = 41, 37, 44, 39, 46
PROG_A = [F2, Db2, Ab2, Eb2]
PROG_B = [Bb2, Db2, F2, Eb2]
intro = range(0, 2)
verse = range(2, 8)
chorus1 = range(8, 16)
verse2 = range(16, 20)
build = range(20, 22)
chorus2 = range(22, 30)
tag = range(30, 32)
choruses = set(chorus1) | set(chorus2) | set(tag)
roots = [(PROG_B if bar in chorus2 else PROG_A)[bar % 4] for bar in range(BARS)]

# --- rhythm guitars: doubled, hard left and right -----------------------------------
for bar in range(BARS):
    r = roots[bar]
    base = bar * 16
    for pan, lag, gain in ((-0.9, 0.0, 0.16), (0.9, 0.05, 0.15)):
        if bar in intro or bar in build:
            # The hits: the chord on the one, ringing; in the build, every beat.
            if bar in intro:
                add(power_chord(r + 12, at(16) + 400), base + lag, pan, gain)
            else:
                for h in (0, 4, 8, 12):
                    add(power_chord(r + 12, at(4) + 400), base + h + lag, pan, gain)
        elif bar in choruses:
            # Open eighths, an accent on each beat.
            for k in range(0, 16, 2):
                add(power_chord(r + 12, at(2) - 60), base + k + lag, pan, gain * (1.0 if k % 4 == 0 else 0.8))
        else:
            # Verses: palm-muted sixteenths, an open chord to end every other bar.
            for k in range(16):
                if bar % 2 == 1 and k >= 12:
                    if k == 12:
                        add(power_chord(r + 12, at(4)), base + 12 + lag, pan, gain * 0.9)
                    continue
                add(power_chord(r + 12, at(1) - 30, muted=True), base + k + lag, pan, gain * (0.95 if k % 4 == 0 else 0.7))

# --- bass guitar: picked eighths on the root, a step into each bar -------------------
for bar in range(BARS):
    if bar in intro:
        add(bass_note(roots[bar], at(16)), bar * 16, 0, 0.42)
        continue
    r = roots[bar]
    nxt = roots[(bar + 1) % BARS]
    for k in range(0, 16, 2):
        m = r
        if k == 14 and nxt != r:
            m = nxt + (2 if nxt < r else -2)  # a step into the next root
        add(bass_note(m, at(2) - 80, muted=bar in verse or bar in verse2), bar * 16 + k, 0, 0.42)

# --- drums ----------------------------------------------------------------------------
for bar in range(BARS):
    base = bar * 16
    if bar in (2, 8, 16, 22, 30):
        add(CRASH, base, 0.3, 0.35)
    if bar in intro:
        if bar == 0:
            add(KICK, base, 0, 0.9)
            add(CRASH, base, -0.3, 0.4)
        else:  # the tom fill down into the beat
            for k, (step, f0) in enumerate([(8, 210), (10, 180), (11, 160), (12, 140), (13, 120), (14, 105), (15, 92)]):
                add(tom_sound(f0), base + step, 0.4 - 0.12 * k, 0.55)
            add(SNARE, base + 15, 0, 0.3)
        continue
    if bar in build:
        # A snare roll, eighths then sixteenths, louder all the way.
        first = bar == build[0]
        for k in range(0, 16, 2 if first else 1):
            add(SNARE, base + k, 0, 0.25 + 0.35 * ((bar - build[0]) * 16 + k) / 32)
            if k % 4 == 0:
                add(KICK, base + k, 0, 0.8)
        continue
    chorus = bar in choruses
    # Kick: the one, the and of two, the three; in the choruses on every beat as well.
    for k in ((0, 4, 6, 8, 12) if chorus else (0, 6, 8, 11)):
        add(KICK, base + k, 0, 0.95)
    for k in (4, 12):
        add(SNARE, base + k, 0.05, 0.75)
    if chorus:
        add(CRASH, base, 0.35, 0.2)
        for k in range(0, 16, 2):
            add(RIDE, base + k, 0.4, 0.17 if k % 4 == 0 else 0.12)
    else:
        for k in range(0, 16, 2):
            add(HAT, base + k, -0.3, 0.2 if k % 4 == 0 else 0.13)
        add(OHAT, base + 14, -0.3, 0.13)
    # A fill at the end of each section.
    if bar in (7, 15, 19, 29):
        for k, f0 in zip(range(12, 16), (180, 150, 120, 95)):
            add(tom_sound(f0), base + k, 0.3 - 0.2 * (k - 12), 0.5)

# --- lead guitar: the hook, an original line --------------------------------------------
# Short repeated stabs that come down from the chord's fifth, then climb
# back up; the second half reaches higher and lands on F.
# (sixteenth in the 8-bar phrase, length in sixteenths, MIDI note)
HOOK = [
    (0, 2, 72), (2, 2, 72), (4, 1, 72), (5, 1, 75), (6, 2, 72), (8, 2, 70), (10, 2, 68), (12, 4, 67),
    (16, 2, 68), (18, 2, 68), (20, 1, 68), (21, 1, 72), (22, 2, 68), (24, 2, 67), (26, 2, 65), (28, 4, 63),
    (32, 2, 63), (34, 2, 65), (36, 2, 68), (38, 2, 72), (40, 4, 75), (44, 2, 72), (46, 2, 70),
    (48, 6, 70), (54, 2, 67), (56, 8, 70),
    (64, 2, 72), (66, 2, 72), (68, 1, 72), (69, 1, 75), (70, 2, 72), (72, 2, 70), (74, 2, 68), (76, 4, 67),
    (80, 2, 77), (82, 2, 77), (84, 1, 77), (85, 1, 75), (86, 2, 72), (88, 4, 73), (92, 4, 72),
    (96, 2, 72), (98, 2, 70), (100, 2, 68), (102, 2, 70), (104, 4, 72), (108, 4, 75),
    (112, 4, 70), (116, 4, 67), (120, 8, 65),
]
lead = np.zeros(N)


def play_hook(start_bar, shift=0, gain=1.0, phrase=HOOK):
    prev = None
    for s16, l16, m in phrase:
        n = at(l16) + int(0.03 * SR)
        bend = prev + shift if prev is not None and abs(prev - m) <= 2 and prev != m else None
        y = lead_note(m + shift, n, bend)
        s = at(start_bar * 16 + s16)
        e = min(N, s + n)
        lead[s:e] += y[: e - s] * 0.2 * gain
        prev = m


play_hook(8)
play_hook(22, 5)    # over iv - VI - i - VII: a fourth up, the same shape
# The second verse: its first half, an octave down and quieter.
play_hook(16, -12, 0.6, HOOK[:16])

# A short echo, and the lead a touch left of the middle.
d = at(3)
echo = np.zeros(N)
echo[d:] = lowpass(lead, 3000)[: N - d] * 0.22
L += lead * 0.95 + echo * 0.4
R += lead * 0.85 + echo * 0.7


# --- a room, then the master ----------------------------------------------------------
def room(x, delays):
    out = np.zeros_like(x)
    for dl in delays:
        k = int(dl * SR)
        y = x
        for _ in range(3):
            y = np.concatenate([np.zeros(k), y[:-k]]) * 0.4
            out += lowpass(y, 3500) * 0.22
    return out


L, R = L + 0.25 * room(L, (0.023, 0.031, 0.043)), R + 0.25 * room(R, (0.027, 0.037, 0.047))
mix = np.stack([L, R], axis=1)
mix /= np.max(np.abs(mix))
mix = np.tanh(mix * 1.8) / np.tanh(1.8) * 0.9
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix * 32767).astype('<i2').tobytes())
print('seconds', N / SR)
