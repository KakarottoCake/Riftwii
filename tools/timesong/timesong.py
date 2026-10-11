#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# An original, driving minor-key synth-pop loop for the "Would You Look
# at the Time" easter-egg theme. Everything here (chords, bass, arp, lead
# melody, drums) is written for RiftWii; nothing is sampled or transcribed.
# Writes a 44.1 kHz stereo WAV; ffmpeg turns it into music.ogg.
import sys
import numpy as np
from scipy.signal import lfilter

SR = 44100
BPM = 90
S16 = 60.0 / BPM / 4          # one sixteenth, seconds
BARS = 32
N = int(round(BARS * 16 * S16 * SR))
rng = np.random.default_rng(2610)

L = np.zeros(N)
R = np.zeros(N)
duck = np.ones(N)             # the sidechain pump from the kick


def at(step):
    return int(round(step * S16 * SR))


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    return lfilter([1 - a], [1, -a], x)


def saw(f, n, detune=0.0, phase=0.0):
    t = np.arange(n) / SR
    return 2 * ((t * f * (1 + detune) + phase) % 1.0) - 1


def add(sig, start, pan=0.0, gain=1.0):
    s = at(start) if isinstance(start, float) or isinstance(start, int) else start
    e = min(N, s + len(sig))
    if e <= s:
        return
    g = sig[: e - s] * gain
    L[s:e] += g * np.sqrt(0.5 * (1 - pan))
    R[s:e] += g * np.sqrt(0.5 * (1 + pan))


def env(n, attack, decay, sustain=0.0, release=None):
    t = np.arange(n) / SR
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    d = sustain + (1 - sustain) * np.exp(-t / max(decay, 1e-4))
    out = a * d
    if release:
        r = int(release * SR)
        if r < n:
            out[-r:] *= np.linspace(1, 0, r)
    return out


# --- the song's plan ----------------------------------------------------
# One chord a bar: i - VI - III - VII, and a turn to iv in the second half.
PROG_A = [(40, [64, 67, 71]), (36, [60, 64, 67]), (43, [62, 67, 71]), (38, [62, 66, 69])]   # Em C G D
PROG_B = [(45, [64, 69, 72]), (36, [60, 64, 67]), (40, [64, 67, 71]), (38, [62, 66, 69])]   # Am C Em D
chords = []
for bar in range(BARS):
    prog = PROG_B if 22 <= bar < 30 else PROG_A
    chords.append(prog[bar % 4])

# Gets going fast: two bars in, the beat's there.
intro = range(0, 2)
verse = range(2, 8)
chorus1 = range(8, 16)
verse2 = range(16, 20)
breakdown = range(20, 22)    # the build: a riser and a snare roll
chorus2 = range(22, 30)
tag = range(30, 32)
drums_on = set(verse) | set(chorus1) | set(verse2) | set(chorus2) | set(tag)
full = set(chorus1) | set(chorus2) | set(tag)
CRASHES = (2, 8, 16, 22, 30)

# --- drums --------------------------------------------------------------
kick_n = int(0.32 * SR)
tk = np.arange(kick_n) / SR
kf = 45 + 110 * np.exp(-tk * 28)
kick = np.sin(2 * np.pi * np.cumsum(kf) / SR) * np.exp(-tk * 9)
kick[: int(0.003 * SR)] += rng.uniform(-0.5, 0.5, int(0.003 * SR))

clap_n = int(0.22 * SR)
tc = np.arange(clap_n) / SR
noise = rng.uniform(-1, 1, clap_n)
clap = (noise - lowpass(noise, 900)) * np.exp(-tc * 16) * 0.9
clap += np.sin(2 * np.pi * 190 * tc) * np.exp(-tc * 30) * 0.4

hat_n = int(0.05 * SR)
hn = rng.uniform(-1, 1, hat_n)
hat = (hn - lowpass(hn, 7000)) * np.exp(-np.arange(hat_n) / SR * 90)
ohat_n = int(0.2 * SR)
on = rng.uniform(-1, 1, ohat_n)
ohat = (on - lowpass(on, 6000)) * np.exp(-np.arange(ohat_n) / SR * 14)

crash_n = int(1.6 * SR)
cn = rng.uniform(-1, 1, crash_n)
crash = (cn - lowpass(cn, 4500)) * np.exp(-np.arange(crash_n) / SR * 2.6)

pump = 1 - 0.6 * np.exp(-np.arange(at(4)) / SR * 10)


def kick_at(step, gain):
    add(kick, step, 0, gain)
    s0 = at(step)
    e0 = min(N, s0 + len(pump))
    duck[s0:e0] = np.minimum(duck[s0:e0], pump[: e0 - s0])


for bar in range(BARS):
    if bar in CRASHES:
        add(crash, bar * 16, 0.3, 0.32)
        add(crash, bar * 16 + 0.02, -0.3, 0.25)
    for beat in range(4):
        step = bar * 16 + beat * 4
        if bar in drums_on:
            kick_at(step, 1.0)
            if beat in (1, 3):
                add(clap, step, 0.05, 0.7 if bar in full else 0.55)
                add(clap, step + 0.03, -0.1, 0.25)
            add(ohat, step + 2, 0.2, 0.2 if bar in full else 0.14)
            # Sixteenth hats all the way, the off ones lighter.
            for k in range(4):
                add(hat, step + k, -0.25, (0.2 if k % 2 == 0 else 0.12) * (1.0 if bar in full else 0.8))
        # The pickup kick into the next bar.
        if bar in drums_on and beat == 3 and (bar + 1) % 2 == 0:
            kick_at(step + 3, 0.7)
    # The build: snare in eighths, then sixteenths, louder; a rising noise.
    if bar in breakdown:
        first = bar == breakdown[0]
        for k in range(0, 16, 2 if first else 1):
            add(clap, bar * 16 + k, 0, 0.18 + 0.4 * ((bar - breakdown[0]) * 16 + k) / 32)
riser_s, riser_e = at(breakdown[0] * 16), at(chorus2[0] * 16)
rn = rng.uniform(-1, 1, riser_e - riser_s)
tr_ = np.linspace(0, 1, len(rn))
riser = np.zeros_like(rn)
step_len = 2048
for i in range(0, len(rn), step_len):  # a high-pass that climbs
    seg = rn[i:i + step_len]
    cut = 400 + 7000 * tr_[i] ** 2
    riser[i:i + step_len] = seg - lowpass(seg, cut)
riser *= tr_ ** 1.5 * 0.35
L[riser_s:riser_e] += riser
R[riser_s:riser_e] += riser

# --- bass: galloping sixteenths, the octave on every third ---------------------
GALLOP = [0, 0, 12, 0, 0, 12, 0, 12, 0, 0, 12, 0, 0, 12, 7, 12]
for bar in range(BARS):
    if bar in intro or bar in breakdown:
        continue
    root, _ = chords[bar]
    for k in range(16):
        n = at(1) - 120
        f = hz(root + GALLOP[k])
        x = saw(f, n) + 0.7 * saw(f, n, 0.007) + 0.5 * np.sign(np.sin(2 * np.pi * f / 2 * np.arange(n) / SR))
        y = lowpass(lowpass(x, 900 if bar in full else 650), 2200)
        y = np.tanh(y * 1.6)
        acc = 1.0 if k % 4 == 0 else 0.8
        add(y * env(n, 0.002, 0.09, 0.4, 0.012), bar * 16 + k, 0, 0.3 * acc)

# --- pad: detuned saws, filtered, ducked ---------------------------------------
pad = np.zeros(N)
for bar in range(BARS):
    _, notes = chords[bar]
    n = at(16) + int(0.15 * SR)
    x = np.zeros(n)
    for m in notes:
        for d in (-0.007, 0.0, 0.006):
            x += saw(hz(m), n, d, rng.uniform())
    x = lowpass(lowpass(x, 1500 if bar in full else 900), 2400)
    s = at(bar * 16)
    e = min(N, s + n)
    pad[s:e] += (x * env(n, 0.08, 10, 1.0, 0.18))[: e - s] * 0.07
pad *= duck
L += pad * 0.95
R += np.roll(pad, int(0.011 * SR)) * 0.95

# --- arp: sixteenths through the chord, panned side to side ---------------------
for bar in range(BARS):
    _, notes = chords[bar]
    tones = [notes[0] + 12, notes[1] + 12, notes[2] + 12, notes[1] + 12, notes[0] + 24, notes[2] + 12, notes[1] + 12, notes[2]]
    lvl = 0.07 if bar in breakdown else (0.08 if bar in intro else 0.095)
    for k in range(16):
        n = at(1) + 600
        f = hz(tones[k % len(tones)])
        sq = np.sign(np.sin(2 * np.pi * f * np.arange(n) / SR)) * 0.6 + saw(f, n, 0.003) * 0.4
        y = lowpass(sq, 2600 if bar in full else 1700) * env(n, 0.002, 0.07)
        add(y, bar * 16 + k, -0.45 if k % 2 == 0 else 0.45, lvl)

# --- guitar: palm-muted sixteenths in the verses, open power chords in the
# choruses, doubled hard left and right ------------------------------------------
for bar in range(BARS):
    if bar in intro or bar in breakdown:
        continue
    root, _ = chords[bar]
    f = hz(root + 12)
    if bar in full:
        for e8 in range(8):
            n = at(2) - 80
            x = saw(f, n, 0.0) + saw(f * 1.4983, n, 0.003) + 0.8 * saw(f * 2, n, -0.004)
            y = np.tanh(lowpass(x, 2600) * 6.0) * env(n, 0.003, 0.6, 0.6, 0.02)
            y = lowpass(y, 4200)
            add(y, bar * 16 + e8 * 2, -0.75, 0.075)
            add(y, bar * 16 + e8 * 2 + 0.08, 0.75, 0.07)
    else:
        for k in range(16):
            n = at(1) - 60
            x = saw(f, n, 0.0) + saw(f * 1.4983, n, 0.003)
            y = lowpass(np.tanh(lowpass(x, 1400) * 6.0), 1800) * env(n, 0.002, 0.05)
            add(y, bar * 16 + k, -0.7 if k % 2 else 0.7, 0.07 if k % 4 == 0 else 0.05)

# --- lead: the hook, an original line over i - VI - III - VII -------------------
# (sixteenth in the 8-bar phrase, length in sixteenths, MIDI note)
HOOK = [
    (0, 2, 71), (2, 2, 71), (4, 2, 69), (6, 2, 67), (8, 4, 69), (12, 4, 71),
    (16, 4, 76), (20, 2, 74), (22, 2, 71), (24, 6, 72), (30, 2, 71),
    (32, 2, 74), (34, 2, 74), (36, 2, 71), (38, 2, 67), (40, 4, 69), (44, 4, 67),
    (48, 6, 66), (54, 2, 69), (56, 8, 71),
    (64, 2, 71), (66, 2, 74), (68, 4, 76), (72, 2, 74), (74, 2, 71), (76, 4, 74),
    (80, 2, 76), (82, 2, 79), (84, 4, 76), (88, 4, 74), (92, 4, 71),
    (96, 4, 74), (100, 2, 71), (102, 2, 69), (104, 4, 67), (108, 4, 69),
    (112, 4, 66), (116, 12, 64),
]
lead = np.zeros(N)


def play_lead(start_bar, transpose=0, gain=1.0):
    for s16, l16, m in HOOK:
        n = at(l16) + int(0.06 * SR)
        t = np.arange(n) / SR
        f = hz(m + transpose) * (1 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip(t * 4 - 0.4, 0, 1))
        ph = np.cumsum(f) / SR
        x = (2 * (ph % 1) - 1) * 0.5 + (2 * ((ph * 1.004) % 1) - 1) * 0.5 + 0.35 * np.sign(np.sin(2 * np.pi * ph * 0.5))
        y = lowpass(x, 5200) * env(n, 0.004, 0.6, 0.65, 0.05)
        # Doubled an octave up, a little quieter: brighter, bigger.
        ph2 = ph * 2
        x2 = (2 * (ph2 % 1) - 1) * 0.5 + (2 * ((ph2 * 1.003) % 1) - 1) * 0.5
        y += 0.45 * lowpass(x2, 6000) * env(n, 0.004, 0.5, 0.5, 0.05)
        s = at(start_bar * 16 + s16)
        e = min(N, s + n)
        lead[s:e] += y[: e - s] * 0.12 * gain


play_lead(8)
play_lead(22)
# A quieter, octave-down hint of the hook in the second verse.
for s16, l16, m in HOOK[:20]:
    n = at(l16) + int(0.06 * SR)
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * hz(m - 12) * t) + 0.3 * np.sin(4 * np.pi * hz(m - 12) * t)
    s = at(16 * 16 + s16)
    e = min(N, s + n)
    lead[s:e] += (x * env(n, 0.01, 0.5, 0.4, 0.05))[: e - s] * 0.08

# Ping-pong echo, three sixteenths.
d = at(3)
echoL = np.zeros(N)
echoR = np.zeros(N)
src = lead.copy()
for k in range(1, 6):
    sh = d * k
    g = 0.42 ** k
    tgt = echoL if k % 2 else echoR
    tgt[sh:] += lowpass(src, 2500)[: N - sh] * g
L += lead + echoL
R += lead + echoR

# --- a little room, then glue -----------------------------------------------
def room(x, delays=(0.029, 0.037, 0.041, 0.053), fb=0.45):
    out = np.zeros_like(x)
    for dl in delays:
        k = int(dl * SR)
        y = x.copy()
        for _ in range(4):
            y = np.concatenate([np.zeros(k), y[:-k]]) * fb
            out += lowpass(y, 3000) * 0.25
    return out


wetL, wetR = room(L), room(R, (0.031, 0.043, 0.047, 0.059))
L, R = L + 0.35 * wetL, R + 0.35 * wetR
# The loop: fade the last beat's tails into the first.
tail = at(4)
mix = np.stack([L, R], axis=1)
peak = np.max(np.abs(mix))
mix = np.tanh(mix / peak * 2.3) / np.tanh(2.3) * 0.9
import wave
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix * 32767).astype('<i2').tobytes())
print('seconds', N / SR)
