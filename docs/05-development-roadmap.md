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
M1 Toolchain ─► M2 Graphics ─► M3 Pad test (Dolphin + real Wii) ─► M4b Audio clock ─► M4c Rhythm core ─► M5 Playable ─► M6 Hardware polish
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
| **Steps** | Copy `reference/extracted/danceclonewii/source/DanceClone-master` to `build-experiments/danceclone/` (never edit the original). `pacman -S` the Wii SDL 1.2 packages. Apply the patches listed in doc 07 §5.1 (drop `-lSDL_net`, add portlibs paths, remove the `dirData` check, `SND_Init` → `ASND_Init`, replace `boost::iequals`) and record every change in `PATCHES.md` |
| **Alternative** | Run the shipped v0.5 `boot.dol` in Dolphin (it's an untrusted binary, but Dolphin is sandboxed. You decide whether to run it) |
| **Success** | Boots in Dolphin and plays its demo song |
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
| **Reuse** | Placeholder arrow PNGs can be borrowed from Dance Clone `Media/Game/*.png` for local testing only (licence unclear, so don't ship them) |

## M3 — Controller input: Dolphin **and** the real Wii

| | |
|---|---|
| **Needs you** | **This is the main gate.** Softmod the Wii if it isn't already (follow wii.hacks.guide, make a NAND backup), put padtest on the SD card, stand on the mat and read the screen back to Claude (or send a photo) |
| **Objective** | Prove the GameCube dance mat reaches our code correctly, including jumps and timestamps |
| **Tools** | `padtest.dol`, the Dolphin keyboard profile (doc 03 §4.4), **Wii with HBC** (doc 03 §6), SD card or wiiload |
| **Steps** | (a) Dolphin: keyboard-mapped Standard Controller. (b) Real Wii: mat in port 1, run padtest via HBC. (c) Optional: the mat via a Wii U GC adapter in Dolphin |
| **Output** | A recorded table: mat SI type, per-panel bits, max simultaneous panels, PAD sample interval min/max |
| **Success** | Mat shows `GC standard (PAD ok)`. All 4 arrows map correctly. L+R and U+D jumps register. Releases clear within one frame. Sampling interval measured |
| **Risks** | Mat reports a non-standard SI type (low likelihood, doc 04 §3; fallback is raw SI reads). The keyboard can't hold some 3-key combinations (a hardware limit). Installing HBC (follow wii.hacks.guide, make a NAND backup) |
| **Reuse** | Dance Clone `WiiInput.cpp` approach (reference) |
| **Then** | Integrate as `platform/wii/WiiInput` with timestamped `LaneEvent`s (doc 04 §8). Show lane states on the M2 scene |

## M4a — Chart parser + timing engine on the PC (can run in parallel)

| | |
|---|---|
| **Needs you** | Nearly nothing. This is fully automatable: Claude writes the parser, the oracle script and the tests, and runs them. You can supply a few real simfiles from your packs |
| **Objective** | Load `.sm` and `.ssc` correctly, and convert beat ↔ seconds exactly |
| **Tools** | PC compiler (MSVC, or MinGW g++ from MSYS2), Python `simfile` (MIT) as the oracle |
| **Steps** | Port `MsdFile.cpp`, the `NotesLoaderSM.cpp` logic and `TimingData` from `reference/extracted/stepmaniawii/stepmania-wii/trunk/` into `game/source/core/` (keep the MIT notices). Add the SSC loader, DELAYS and WARPS. Write `tools/simfile-oracle.py`, which dumps `(beat, seconds)` for every note, and compare in `game/tests/` |
| **Test corpus** | ~20 charts: constant BPM, many BPM changes, stops, delays (.ssc), split timing, odd whitespace/comments, all quantisations, holds, mines. Include `garcia/simfile`'s testdata |
| **Success** | Every note time matches the oracle within 0.1 ms. No crashes on malformed files (fuzz with truncated files) |
| **Risks** | SSC edge cases (spec page "in progress"). Mitigated by the oracle |
| **Reuse** | **High** (StepMania 3.9 algorithms) |

## M4b — Audio clock experiment

| | |
|---|---|
| **Needs you** | Listen and watch: does the click line up with the flash in Dolphin and on the Wii? Run the 5-minute soak on hardware. Iterations depend on what the measurements show |
| **Objective** | Stream a song through ASND and prove that `SongClock` is smooth, monotonic and drift-free |
| **Tools** | libogc ASND, `ppc-libvorbisidec` and/or `ppc-libmad` |
| **Steps** | Start with **WAV** (no decoder) at 44.1 and 48 kHz → then OGG via Tremor → then MP3 via libmad. Implement the ring buffer, decode thread and voice callback (doc 06 §4). Show on screen: `SongClock::now()`, `ASND_GetTimerVoice(0)`, the difference between them, and buffer underruns. Add a metronome test: flash the screen and play a click on every beat of a 120 BPM WAV |
| **Success** | No underruns over a 5-minute track. Clock jitter < 1 ms (frame to frame). Drift against `ASND_GetTimerVoice` < 2 ms over 5 min. Visual flash and audible click line up in Dolphin and on the Wii (judged by eye/ear, then with a phone slow-motion video) |
| **Risks** | **The biggest technical risk in the project** (see the final answer in the summary). ASND callback timing semantics. Decoder CPU cost. Dolphin audio latency differs from hardware |
| **Reuse** | StepMania-Wii `RageSoundDriver_Wii.cpp` structure (re-implemented, MIT) |

## M4c — Basic rhythm engine

| | |
|---|---|
| **Needs you** | Play test songs (keyboard in Dolphin, then the mat on the Wii) and report how it feels. Autoplay correctness can be checked without you |
| **Objective** | One hard-coded song: scrolling arrows in sync, hits judged, basic score |
| **Steps** | Load one `.sm` (M4a) + its audio (M4b). Render notes by beat (doc 06 §3.2). Judge with timestamped LaneEvents (§3.4). Show judgement, ms error, combo and score. Results screen |
| **Test** | Play a simple 4th-note chart with the keyboard in Dolphin, then with the mat on the Wii. Use the "autoplay" debug mode (perfect inputs generated from the chart): every judgement must be Marvelous |
| **Success** | Autoplay gives 100% Marvelous. A human gets consistent ms error (a stable mean, which is the latency to calibrate away). Holds work |
| **Risks** | Input-to-song-time conversion bugs (caught by autoplay) |
| **Reuse** | Dance Clone judgement/jump logic as reference |

## M5 — Playable prototype

| | |
|---|---|
| **Needs you** | Copy your song packs to the SD card, play many sessions, report crashes and bad charts. Most of the code is routine for Claude |
| **Objective** | A real game loop over many songs |
| **Features** | SongLibrary scan + cache, song wheel with banners and preview audio, difficulty select (Beginner…Challenge), scroll speed option, life bar + fail, results with grade, high scores saved to SD, **offset calibration screen**, `config.ini` |
| **Test** | 50+ songs from your own StepMania packs (a mix of .sm and .ssc, OGG and MP3) |
| **Success** | Start → choose song → play → results → back, repeated 20 times without a crash or leak (watch the heap display). Library scan < 5 s with the cache |
| **Risks** | SD scan speed. Memory fragmentation from repeated song loads (fix: fixed MEM2 arenas). Odd simfiles |
| **Reuse** | Dance Clone song-menu ideas (reference) |

## M6 — Physical Wii validation and polish (ongoing)

| | |
|---|---|
| **Needs you** | Entirely hands-on: calibration on your TV, long play sessions |
| **Objective** | Feels right on your TV with your mat |
| **Steps** | Package `apps/ddrwii/` (boot.dol, meta.xml, icon.png) + `ddrwii/songs/`. Calibrate the global offset on your TV (Game mode on). Run a long session (an hour of play). Check PAL/NTSC/480p if relevant. Optionally test a second mat model |
| **Success** | Calibrated mean error within ±5 ms. No audio dropouts. No missed jumps. Stable for an hour |
| **Risks** | TV latency (calibration), SD card quality, mat wear (dead panels look like software bugs, so padtest helps tell them apart) |

---

## Acceptance checklist (quick view)

| Milestone | Done when… |
|---|---|
| M1 | `padtest.dol` builds from VS Code and boots in Dolphin |
| M2 | 60 fps scene with receptors + 200 sprites |
| M3 | Real mat on the real Wii: 4 arrows + jumps confirmed, SI type recorded |
| M4a | Parser/timing match `simfile` within 0.1 ms on the test corpus |
| M4b | 5-minute song, no underruns, clock jitter < 1 ms, drift < 2 ms |
| M4c | Autoplay = 100% Marvelous. Human play feels in sync after offset |
| M5 | Full loop over 50+ songs, high scores persist |
| M6 | Calibrated, stable, fun on your TV |
