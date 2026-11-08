#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ddr-wii -- simfile timing oracle.
#
# Uses the `simfile` library (MIT, https://github.com/garcia/simfile;
# `py -m pip install simfile`) to dump, for every note of every chart, its
# row (48 per beat), column, note character and time in seconds. The C++
# tests in game/tests/ compare our parser + TimingData against these dumps.
#
# Usage:
#   py tools/simfile-oracle.py FILE.sm [FILE.ssc ...]        # print to stdout
#   py tools/simfile-oracle.py --out-dir DIR FILE...           # DIR/<name>.tsv
#   py tools/simfile-oracle.py --regen                         # whole corpus
#
# Output format (tab-separated, one note per line, '#' lines are comments):
#   chart  row  column  type  time
# chart  index of the chart in file order
# row    note row, beat * 48, rounded to the nearest row like StepMania does
# column 0-based
# type   the .sm note character: 1 2 3 4 M L F K
# time   seconds, repr() of a Python float
#
# Notes on matching StepMania (see game/tests/README.md for the full list):
#  - Beats are rounded to 1/48 before timing, as StepMania stores note rows.
#  - Unhittable notes (inside warps) are kept (UnhittableNotes.KEEP_NOTE).
#  - Unmatched hold heads/tails are dropped the way StepMania drops them
#    (simfile keeps them); see stepmania_hold_filter().

import argparse
import os
import sys
import warnings
from fractions import Fraction

warnings.filterwarnings("ignore", category=UserWarning)  # fs/pkg_resources noise

try:
    import simfile
    from simfile.notes import NoteData
    from simfile.timing import Beat, TimingData
    from simfile.timing.engine import TimingEngine
except ImportError:  # pragma: no cover
    sys.exit("simfile is not installed: py -m pip install simfile")

ROWS_PER_BEAT = 48
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.join(REPO, "game", "tests", "corpus")
EXPECTED = os.path.join(REPO, "game", "tests", "expected")


def note_row(beat: Fraction) -> int:
    # Python's round() on a Fraction is round-half-even, like lrint().
    return int(round(beat * ROWS_PER_BEAT))


def stepmania_hold_filter(notes):
    """Drop the hold parts StepMania drops (NoteDataUtil::LoadFromSMNoteData):
    a '3' only closes the nearest note above it in its column if that note is
    an open '2'/'4' head ('K' is skipped over); unmatched '3's are ignored and
    heads that never get a tail are removed. simfile keeps both."""
    keep = [True] * len(notes)
    open_head = {}  # column -> index of the open head
    for i, n in enumerate(notes):
        c, t = n.column, n.note_type.value
        if t == "K":
            continue
        if t in "24":
            if c in open_head:
                keep[open_head[c]] = False
            open_head[c] = i
        elif t == "3":
            if c in open_head:
                del open_head[c]
            else:
                keep[i] = False
        else:  # a tap, mine, lift or fake blocks the hold above it
            if c in open_head:
                keep[open_head.pop(c)] = False
    for i in open_head.values():
        keep[i] = False
    return [n for n, k in zip(notes, keep) if k]


def dump(path: str) -> str:
    sim = simfile.open(path, strict=False)
    lines = [f"# simfile-oracle {simfile.__version__}: {os.path.basename(path)}"]
    for ci, chart in enumerate(sim.charts):
        try:
            engine = TimingEngine(TimingData(sim, chart))
        except Exception as e:  # e.g. no BPMs
            lines.append(f"# chart {ci}: no timing ({e})")
            continue
        notes = sorted(NoteData(chart), key=lambda n: (n.column, n.beat))
        notes = stepmania_hold_filter(notes)
        notes.sort(key=lambda n: (n.beat, n.column))
        dropped = sum(1 for _ in NoteData(chart)) - len(notes)
        if dropped:
            lines.append(f"# chart {ci}: {dropped} unmatched hold head(s)/tail(s) dropped")
        for note in notes:
            row = note_row(note.beat)
            t = engine.time_at(Beat(row, ROWS_PER_BEAT))
            lines.append(f"{ci}\t{row}\t{note.column}\t{note.note_type.value}\t{float(t)!r}")
    return "\n".join(lines) + "\n"


def corpus_files():
    out = []
    for root, _dirs, files in os.walk(CORPUS):
        for f in sorted(files):
            if f.lower().endswith((".sm", ".ssc")):
                out.append(os.path.join(root, f))
    return sorted(out)


def expected_name(path: str) -> str:
    rel = os.path.relpath(path, CORPUS).replace("\\", "/")
    return rel.replace("/", "__") + ".tsv"


def main() -> int:
    ap = argparse.ArgumentParser(description="Dump note times from the simfile library.")
    ap.add_argument("files", nargs="*")
    ap.add_argument("--out-dir")
    ap.add_argument("--regen", action="store_true",
                    help="regenerate game/tests/expected/ from game/tests/corpus/")
    args = ap.parse_args()

    if args.regen:
        files, out_dir = corpus_files(), EXPECTED
    else:
        files, out_dir = args.files, args.out_dir
    if not files:
        ap.error("no input files")

    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    for f in files:
        text = dump(f)
        if out_dir:
            name = expected_name(f) if args.regen else os.path.basename(f) + ".tsv"
            with open(os.path.join(out_dir, name), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            print(f"wrote {name}")
        else:
            sys.stdout.write(text)
    if args.regen:
        # The C++ tests read this list to know which corpus files to load.
        with open(os.path.join(out_dir, "index.txt"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("# corpus files (relative to game/tests/corpus/), written by "
                     "tools/simfile-oracle.py --regen\n")
            for f in files:
                fh.write(os.path.relpath(f, CORPUS).replace("\\", "/") + "\n")
        print("wrote index.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
