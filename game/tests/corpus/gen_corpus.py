#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates the synthetic part of the M4a test corpus (game/tests/corpus/synthetic/).
# The files are committed; rerun this only to change them, then regenerate the
# oracle dumps with `py tools/simfile-oracle.py --regen`.
#
# Every file here must be one where StepMania and the `simfile` oracle agree.
# Known differences we steer around (see game/tests/README.md):
#   - full-line comments or blank lines *inside* a measure (simfile counts them
#     as rows; StepMania skips them),
#   - ",," (StepMania merges empty measures),
#   - negative BPMs/stops in .sm (StepMania turns them into warps),
#   - duplicate stops on one beat (StepMania keeps the last, simfile adds them),
#   - split-timing charts without their own #OFFSET or #BPMS,
#   - holds without tails / tails without heads.

import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "synthetic")

rng = random.Random(4242)


def measure(rows, cols=4, density=0.35, kinds="1"):
    lines = []
    for _ in range(rows):
        line = ""
        for _c in range(cols):
            line += rng.choice(kinds) if rng.random() < density else "0"
        lines.append(line)
    return lines


def taps(nmeasures, rows=4, cols=4, density=0.35):
    return [measure(rows, cols, density) for _ in range(nmeasures)]


def join_measures(ms, nl="\n", comment_every=0):
    out = []
    for i, m in enumerate(ms):
        out.append(nl.join(m))
        sep = ","
        if comment_every and i % comment_every == 0:
            sep += f" // measure {i + 1}"
        out.append(sep if i < len(ms) - 1 else ";")
    return nl.join(out)


def holds_measures(nmeasures, rows=16, cols=4):
    """Random taps, holds, rolls and mines, with every hold closed."""
    open_hold = [None] * cols  # remaining rows
    ms = []
    total = nmeasures * rows
    r = 0
    for _m in range(nmeasures):
        m = []
        for _l in range(rows):
            line = []
            for c in range(cols):
                ch = "0"
                if open_hold[c] is not None:
                    open_hold[c] -= 1
                    if open_hold[c] <= 0:
                        ch = "3"
                        open_hold[c] = None
                elif r < total - 8:
                    x = rng.random()
                    if x < 0.05:
                        ch = "2"
                        open_hold[c] = rng.randint(1, 6)
                    elif x < 0.07:
                        ch = "4"
                        open_hold[c] = rng.randint(1, 6)
                    elif x < 0.10:
                        ch = "M"
                    elif x < 0.25:
                        ch = "1"
                line.append(ch)
            m.append("".join(line))
            r += 1
        ms.append(m)
    # close anything still open on the last row
    last = list(ms[-1][-1])
    for c in range(cols):
        if open_hold[c] is not None:
            last[c] = "3"
    ms[-1][-1] = "".join(last)
    return ms


def sm_file(header, charts, nl="\n"):
    s = []
    for k, v in header:
        s.append(f"#{k}:{v};")
    for ct in charts:
        st = ct.get("stepstype", "dance-single")
        s.append(
            f"#NOTES:{nl}     {st}:{nl}     {ct.get('desc', '')}:{nl}"
            f"     {ct['diff']}:{nl}     {ct['meter']}:{nl}"
            f"     0.000,0.000,0.000,0.000,0.000:{nl}{ct['notes']}"
        )
    return nl.join(s) + nl


def ssc_file(header, charts, nl="\n"):
    s = []
    for k, v in header:
        s.append(f"#{k}:{v};")
    for ct in charts:
        s.append("")
        s.append("//---------------" + ct.get("stepstype", "dance-single") + "----------------")
        s.append("#NOTEDATA:;")
        s.append(f"#STEPSTYPE:{ct.get('stepstype', 'dance-single')};")
        s.append(f"#DESCRIPTION:{ct.get('desc', '')};")
        s.append(f"#DIFFICULTY:{ct['diff']};")
        s.append(f"#METER:{ct['meter']};")
        for k, v in ct.get("tags", []):
            s.append(f"#{k}:{v};")
        s.append("#NOTES:")
        s.append(ct["notes"])
    return nl.join(s) + nl


def write(name, text):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="") as f:
        f.write(text)


def head(title, offset, bpms, stops=None, extra=()):
    h = [("TITLE", title), ("ARTIST", "ddr-wii test corpus"), ("MUSIC", "none.ogg"),
         ("OFFSET", offset), ("BPMS", bpms)]
    if stops is not None:
        h.append(("STOPS", stops))
    h.extend(extra)
    return h


def main():
    # 1. Constant BPM, two charts.
    write("01_constant_bpm.sm", sm_file(
        head("Constant BPM", "-0.050", "0.000=120.000", ""),
        [{"diff": "Easy", "meter": 3, "notes": join_measures(taps(24, 4))},
         {"diff": "Hard", "meter": 8, "notes": join_measures(taps(24, 8, density=0.5))}]))

    # 2. Many BPM changes, including off-grid decimal beats (rounded to rows).
    beats = sorted(set([0.0] + [round(rng.uniform(0.2, 120), 3) for _ in range(60)]))
    bpms = ",\n".join(f"{b:.3f}={rng.uniform(60, 400):.3f}" for b in beats)
    write("02_many_bpm_changes.sm", sm_file(
        head("Many BPM changes", "0.123", bpms, ""),
        [{"diff": "Medium", "meter": 6, "notes": join_measures(taps(32, 16))}]))

    # 3. Stops, including one at beat 0, stops on notes and #FREEZES alias.
    write("03_stops.sm", sm_file(
        [("TITLE", "Stops"), ("OFFSET", "-0.010"), ("BPMS", "0.000=150.000,32.000=75.000"),
         ("FREEZES", "0.000=0.500,4.000=0.250,8.500=1.000,16.000=0.125,16.250=0.125,"
                     "32.000=2.000,47.750=0.333")],
        [{"diff": "Hard", "meter": 9, "notes": join_measures(taps(16, 16, density=0.4))}]))

    # 4. BPM change and stop on the same beat; stops between rows.
    write("04_bpm_and_stop_same_beat.sm", sm_file(
        head("BPM+stop", "0.000", "0=100,8=200,16=50,24=133.333", "8=0.5,16=0.75,20.1=0.2,24=1.0"),
        [{"diff": "Challenge", "meter": 10, "notes": join_measures(taps(10, 8, density=0.6))}]))

    # 5. .ssc delays (note on the delay is hit after it), delay+stop on one beat.
    write("05_delays.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Delays"), ("OFFSET", "-0.020"),
         ("BPMS", "0.000=140.000"), ("STOPS", "12.000=0.400,20.000=0.300"),
         ("DELAYS", "4.000=0.300,12.000=0.200,13.500=0.050,28.000=1.000")],
        [{"diff": "Medium", "meter": 5, "notes": join_measures(taps(9, 8, density=0.6))}]))

    # 6. .ssc warps: plain, overlapping, warp with a stop inside, notes inside.
    write("06_warps.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Warps"), ("OFFSET", "0.000"),
         ("BPMS", "0.000=120.000,10.000=240.000"),
         ("STOPS", "18.000=0.500"),
         ("WARPS", "4.000=2.000,12.000=4.000,14.000=4.000,17.000=3.000,30.000=0.500")],
        [{"diff": "Hard", "meter": 7, "notes": join_measures(taps(10, 8, density=0.5))}]))

    # 7. Split timing: each chart has its own BPMs/stops/offset (#VERSION 0.83).
    write("07_split_timing.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Split timing"), ("OFFSET", "-0.100"),
         ("BPMS", "0.000=150.000"), ("STOPS", "")],
        [{"diff": "Easy", "meter": 2, "notes": join_measures(taps(8, 4))},
         {"diff": "Hard", "meter": 9,
          "tags": [("OFFSET", "0.250"), ("BPMS", "0.000=150.000,16.000=300.000"),
                   ("STOPS", "8.000=0.750"), ("DELAYS", "20.000=0.125")],
          "notes": join_measures(taps(8, 16, density=0.4))},
         {"diff": "Challenge", "meter": 11,
          "tags": [("OFFSET", "-0.100"), ("BPMS", "0.000=75.000"),
                   ("WARPS", "8.000=4.000")],
          "notes": join_measures(taps(8, 8, density=0.4))}]))

    # 8. Old .ssc (#VERSION 0.53): chart timing tags are ignored.
    write("08_ssc_old_version.ssc", ssc_file(
        [("VERSION", "0.53"), ("TITLE", "Old ssc"), ("OFFSET", "0.040"),
         ("BPMS", "0.000=128.000,16.000=64.000"), ("STOPS", "8.000=0.250")],
        [{"diff": "Medium", "meter": 4, "tags": [("BPMS", "0.000=999.000")],
          "notes": join_measures(taps(8, 8))}]))

    # 9. Odd whitespace: CRLF, tabs, trailing comments, comments between tags,
    #    leading/trailing blank lines in measures, mixed-case tags.
    notes = join_measures(taps(12, 8), nl="\r\n", comment_every=3)
    notes = notes.replace("\r\n0", "\r\n\t  0", 5)
    text = ("// a comment before anything\r\n"
            "#TITLE:Whitespace;  // trailing comment\r\n"
            "   #offset:  -0.075 ;\r\n"
            "#BPMs:\r\n  0.000 = 145.000 ,\r\n\t16.000=170.000\r\n;\r\n"
            "#STOPS: 8.000 = 0.250 ;\r\n\r\n\r\n"
            "#NOTES:\r\n\tdance-single:\r\n\t:\r\n\tHard:\r\n\t7:\r\n\t0,0,0,0,0:\r\n"
            "\r\n" + notes + "\r\n")
    write("09_odd_whitespace.sm", text)

    # 10. Missing semicolons (MsdFile recovers when '#' starts a line).
    write("10_missing_semicolons.sm",
          "#TITLE:No semicolons\n#OFFSET:0.010\n#BPMS:0.000=111.000,8.000=222.000\n"
          "#STOPS:4.000=0.100\n"
          "#NOTES:\n     dance-single:\n     :\n     Easy:\n     2:\n     0:\n"
          + join_measures(taps(6, 4))[:-1] + "\n")

    # 11. All quantisations: 4..192 rows, plus 3/5/6/7/9/20/40-row measures whose
    #     rows get rounded to 1/48 beat.
    ms = []
    for rows in [4, 8, 12, 16, 24, 32, 48, 64, 96, 192, 3, 5, 6, 7, 9, 20, 40, 2, 1]:
        ms.append(measure(rows, density=0.5))
    write("11_quantisations.sm", sm_file(
        head("Quantisations", "0.000", "0.000=97.000,13.000=203.500", "30.000=0.123"),
        [{"diff": "Challenge", "meter": 12, "notes": join_measures(ms)}]))

    # 12. Holds, rolls, mines across BPM changes and stops.
    write("12_holds_rolls_mines.sm", sm_file(
        head("Holds", "-0.033", "0.000=160.000,9.000=80.000,21.500=240.000",
             "6.000=0.400,14.000=0.200,30.000=0.300"),
        [{"diff": "Hard", "meter": 9, "notes": join_measures(holds_measures(12, 16))}]))

    # 13. Lifts and fakes in a .ssc, plus #FAKES ranges.
    ms = taps(8, 8, density=0.5)
    for m in ms:
        for i in range(len(m)):
            if rng.random() < 0.2:
                m[i] = "".join(rng.choice("LF0") for _ in range(4))
    write("13_lifts_fakes.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Lifts and fakes"), ("OFFSET", "0.000"),
         ("BPMS", "0.000=130.000"), ("FAKES", "8.000=4.000")],
        [{"diff": "Medium", "meter": 5, "notes": join_measures(ms)}]))

    # 14. Extreme BPMs and a long song (precision over ~10 minutes).
    write("14_extreme_bpm.sm", sm_file(
        head("Extreme", "0.500", "0.000=1.500,1.000=60.000,4.000=1000.000,12.000=12.000,"
             "13.000=600.000,20.000=135.000", ""),
        [{"diff": "Challenge", "meter": 13, "notes": join_measures(taps(4, 16))},
         {"diff": "Beginner", "meter": 1, "notes": join_measures(taps(320, 4, density=0.25))}]))

    # 15. dance-double (8 columns) and dance-solo (6 columns) in one file.
    write("15_double_solo.sm", sm_file(
        head("Doubles", "-0.200", "0=175", "16=0.343"),
        [{"stepstype": "dance-double", "diff": "Hard", "meter": 10,
          "notes": join_measures([measure(8, 8) for _ in range(8)])},
         {"stepstype": "dance-solo", "diff": "Medium", "meter": 6,
          "notes": join_measures([measure(8, 6) for _ in range(8)])}]))

    # 16. Escapes in tags and stops/BPMs written with lots of decimals.
    write("16_escapes_decimals.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", r"Esc\:aped \#title\; here"),
         ("ARTIST", r"back\\slash"), ("OFFSET", "-0.009000000"),
         ("BPMS", "0.000000=181.685000,32.000000=90.842500"),
         ("STOPS", "16.000000=0.165130,48.500000=0.660523")],
        [{"diff": "Hard", "meter": 8, "notes": join_measures(taps(16, 16, density=0.3))}]))

    # 17. Stop and delay at beat 0, positive offset, first note on beat 0.
    write("17_beat0_events.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Beat zero"), ("OFFSET", "0.300"),
         ("BPMS", "0.000=100.000"), ("STOPS", "0.000=0.700"), ("DELAYS", "0.000=0.400")],
        [{"diff": "Easy", "meter": 1, "notes": join_measures([["1000", "0100", "0010", "0001"]] * 4)}]))

    # 18. Split timing with warps and scrolls/speeds (scrolls/speeds don't move
    #     note times).
    write("18_split_warps_scrolls.ssc", ssc_file(
        [("VERSION", "0.83"), ("TITLE", "Split warps"), ("OFFSET", "0.000"),
         ("BPMS", "0.000=200.000"), ("SCROLLS", "0.000=1.000,8.000=0.500"),
         ("SPEEDS", "0.000=1.000=0.000=0,16.000=2.000=1.000=1")],
        [{"diff": "Hard", "meter": 9,
          "tags": [("OFFSET", "0.000"), ("BPMS", "0.000=200.000,4.000=100.000"),
                   ("WARPS", "6.000=1.500,20.000=8.000"), ("STOPS", "21.000=0.300"),
                   ("SCROLLS", "0.000=2.000")],
          "notes": join_measures(taps(8, 12, density=0.4))}]))

    # 19. .sm with #DELAYS (SM5 extension) and several charts incl. Edit.
    write("19_sm_delays_edit.sm", sm_file(
        head("SM delays", "0.000", "0=125", "10=0.2", [("DELAYS", "6=0.333,10=0.111")]),
        [{"diff": "Edit", "desc": "my edit", "meter": 4, "notes": join_measures(taps(4, 8))},
         {"diff": "Easy", "meter": 3, "notes": join_measures(taps(4, 4))}]))


if __name__ == "__main__":
    main()
