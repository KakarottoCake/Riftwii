# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the menu's two sounds, made here from sine waves (no recordings):

  vendor-libgui/source/sounds/button_over.pcm   the hover tick
  vendor-libgui/source/sounds/button_click.pcm  the press

Both are 48 kHz, 16-bit big-endian stereo, what GuiSound hands to ASND.
The tick is a short, quiet, round "tock"; the press two soft mallet notes
a fifth apart, in the spirit of the Wii Menu's gentle clicks. With --wav
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
    sounds = {"button_over": render(0.045, hover), "button_click": render(0.21, click)}
    for name, frames in sounds.items():
        write_pcm(name + ".pcm", frames)
    if "--wav" in sys.argv:
        folder = sys.argv[sys.argv.index("--wav") + 1]
        for name, frames in sounds.items():
            write_wav(os.path.join(folder, name + ".wav"), frames)
    print("wrote", ", ".join(n + ".pcm" for n in sounds))


if __name__ == "__main__":
    main()
