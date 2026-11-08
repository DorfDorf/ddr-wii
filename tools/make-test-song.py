#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the M4c test song: a 120 BPM .sm chart for the click track (doc 05 M4c).

The music is tools/make-click-track.py's output (beat k at exactly k * 0.5 s, beat 0
at sample 0), so #OFFSET is 0 and every note lands on a click. The sync is perfect by
construction, which makes this the reference song for judging and later for offset
calibration. Everything here is generated, so it's licence-clean.

Charts (notes from measure 2, 4 s in, to measure 65, about 2:12):
  Beginner 1  quarter notes, one panel at a time, L D U R
  Easy     3  quarters with a jump every 2nd downbeat and a 2-beat hold every 4th measure
  Medium   5  eighths (the off-beats fall between clicks), jumps and holds

  py tools/make-test-song.py "sd/ddrwii/songs/ddr-wii tests/Click 120"
  py tools/make-test-song.py --sm-only game/tests/corpus/synthetic/20_test_song.sm
"""
import argparse
import os

LEAD_IN = 2     # empty measures before the first note
MEASURES = 64   # measures with notes
MUSIC = "click120-44k.ogg"


def row(*lanes, kind="1"):
    return "".join(kind if i in lanes else "0" for i in range(4))


def beginner():
    out = []
    for m in range(MEASURES):
        out.append([row(b) for b in range(4)])   # L D U R on the four beats
    return out


def easy():
    out = []
    for m in range(MEASURES):
        if m % 4 == 3:   # 2-beat hold on Down, then taps on Up and Right
            out.append([row(1, kind="2"), "0000", row(1, kind="3"), row(3)])
            continue
        first = row(0, 3) if m % 2 == 0 else row(0)
        out.append([first, row(1), row(2), row(3)])
    return out


def medium():
    seq = [0, 1, 2, 3, 2, 1, 0, 3]
    out = []
    for m in range(MEASURES):
        if m % 4 == 3:   # 1-beat hold on Left under eighths on the right panels
            rows = [row(0, kind="2"), row(3), row(0, kind="3"), row(2),
                    row(1), row(2), row(1, 2), "0000"]
        else:
            rows = [row(seq[i]) for i in range(8)]
            if m % 2 == 0:
                rows[0] = row(0, 3)
        out.append(rows)
    return out


def notes_block(difficulty, meter, measures):
    lines = ["#NOTES:", "     dance-single:", "     ddr-wii:", f"     {difficulty}:",
             f"     {meter}:", "     0.000,0.000,0.000,0.000,0.000:"]
    body = [["0000"] * 4 for _ in range(LEAD_IN)] + measures
    for i, m in enumerate(body):
        lines.extend(m)
        lines.append("," if i + 1 < len(body) else ";")
    return "\n".join(lines)


def sm_text():
    head = [
        "#TITLE:Click 120;",
        "#SUBTITLE:ddr-wii test song;",
        "#ARTIST:ddr-wii;",
        "#CREDIT:tools/make-test-song.py;",
        f"#MUSIC:{MUSIC};",
        "#OFFSET:0.000;",
        "#SAMPLESTART:4.000;",
        "#SAMPLELENGTH:12.000;",
        "#BPMS:0.000=120.000;",
        "#STOPS:;",
    ]
    charts = [notes_block("Beginner", 1, beginner()), notes_block("Easy", 3, easy()),
              notes_block("Medium", 5, medium())]
    return "\n".join(head) + "\n\n" + "\n\n".join(charts) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out", help="song folder (writes click120.sm), or the .sm path with --sm-only")
    p.add_argument("--sm-only", action="store_true", help="write just the .sm file at OUT")
    a = p.parse_args()
    path = a.out if a.sm_only else os.path.join(a.out, "click120.sm")
    if os.path.dirname(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(sm_text())
    print(f"{path}: 3 charts, music {MUSIC}")


if __name__ == "__main__":
    main()
