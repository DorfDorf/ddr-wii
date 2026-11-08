#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a metronome click track as a 16-bit stereo WAV (M4b audio clock test, doc 05).

Beat k starts exactly at sample round(k * 60 / bpm * rate), beat 0 at sample 0.
Every 4th beat (the downbeat) clicks higher, so a lost or doubled beat is easy to hear.

  py tools/make-click-track.py out.wav                 # 48 kHz, 120 BPM, 5 min
  py tools/make-click-track.py out.wav --rate 44100 --seconds 60
"""
import argparse
import math
import struct
import wave

CLICK_MS = 30  # length of each click


def click(rate, freq):
    n = int(rate * CLICK_MS / 1000)
    out = bytearray()
    for i in range(n):
        env = (1.0 - i / n) ** 3  # sharp attack, fast decay: an easy-to-locate onset
        s = int(26000 * env * math.sin(2 * math.pi * freq * i / rate))
        out += struct.pack("<hh", s, s)
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out")
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--bpm", type=float, default=120.0)
    p.add_argument("--seconds", type=float, default=300.0)
    a = p.parse_args()

    total = int(a.seconds * a.rate)
    hi, lo = click(a.rate, 1760), click(a.rate, 880)
    silence = b"\0\0\0\0"
    with wave.open(a.out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(a.rate)
        pos, beat = 0, 0
        while pos < total:
            start = round(beat * 60.0 / a.bpm * a.rate)
            if start > pos:  # silence up to the beat
                w.writeframesraw(silence * (min(start, total) - pos))
                pos = min(start, total)
                continue
            c = hi if beat % 4 == 0 else lo
            frames = min(len(c) // 4, total - pos)
            w.writeframesraw(c[: frames * 4])
            pos += frames
            beat += 1
    print(f"{a.out}: {a.rate} Hz, {a.bpm:g} BPM, {a.seconds:g} s")


if __name__ == "__main__":
    main()
