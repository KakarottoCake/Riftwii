# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the menu's two sounds, made here from sine waves (no recordings):

  vendor-libgui/source/sounds/button_over.pcm   the hover tick
  vendor-libgui/source/sounds/button_click.pcm  the press
  vendor-libgui/source/sounds/home_open.pcm     the HOME Menu opening
  vendor-libgui/source/sounds/home_close.pcm    and closing

Both are 48 kHz, 16-bit big-endian stereo, what GuiSound hands to ASND.
The tick is a short, quiet, round "tock"; the press two soft mallet notes
a fifth apart, in the spirit of the Wii Menu's gentle clicks. The HOME
Menu opens with a soft rising whoosh under three bell notes going up (a
major arpeggio), and closes with the same going down. With --wav
DIR it also writes .wav copies to listen to on a computer.

    py -3 tools/make_sounds.py [--wav DIR]
"""

import math
import os
import struct
import sys
import wave

RATE = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vendor-libgui", "source", "sounds")


def mallet(freq, start, length, level, decay, t):
    """A struck note: the fundamental and a faint fourth partial that dies
    faster, a 2 ms rise so it never clicks."""
    x = t - start
    if x < 0 or x > length:
        return 0.0
    rise = min(1.0, x / 0.002)
    body = math.sin(2 * math.pi * freq * x) * math.exp(-x / decay)
    shine = 0.18 * math.sin(2 * math.pi * freq * 4.0 * x) * math.exp(-x / (decay * 0.25))
    tail = min(1.0, (length - x) / 0.004)  # a short fade at the very end
    return level * rise * tail * (body + shine)


def render(seconds, voice):
    frames = []
    for i in range(int(seconds * RATE)):
        v = voice(i / RATE)
        s = int(max(-1.0, min(1.0, v)) * 32767)
        frames.append(s)
    return frames


def hover(t):
    return mallet(1760.0, 0.0, 0.045, 0.30, 0.010, t)


def click(t):
    return (mallet(1046.5, 0.0, 0.16, 0.34, 0.045, t) +   # C6
            mallet(1568.0, 0.045, 0.16, 0.30, 0.050, t))  # G6


def whoosh(start, length, level, rising, t):
    """Air: a few detuned sines gliding together, swelling then fading,
    so the band slides in (rising) or out (falling) without any noise
    source (repeatable from run to run)."""
    x = t - start
    if x < 0 or x > length:
        return 0.0
    p = x / length
    glide = p if rising else 1.0 - p
    env = math.sin(math.pi * p) ** 2
    v = 0.0
    for base, spread in ((420.0, 1.0), (433.0, 1.13), (611.0, 0.87), (857.0, 1.21)):
        f0 = base * spread
        # The phase of a linear glide from f0 to 2.2 * f0 over `length`.
        k = 1.2 * f0 / length
        phase = f0 * x + 0.5 * k * x * x if rising else 2.2 * f0 * x - 0.5 * k * x * x
        v += math.sin(2 * math.pi * phase)
    return level * env * (0.35 + 0.65 * glide) * v / 4.0


def home_open(t):
    return (whoosh(0.0, 0.26, 0.10, True, t) +
            mallet(1318.5, 0.03, 0.30, 0.20, 0.070, t) +   # E6
            mallet(1568.0, 0.09, 0.30, 0.20, 0.075, t) +   # G6
            mallet(2093.0, 0.15, 0.34, 0.22, 0.085, t))    # C7


def home_close(t):
    return (whoosh(0.0, 0.24, 0.09, False, t) +
            mallet(2093.0, 0.02, 0.26, 0.18, 0.060, t) +   # C7
            mallet(1568.0, 0.08, 0.26, 0.18, 0.060, t) +   # G6
            mallet(1318.5, 0.14, 0.30, 0.19, 0.070, t))    # E6


def write_pcm(name, frames):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(b"".join(struct.pack(">hh", s, s) for s in frames))


def write_wav(path, frames):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<hh", s, s) for s in frames))


def main():
    sounds = {"button_over": render(0.045, hover), "button_click": render(0.21, click),
              "home_open": render(0.50, home_open), "home_close": render(0.45, home_close)}
    for name, frames in sounds.items():
        write_pcm(name + ".pcm", frames)
    if "--wav" in sys.argv:
        folder = sys.argv[sys.argv.index("--wav") + 1]
        for name, frames in sounds.items():
            write_wav(os.path.join(folder, name + ".wav"), frames)
    print("wrote", ", ".join(n + ".pcm" for n in sounds))


if __name__ == "__main__":
    main()
