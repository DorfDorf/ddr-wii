# 05 — Development Roadmap

*Each milestone is small, has a visible result and retires a specific risk. The sequence is refined from your original six, based on what the existing code provides (doc 07) and where the risks really are (doc 06). The main changes: **(1) test the real dance pad on the real Wii early (M3)**, since that's the one thing Dolphin can't prove; **(2) build and test the chart parser and timing engine on the PC** in parallel with the Wii work; **(3) prove the audio clock in isolation before the rhythm engine depends on it.***

### Where the time actually goes (AI-assisted development)

Claude writes the code, the build scripts and the host-side tests, and can run the build and PC tests itself. So typing code is no longer the bottleneck. What sets the pace is the **human- and hardware-in-the-loop work**, and that's what each milestone lists under *Needs you*:

| Bottleneck | Why AI doesn't remove it |
|---|---|
| One-time installs and approvals | devkitPro, Dolphin, and softmodding the Wii (HBC + NAND backup) need your consent and, for the Wii, your hands |
| Real hardware | Only you can plug in the mat, stomp on panels, swap SD cards and watch the TV |
| Perception | "Does it *feel* in sync?" and "does the click line up with the flash?" are judged by you (with a phone slow-motion video as a check) |
| Real-time tests | A 5-minute soak test takes 5 minutes. An hour-long stability run takes an hour |
| Unknowns | Mat SI type, ASND clock behaviour, TV latency: each is an experiment whose result may change the plan |

Claude can also watch Dolphin directly (screenshots via computer use, or reading the log window) to check on-screen results in emulation without you. Real-hardware checks still need you.

```
M1 Toolchain ─► M2 Graphics (+ M2b 2P/4P stress) ─► M3 Pad test (Dolphin + real Wii) ─► M4b Audio clock ─► M4c Rhythm core ─► M5 Playable ─► M6 Hardware polish
                       M4a Chart parser + timing on PC (can start any time after M1) ──────────┘
     (optional) M1.5 Dance Clone reference build
```

---

## M1 — Build environment

| | |
|---|---|
| **Needs you** | Approve or run the devkitPro and Dolphin installs. A one-off look at Dolphin's Wii and controller settings |
| **Objective** | Install the toolchain and produce a `.dol` that runs in Dolphin |
| **Tools** | devkitPro installer + `wii-dev`, Dolphin, VS Code (doc 03 §1–5) |
| **Steps** | Build `wii-examples/templates/makefile` → run with `Dolphin.exe -e`. Then create `tools/padtest/` from that template with the doc 04 program and build it |
| **Output** | `template.dol` and `padtest.dol` |
| **Test** | Both boot in Dolphin. padtest shows 4 port lines |
| **Success** | Edit → `make` → Dolphin cycle in under 30 s, from the VS Code task |
| **Risks** | PATH/env issues in MSYS2 (fix: always build from the devkitPro MSys2 shell or bash `-lc`). Dolphin menu names differ from doc 03 **[mem]** |
| **Reuse** | Nothing from the old projects. The modern template replaces their 2012 Makefiles |

## M1.5 (optional) — Dance Clone reference build (timeboxed)

| | |
|---|---|
| **Needs you** | Little. Claude can patch a copy and build it. You decide whether to run the old v0.5 binary |
| **Objective** | Get a working reference DDR on modern tools, to compare input behaviour and feel |
| **Steps** | Copy `reference/extracted/danceclonewii/source/DanceClone-master` to `build-experiments/danceclone/` (never edit the original). `pacman -S` the Wii SDL 1.2 packages. Apply the patches listed in doc 07 §5.1: drop `-lSDL_net`, point the include/lib paths at `$(PORTLIBS)`, replace `boost::iequals`, and keep `mp3Buffer` alive until `StopMusic` (it's freed while playing). `dirData` and `SND_Init` compile as they are. Record every change in `PATCHES.md`. **Give it copies of simfiles only:** v0.68 rewrites `.sm` files that lack radar values |
| **Alternative** | Run the shipped v0.5 `boot.dol` in Dolphin (it's an untrusted binary, but Dolphin is sandboxed. You decide whether to run it). v0.5 plays its own demo song |
| **Success** | Boots in Dolphin and plays a hand-made test `.sm` (v0.68 can't read the v0.5 `.dc` demo). The chart needs a one-line `#BPMS`, a negative `#OFFSET`, no meter of 8, and only 4/8/16-row measures (doc 07 §3) |
| **Stop rule** | If it isn't building after one focused session, abandon it. It isn't on the critical path |

## M2 — Graphics

| | |
|---|---|
| **Needs you** | Almost nothing. Claude can build, launch Dolphin and check screenshots. You judge the look |
| **Objective** | GRRLIB rendering: background, four receptors, a moving arrow, text |
| **Tools** | GRRLIB (doc 03 §2) |
| **Output** | `game/` skeleton (doc 06 §9) showing a 640×480 scene. One arrow scrolls up based on `gettime()` |
| **Test** | Dolphin, NTSC and PAL60 settings. Frame-time overlay |
| **Success** | Steady 60 fps (16.7 ms frame), with ≥ 200 arrow sprites drawn without dropping frames |
| **Risks** | Texture format/alignment errors (GRRLIB handles them). Getting the link order right |
| **Reuse** | Placeholder arrow PNGs can be borrowed from Dance Clone `Media/Game/*.png` for local testing only (licence unclear, so don't ship them). Not needed in the end: all art is generated in code (`render/ArrowArt`) |
| **Status** | **Done.** `screens/RenderTest` (B from the M2b scene). **Dolphin** (2026-11-08): NTSC 480i, PAL60 480i and PAL50 528i all hold full refresh with 0 dropped frames at every step up to 1600 sprites. **Real Wii** (2026-11-08, PAL60 480p, photo of the TV): every step 0–1600 sprites averaged 16.68 ms per frame (max 16.7) with 0 dropped frames, and CPU busy time was 4.20 / 4.21 ms avg / max at 1600 sprites. That's 8× the 200-sprite target with about 75% of the frame to spare |

## M2b — Multi-playfield stress test (2 and 4 players)

| | |
|---|---|
| **Needs you** | Nothing in Dolphin. On the real Wii (with M3's setup): boot it, read the overlay, and look at the 4-player layout from where you'd stand to play |
| **Objective** | Find out whether 2 and 4 playfields hold full frame rate on real hardware, and whether 4P arrows are readable (doc 06 §11) |
| **Steps** | Add a mode to `RenderTest` that draws 1, 2 or 4 playfields from the doc 06 §11.3 layout table, each with a dense worst-case stream: 16ths at high scroll speed, long hold bodies, explosion and receptor-glow quads (large and alpha-blended), and per-player judgement, combo and score text in the bitmap font. Cycle 1 → 2 → 4 players automatically, like the existing sprite ramp, and show per step: worst frame time, dropped frames, CPU time from frame start to the vsync wait, draw count, texture switches and total drawn pixel area (doc 06 §11.5) |
| **Test** | Dolphin in NTSC, PAL60 and PAL50 first. It catches logic and layout bugs, and its CPU time and draw counts show where the budget goes (its CPU time is a lower bound; it says nothing about GPU cost). Then the **real Wii**, sent over Wi-Fi with `WIILOAD=tcp:<wii-ip> make -C game run` (no SD card swap). Only the hardware numbers decide |
| **Success** | 2P: no dropped frames on hardware, with headroom (worst frame < 12 ms of 16.7). 4P: no dropped frames on hardware, and you can read 36 px arrows at your playing distance. If 4P fails on either count, it stays 2P-only or tries the 16:9 layout |
| **Risks** | CPU cost per GRRLIB draw call and per texture switch (the likely limit; fixes are an arrow atlas and batched GX draws per playfield). Fill rate from large alpha-blended quads is estimated at ~10% of the GPU's ceiling, so it's unlikely. Text drawing cost |
| **Status** | Built (`game/source/screens/PlayfieldStress.*`, counters in `render/Draw.*`, GPU wait in `WiiVideo`). The game boots into it; B switches to the M2 scene. **Dolphin passed** (PAL60 480i). **Real Wii passed on performance** (2026-11-08, PAL60 480p): no dropped frames at any player count, and 4P uses under 3 ms of the 16.7 ms frame. **4P readability passed** at playing distance. **Decision:** ship 2P first; 4P is deferred until after M6 (doc 06 §11). The TV overscan finding below applies to every layout |

Dolphin results (2026-11-08; per frame, scene only; CPU time is a lower bound and GPU wait reads 0 in Dolphin):

| Players | Frame avg / max (ms) | CPU avg / max (ms) | Dropped | Draws | Texture switches | Drawn area (Mpx) |
|---|---|---|---|---|---|---|
| 1 | 16.68 / 16.68 | 0.99 / 1.00 | 0 | 107 | 17 | 0.77 |
| 2 | 16.68 / 16.68 | 1.19 / 1.20 | 0 | 190 | 34 | 1.21 |
| 3 | 16.68 / 16.68 | 1.61 / 1.62 | 0 | 368 | 62 | 0.98 |
| 4 | 16.68 / 16.68 | 1.88 / 1.89 | 0 | 483 | 82 | 1.19 |

The drawn area is below doc 06's 1.5 Mpx estimate. The 3–4P fields draw more sprites but smaller ones, so area doesn't grow with player count.

**Real Wii results** (2026-11-08; PAL60 480p over a progressive-scan connection to a Sony Bravia; read from a photo of the TV, and the TV's overscan cut off the bottom of the 4P row):

| Players | Frame avg / max (ms) | CPU avg / max (ms) | GPU wait avg / max (ms) | Dropped | Draws | Texture switches | Drawn area (Mpx) |
|---|---|---|---|---|---|---|---|
| 1 | 16.68 / 16.68 | 0.82 / 0.83 | 0.93 / 1.16 | 0 | 109 | 17 | 0.77 |
| 2 | 16.68 / 16.68 | 0.99 / 1.01 | 1.28 / 1.52 | 0 | 194 | 34 | 1.21 |
| 3 | 16.68 / 16.68 | 1.35 / 1.36 | 0.89 / 1.12 | 0 | 368 | 62 | 0.97 |
| 4 | 16.68 / 16.68 | 1.58 / 1.59 | 0.98 / 1.21 | 0 (partly cut off) | 483 | 82 | 1.19 |

What it shows:
- **The hardware is faster than Dolphin's CPU estimate** (4P: 1.58 ms vs 1.88 ms), so Dolphin's CPU numbers are a safe upper guide for this code, not a lower bound.
- **GPU wait follows drawn area, not sprite count.** It peaks at 2P (1.21 Mpx, the biggest sprites) and is about 1 ms elsewhere.
- **4P costs under 3 ms of CPU + GPU per 16.7 ms frame**, about 5× headroom. Neither draw calls nor fill rate limits 4 players. Gameplay will add judging and audio decoding (doc 06 §4) on top, which this headroom covers comfortably.
- **TV overscan crops the picture.** On this TV the left edge of P1's left lane, the top of the life bars and the bottom ~2 rows of the overlay are cut off. The 4P layout fills the full 640 px width, so it must move inside a safe area (doc 06 §11.3).

## M3 — Controller input: Dolphin **and** the real Wii

| | |
|---|---|
| **Needs you** | **This is the main gate.** Softmod the Wii if it isn't already (follow wii.hacks.guide, make a NAND backup), put padtest on the SD card, stand on the mat and read the screen back to Claude (or send a photo) |
| **Objective** | Prove the GameCube dance mat reaches our code correctly, including jumps and timestamps |
| **Tools** | `padtest.dol`, the Dolphin keyboard profile (doc 03 §4.4), **Wii with HBC** (doc 03 §6), SD card or wiiload |
| **Steps** | (a) Dolphin: keyboard-mapped Standard Controller. (b) Real Wii: mat in port 1, run padtest via HBC. (c) Optional: the mat via a Wii U GC adapter in Dolphin. (d) If a second mat (or any GC controller) is available: plug it into port 2 and check that both ports report independently, with no change to the sampling interval. Repeat with all four ports filled if you have the devices. Also run the M2b stress test on the hardware |
| **Output** | A recorded table: mat SI type, per-panel bits, max simultaneous panels, PAD sample interval min/max, and how long each panel bounces on press and release. That sets the debounce window; ITGmania defaults to 20 ms for pads |
| **Success** | Mat shows `GC standard (PAD ok)`. All 4 arrows map correctly. L+R and U+D jumps register. Releases clear within one frame. Sampling interval measured |
| **Risks** | Mat reports a non-standard SI type (low likelihood, doc 04 §3; fallback is raw SI reads). The keyboard can't hold some 3-key combinations (a hardware limit). Installing HBC (follow wii.hacks.guide, make a NAND backup) |
| **Reuse** | Dance Clone `WiiInput.cpp` approach (reference) |
| **Then** | Integrate as `platform/wii/WiiInput` with timestamped, debounced `LaneEvent`s (doc 04 §8, doc 06 §3.4), tagged by port with debounce state per port. Show lane states on the M2 scene, one playfield per connected port. Add held-miss logging for stuck-panel diagnosis |
| **Status** | **Mostly passed on the real Wii** (2026-11-08, padtest via HBC, photos of the TV). Mat in port 1: `SI=0x09000300 GC standard (PAD ok)`. Arrows arrive as D-pad bits (`held=0x0003` = L+R, `0x000C` = D+U). All 4 panels map correctly, `max-at-once` reached 4 (L+R and U+D jumps both register), and there was no sticking or flicker on release. **Open:** (1) the timestamp hook. `PAD_SetSamplingCallback` is a stub in libogc 3.2.1 (doc 04 §5), so padtest now uses `SI_RegisterPollingHandler`. Dolphin gives ~119 samples/s; the real-Wii rate and interval need one more padtest run. (2) All four ports reported `[conn]` with the same `0x09000300` on the Wii, while Dolphin's empty ports read `0x00000008 none`. Check whether other devices were plugged in. If not, `PAD_ScanPads`' connected mask can't be trusted for joining, and players must join by pressing START (doc 06 §11.2). (3) Panel bounce duration (debounce window) isn't measured yet. **Then-step built** (branch `m3-wii-input`): `platform/wii/WiiInput` reads all ports from the SI polling interrupt into a lock-free ring, debounces per port with `core/input/InputFilter` (20 ms, PC-tested), and gives timestamped `LaneEvent`s plus held/down masks; the scenes use it instead of `PAD_*`. Dolphin (keyboard): 120 samples/s, gaps 1.0–15.6 ms, 0 lost, all four lanes map correctly, a press is read ~0.6 ms after it was sampled. **Hardware check pending:** the render test's `input` line shows the real-Wii sample rate |

## M4a — Chart parser + timing engine on the PC (can run in parallel)

| | |
|---|---|
| **Needs you** | Nearly nothing. This is fully automatable: Claude writes the parser, the oracle script and the tests, and runs them. You can supply a few real simfiles from your packs |
| **Objective** | Load `.sm` and `.ssc` correctly, and convert beat ↔ seconds exactly |
| **Tools** | PC compiler (MSVC, or MinGW g++ from MSYS2), Python `simfile` (MIT) as the oracle |
| **Steps** | Port the SM5-era `MsdFile`, the `TimingData` algorithms (`GetBeatInternal`, `GetElapsedTimeInternal`, `GetDisplayedBeat`), the `NotesLoaderSM` tag handling with `ProcessBPMsAndStops`, and `NotesLoaderSSC` from **ITGmania tag `v1.3.0`** (`itgmania/itgmania`, GPLv3, matching this project's licence) into `game/source/core/`. Keep the original notices, add our GPL-3.0-or-later header, and note what we changed. Skip the Lua, editor and cache code. Use StepMania 3.9 (`stepmania/stepmania` tag `v390release`) as the small, readable reference. Port `pack.ini` (`Group`) parsing with its `SyncOffset`. Write `tools/simfile-oracle.py`, which dumps `(beat, seconds)` for every note, and compare in `game/tests/` |
| **Test corpus** | ~20 charts: constant BPM, many BPM changes, stops, delays (.ssc), split timing, odd whitespace/comments, all quantisations, holds, mines. Include `garcia/simfile`'s testdata |
| **Success** | Every note time matches the oracle within 0.1 ms. No crashes on malformed files (fuzz with truncated files) |
| **Risks** | SSC edge cases (spec page "in progress"). Mitigated by the oracle |
| **Reuse** | **High** (ITGmania `v1.3.0` code, GPLv3; SM 3.9 as reference) |

## M4b — Audio clock experiment

| | |
|---|---|
| **Needs you** | Listen and watch: does the click line up with the flash in Dolphin and on the Wii? Run the 5-minute soak on hardware. Iterations depend on what the measurements show |
| **Objective** | Stream a song through ASND and prove that `SongClock` is smooth, monotonic and drift-free |
| **Tools** | libogc ASND, `ppc-libvorbisidec` and/or `ppc-libmad` |
| **Steps** | Start with **WAV** (no decoder) at 44.1 and 48 kHz → then OGG via Tremor → then MP3 via libmad. Implement the ring buffer, decode thread and voice callback (doc 06 §4). Show on screen: `SongClock::now()`, `ASND_GetTimerVoice(0)`, the difference between them, and buffer underruns. Add a metronome test: flash the screen and play a click on every beat of a 120 BPM WAV |
| **Success** | No underruns over a 5-minute track. Clock jitter < 1 ms (frame to frame). Drift against `ASND_GetTimerVoice` < 2 ms over 5 min. Visual flash and audible click line up in Dolphin and on the Wii (judged by eye/ear, then with a phone slow-motion video) |
| **Risks** | **The biggest technical risk in the project** (see the final answer in the summary). ASND callback timing semantics. Decoder CPU cost. Dolphin audio latency differs from hardware |
| **Reuse** | The double-buffered voice + fill-thread shape of StepMania-Wii's `RageSoundDriver_Wii.cpp`, re-implemented from the asndlib docs. Don't copy code: the file has no licence, the project may be GPLv2-only (which can't be combined with our GPLv3), and its position logic is broken (doc 07 §6 row 6) |
| **Status** | **WAV step built and passing in Dolphin** (branch `m4b-audio-clock`, 2026-11-08). `tools/make-click-track.py` writes 5-minute 120 BPM click tracks (48 and 44.1 kHz, stereo); `tools/stage-sd.sh` puts them in `sd:/ddrwii/test/`. `platform/wii/WiiAudio` streams the WAV from SD through ASND voice 0 (16 × 2048-frame ring, fill thread). `core/timing/SongClock` low-pass-maps position reports onto `gettime()` (PC-tested with simulated 21.3 ms jitter and 100 ppm drift: frame jitter 0.2 ms, error band 3.9 ms). The boot scene `AudioClockTest` flashes on every beat from SongClock. **First version** drove SongClock from buffer-finished reports, and SongClock − ASND tick counter came out −10.7 ms at 48 kHz but −21.6 ms at 44.1 kHz. Cause (from asndlib.c): a buffer report comes one tick after ASND switches buffers. 2048 frames is exactly 2 ticks at 48 kHz, so 48 kHz reports are always 1 tick late, while 44.1 kHz buffers end mid-tick and are 1.5 ticks late on average. **Fix:** SongClock now follows ASND's tick counter, timestamped every tick by `ASND_SetCallback`, which is the same at every rate. Buffer reports are now the cross-check (`buffers-clock`: file content played − SongClock, 10 s averages). **Dolphin results** (keyboard, PAL60, after the fix): 48 kHz: 0 underruns, ring never below 13/16, SD read max 0.5 ms, frame jitter 0.00 ms, buffers-clock 0.00 ms, drift 0.00 ms (the first version also ran the full 5 min with 0 underruns). 44.1 kHz (70 s): 0 underruns, jitter 0.00 ms, buffers-clock −10.5 ms (the half-tick, as predicted), drift −0.14…0.00 ms. **OGG step built** (same branch): `WiiAudio` reads a `.ogg` into memory and decodes it with Tremor in the fill thread (`tools/stage-sd.sh` makes `click120-44k.ogg` from the WAV with ffmpeg). Dolphin, 30 s: 0 underruns, 0 dropped frames, jitter 0.00 ms, drift −0.12…0.00 ms, buffers-clock −10.53 ms (identical to the 44.1 kHz WAV, so Vorbis adds no start offset), decode 2.0% CPU with the slowest 2048-frame chunk at 6.5 ms. Dolphin's CPU timing isn't the Wii's, so decode cost and dropped frames need the hardware. **MP3 step built** (same branch): libmad, whole file in memory. It plays MP3s **the way ITGmania v1.3.0 does** (`RageSoundReader_MP3.cpp` [src]), because simfile offsets were synced against it. There's no gapless trimming: the LAME encoder delay and libmad's 529-sample delay stay in. A first frame tagged `Xing` (VBR) is skipped, but one tagged `Info` (CBR) is played as audio. The screen shows the resulting start delay. Dolphin, 30 s of `click120-44k.mp3` (ffmpeg libmp3lame 192k CBR, so an ID3 tag and an Info frame): 0 underruns, 0 dropped frames, jitter 0.00 ms, drift −0.14…0.00 ms, decode 4.8% CPU (slowest chunk 3.4 ms). Start delay 51.2 ms = 1152 (Info) + 576 (LAME) + 529 (libmad) frames, so on this track the click should come about 51 ms **after** the flash, on purpose. **Open:** on the Wii, click vs flash by ear and eye for all four tracks (MP3 late by ~51 ms, the others together), decode CPU and dropped frames for OGG/MP3, and a 5-minute soak. Then M4b is done. **Real Wii, first build** (2026-11-08, commit `0d5e596`, buffer-report SongClock, Sony Bravia, PAL60 480p, photos of the TV): 48 kHz at 4:06: 0 underruns, ring min 13/16, SD read max 5.3 ms, frame jitter 0.00 ms, drift −0.29…+0.40 ms, SongClock − ASND −31.1 ms. 44.1 kHz at 3:06: 0 underruns, ring min 13/16, SD read max 4.6 ms, jitter 0.19 ms, drift −0.01…+0.89 ms, SongClock − ASND −20.9 ms. So streaming and drift pass on hardware. The rate-dependent offset shows up there too, but **reversed** (48 kHz ~10 ms later than 44.1 kHz; Dolphin had 44.1 kHz later), which confirms that buffer-report timing depends on interrupt order and shouldn't drive the clock. The tick-driven build (`2c77f2c` and later) still needs its hardware run |

## M4c — Basic rhythm engine

| | |
|---|---|
| **Needs you** | Play test songs (keyboard in Dolphin, then the mat on the Wii) and report how it feels. Autoplay correctness can be checked without you |
| **Objective** | One hard-coded song: scrolling arrows in sync, hits judged, basic score |
| **Steps** | Load one `.sm` (M4a) + its audio (M4b). Render notes by beat (doc 06 §3.2). Judge with timestamped LaneEvents (§3.4). Show judgement, ms error, combo and score. Results screen. Build judging, score and rendering **per player** from the start (doc 06 §11.1), with players bound by port |
| **Test** | Play a simple 4th-note chart with the keyboard in Dolphin, then with the mat on the Wii. Use the "autoplay" debug mode (perfect inputs generated from the chart): every judgement must be Marvelous. Run 2P with the human as P1 and autoplay as P2 on a different difficulty |
| **Success** | Autoplay gives 100% Marvelous. A human gets consistent ms error (a stable mean, which is the latency to calibrate away). Holds work. A human's judgements are the same with autoplay players on screen as without |
| **Risks** | Input-to-song-time conversion bugs (caught by autoplay) |
| **Reuse** | Dance Clone judgement/jump logic as reference. ITGmania's early-hit rule (doc 06 §3.4), ported from `v1.3.0` |
| **Status** | **Step 1 of 4 built** (branch `m4c-judge`, 2026-11-08): `core/game/Judge` judges one player's chart and keeps their score, PC-tested. It has the doc 06 §3.4 windows, the closest unjudged note per lane, the early-hit rule (on by default), jumps counted once with their worst grade, holds (0.25 s release tolerance; rolls are treated as holds for now), held misses, combo (Good breaks it), dance points and the mean/spread of ms error. Mines, lifts and fakes are not judged yet. `MakeAutoplay` turns a chart into perfect presses: **42/42 corpus charts give 100% Marvelous** (15 107 notes, 613 holds). **Step 2 built** (branch `m4c-test-song`): `tools/make-test-song.py` writes "Click 120", a 120 BPM `.sm` on the OGG click track with `#OFFSET:0`. It has Beginner (quarters), Easy (quarters, jumps, holds) and Medium (eighths, jumps, holds) charts, with notes from 0:04 to about 2:12. Every note lands on the click grid by construction, so the mean ms error a human gets on it is pure latency. `tools/stage-sd.sh` puts it in `sd:/ddrwii/songs/ddr-wii tests/Click 120/`. A copy is in the test corpus as `synthetic/20`: it matches the oracle exactly, autoplay gives 100% Marvelous on it, and a test checks the grid. **Step 3 built** (branch `m4c-gameplay`): `screens/Gameplay` is now the boot scene. It loads Click 120 from the SD card and plays its OGG through `WiiAudio`. SongClock follows ASND's tick, as in M4b. Notes scroll by beat, coloured by quantisation, and hold bodies follow the receptor while held. Steps come from `wii::Input`'s timestamped `LaneEvent`s, mapped to song time through SongClock, then the global offset. The screen shows the judgement, the ms error (early/late), combo, %, the counts, holds OK/NG, and the mean and spread of the ms error. Each player has their own chart, `Judge` and autoplay and is bound to a port (one player for now). On the ready screen, Up/Down picks the chart, Z toggles autoplay and Left/Right sets the offset in 5 ms steps. **Dolphin** (2026-11-08, PAL60 480i, keyboard): autoplay Medium 100.00% (480 Marvelous, max combo 480, 16 holds OK, mean +0.0 ms) and Easy all Marvelous, with 0 dropped frames and 0 underruns. Keyboard play on Beginner was judged per step, with a mix of grades and errors from the scripted keypresses. **Step 4 built** (branch `m4c-results-2p`): 2P and the results screen. P2 joins on the ready screen with START on port 2 (human), or with Y on port 1 as an autoplay P2 (X changes its chart). The playfields move to x = 160 / 480 (doc 06 §11.3), and each player gets their own chart, autoplay, %, judgement and combo. The results screen shows each player's column: %, judgement counts, holds held / let go, max combo, mean and spread of the ms error, and held misses. **Dolphin** (2026-11-08, PAL60 480i): P1 with no input on Beginner and P2 on autoplay Easy ran the whole song. Results: P1 0.00% (256 Miss), P2 100.00% (224 Marvelous, 16 held, max combo 224), with 0 dropped frames. **Hardware still open** (the M4c success criteria): play Click 120 on the mat, check that it feels in sync and the mean ms error is stable from run to run, and that holds work. Then play with an autoplay P2 on screen and check your judgements don't change. **Code review of M4a–M4c** (2026-11-08, after step 4): no correctness bugs found in the judge, the clock or the audio ring; one robustness fix (`WavFile` chunk walk uses 64-bit offsets). **Open items for M5**, each with the step it belongs to: (1) if the audio ends before the chart, SongClock holds and the leftover notes are never judged (the song ends on `AudioStream::finished`); keep chart time running on the CPU clock after the audio ends → step 2, with real songs. (2) Mines and rolls aren't judged → step 4, with the life bar (a mine is hit when its lane is down at its time; a roll needs a re-press about every 0.3 s). Lifts stay unjudged until a pack needs them. (3) P1 is hard-wired to port 1 and P2 to port 2 → the join-order rule of doc 06 §11.2 goes in song select, step 2, which then hands Gameplay its player list. (4) Dance points use doc 06 §7's simple weights → switch to ITG's **[mem]** (Marvelous 5, Perfect 4, Great 2, Good 0, Boo −6, Miss −12, held 5, let go 0, mine −6) and its letter grades in step 5, before any score is saved. (5) OGG/MP3 files are read whole into MEM1 with `malloc` → a small MEM2 heap for song files in step 2, checked after 20 song loads |

## M5 — Playable prototype

| | |
|---|---|
| **Needs you** | Copy your song packs to the SD card, play many sessions, report crashes and bad charts. Most of the code is routine for Claude |
| **Objective** | A real game loop over many songs |
| **Features** | SongLibrary scan + cache (with `pack.ini` titles, banners and `SyncOffset`), song wheel with banners and preview audio, difficulty select (Beginner…Challenge), scroll speed option, life bar + fail, results with grade, high scores saved to SD, **offset calibration screen**, `config.ini`. **2-player mode:** Start-to-join per port, per-player difficulty and scroll speed, side-by-side results (doc 06 §11). Layouts inside the overscan safe area (doc 06 §11.3) |
| **Test** | 50+ songs from your own StepMania packs (a mix of .sm and .ssc, OGG and MP3) |
| **Success** | Start → choose song → play → results → back, repeated 20 times without a crash or leak (watch the heap display). Library scan < 5 s with the cache |
| **Risks** | SD scan speed. Memory fragmentation from repeated song loads (fix: fixed MEM2 arenas). Odd simfiles |
| **Reuse** | Dance Clone song-menu ideas (reference) |
| **Status** | **Steps 1–2 of 6 built.** **Step 2** (branch `m5-song-select`, 2026-11-08): `screens/SongSelect`, laid out after ITGmania's Simply Love theme (doc 06 §7), is the boot scene. The library is scanned once at boot. START joins a port as the next player (doc 06 §11.2), Left/Right move the wheel, Up/Down pick each player's difficulty, START opens a pack or plays; the players and song go to Gameplay in a `Session`, and the results lead back to the select screen at the same wheel position. M4c open items 1, 3 and 5 are done: SongClock has a free-run mode, so chart time keeps going on the CPU clock after the audio ends (PC-tested); the player list comes from song select; OGG/MP3 files are read into one fixed 24 MB buffer at the top of MEM2, reserved once and reused for every song. **Dolphin** (2026-11-08, PAL60 480i): scan 899 ms for 33 songs; P1 and an autoplay P2 on Artifacts Medium 11 (stopped at 25 s, results shown, back to select), then DNA Medium 5 to the end: both 100.00% (249 Marvelous, 16 held), 0 dropped frames, 0 underruns; B still reaches the test scenes. **Hardware still open:** the 20-song-loads check of the MEM2 buffer (watch for slowdowns or a crash), the scan time on the real card, and select-screen text inside the TV's safe area. **Step 1** (branch `m5-song-library`, 2026-11-08): `core/songs/SongLibrary` scans `<root>/<pack>/<song>/` with `dirent` (newlib and the PC both have it), picks the `.ssc` before the `.sm`, reads `Pack.ini` (any case) for titles and `SyncOffset`, and keeps each song's folder, simfile, music file (`#MUSIC` matched ignoring case, else the first OGG/MP3/WAV), banner (`#BANNER`, else a file named like `bn.png` or `banner.png`), titles and its dance-single charts sorted by difficulty. Songs without a simfile, a dance-single chart or an audio file are skipped with a warning; packs with no playable song are dropped. Packs sort by `SortTitle`, songs by transliterated title, subtitle, then folder. PC-tested on the fake tree in `game/tests/library/`. The starting library is the owner's ITGmania Songs folder (Club Fantastic Seasons 1 and 2, 32 songs, all `.ssc` + OGG, no `Pack.ini`): `tools/stage-sd.sh` copies every pack in `$SONG_PACKS` (default `C:\Games\ITGmania\Songs`) into `ddrwii/songs/`. Until song select existed, Gameplay scanned at start and L/R on port 1 stepped through the songs; the pack's `SyncOffset` is applied on load. **Dolphin** (2026-11-08, PAL60 480i): 3 packs, 33 songs, 0 skipped, scan 928 ms; BACK UP (Club Fantastic S1) Medium on autoplay 100.00% (249 Marvelous, 28 held), 0 dropped frames, 0 underruns. **Hardware:** time the scan on the real card (top right of song select). Planned (2026-11-08) as six small PRs, each tested in Dolphin before the next: (1) **song library**: scan `sd:/ddrwii/songs/<pack>/<song>/` for `.sm`/`.ssc`, read `pack.ini` titles and `SyncOffset`, sort by pack then title; portable in `core/`, PC-tested on a fake folder tree, then timed on the real card. (2) **song select → gameplay**: a plain text list by pack, difficulty per player, START-to-join by port (doc 06 §11.2), then the existing gameplay screen with any OGG or MP3. Also here: a MEM2 heap for song files, and chart time that keeps running after the audio ends (M4c open items 1, 3, 5). (3) **`config.ini` + offset calibration screen** on the click track (doc 06 §3.5). (4) **life bar and fail**, scroll speed, mines and rolls (M4c item 2). (5) **high scores** on SD, written to a temp file then renamed, with ITG's dance-point weights and letter grades (M4c item 4) before anything is saved. (6) **polish**: banners, preview audio, and the library cache if the scan is slow. After step 2 the owner's own packs play on the Wii, which is the best point for a hardware session |

## M6 — Physical Wii validation and polish (ongoing)

| | |
|---|---|
| **Needs you** | Entirely hands-on: calibration on your TV, long play sessions |
| **Objective** | Feels right on your TV with your mat |
| **Steps** | Package `apps/ddrwii/` (boot.dol, meta.xml, icon.png) + `ddrwii/songs/`. Calibrate the global offset on your TV (Game mode on). Run a long session (an hour of play). Check PAL/NTSC/480p if relevant. Play real 2-player sessions on two mats. Optionally test a second mat model, and set a per-port `InputOffset` if two mats measure differently |
| **Success** | Calibrated mean error within ±5 ms. No audio dropouts. No missed jumps. Stable for an hour. 2P sessions hold frame rate with both mats in play |
| **Risks** | TV latency (calibration), SD card quality, mat wear (dead panels look like software bugs, so padtest helps tell them apart) |

---

## Acceptance checklist (quick view)

| Milestone | Done when… |
|---|---|
| M1 | `padtest.dol` builds from VS Code and boots in Dolphin |
| M2 | 60 fps scene with receptors + 200 sprites |
| M2b | 2 playfields hold frame rate on the real Wii. 4P decided (frame rate + readability). **Done:** frame rate and readability passed for 1–4P on hardware; 2P now, 4P after M6 |
| M3 | Real mat on the real Wii: 4 arrows + jumps confirmed, SI type recorded |
| M4a | Parser/timing match `simfile` within 0.1 ms on the test corpus |
| M4b | 5-minute song, no underruns, clock jitter < 1 ms, drift < 2 ms |
| M4c | Autoplay = 100% Marvelous. Human play feels in sync after offset |
| M5 | Full loop over 50+ songs, high scores persist |
| M6 | Calibrated, stable, fun on your TV |
| Later | 4 players: 3–4P layout inside the safe area, join for ports 3–4, four-mat play test (doc 06 §11) |
