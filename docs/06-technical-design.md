# 06 — Technical Design: Lightweight Wii Rhythm Engine

*Proposed design for Option D (Hybrid) from [02](02-architecture-options.md). It's deliberately small. Anything marked **[exp]** must be validated in the milestone that introduces it ([05](05-development-roadmap.md)).*

---

## 1. Goals and non-goals (prototype)

**Goals:** a single-player four-lane `dance-single` game that reads `.sm`/`.ssc` from SD, plays OGG/MP3, scrolls arrows in sync, judges taps and holds, shows a combo and score, and saves high scores. It runs in Dolphin and on a real Wii with a GameCube dance mat.

**Non-goals (for now):** background videos, BGCHANGES, mods and attacks, Lua or themes, courses, doubles, online play, editing. All of these can be added later without changing the core.

## 2. Threads and main loop

```
 ┌──────────── main thread (LWP prio normal) ────────────┐   ┌── decode thread (higher prio) ──┐
 │ loop:                                                  │   │ loop:                           │
 │   now   = SongClock::now()          // audio-derived   │   │   while ring has space:         │
 │   events= Input::drain()           // timestamped     │   │     decode N frames (libmad/    │
 │   screen->update(now, events)      // judge, menus    │   │       Tremor) -> ring buffer    │
 │   screen->render(now)              // GRRLIB          │   │   sleep until ring < low-water  │
 │   GRRLIB_Render()                  // waits vsync     │   └─────────────────────────────────┘
 └────────────────────────────────────────────────────────┘
 IRQ context:  ASND voice callback -> hand next ring chunk to DSP, advance framesConsumed
               PAD sampling callback -> diff buttons, push timestamped LaneEvents
```

- Update and render run **once per video frame** (60 or 50 Hz). Because everything is computed from **time**, PAL and NTSC behave the same.
- Nothing is allocated per frame during gameplay. Charts, textures and audio buffers are all prepared before the song starts.
- Dance Clone's single-loop structure (`main.cpp`) shows the per-frame flow works on the Wii. We improve on it by not tying timing to the loop.

## 3. Timing engine (the critical part)

### 3.1 Clocks

| Clock | Source | Use |
|---|---|---|
| CPU timer | libogc `gettime()` (time base ticks), `diff_usec()`, `ticks_to_microsecs()` **[src: used by StepMania-Wii `ArchHooks_Wii.cpp:68-72`]** | Timestamps for inputs and the audio callbacks. Smoothing |
| Audio position | Frames handed to the DSP and completed, counted in our ASND voice callback. Cross-checked with `ASND_GetTimerVoice(voice)` (ms since the voice started) and `ASND_GetSampleCounter()` **[src: asndlib.h]** | **The master song clock** |

**SongClock** keeps a linear map `audioSeconds ≈ a·cpuSeconds + b`. Each time the voice callback fires it records `(gettime(), framesConsumed)` and updates the map with a gentle low-pass, so time never jumps or runs backwards. This is the same idea as StepMania's `RageSoundPosMap`. `SongClock::now()` uses `gettime()` and the map, which gives sub-millisecond resolution between callbacks. **[exp, M4]:** measure jitter and drift against `ASND_GetTimerVoice` over a 3-minute song.

Why not Dance Clone's approach? It uses `SDL_GetTicks()` from a start point plus a fixed 109 ms "experimentally obtained" delay (`Constants.cpp:36-40`). That breaks as soon as start-up latency varies, and its own changelog says sync is "not quite perfect".

### 3.2 From audio time to chart time

Using StepMania's convention (verified in SM 3.9 `TimingData.cpp:98-200` and `NotesLoaderSM.cpp:93-94`):

```
noteTime(beat) = TimingData.secondsAtBeat(beat) − #OFFSET − globalOffset
chartNow       = SongClock::now()          // seconds of music played (0 = first audio sample)
```

- `#OFFSET` is per song (from the file). `globalOffset` is per machine (a calibration covering TV, audio and pad latency, stored in `config.ini`).
- **Precompute** `noteTime` (as a `double`) for every note at load. Judging is then a simple comparison. SM 3.9 used `float`, but we use `double` for seconds to avoid precision loss on long songs.
- For rendering, `currentBeat = TimingData.beatAtSeconds(chartNow + OFFSET + globalOffset)`, and a note's y position = `(note.beat − currentBeat) · pixelsPerBeat · speedMod`. Scrolling by **beat** is what makes arrows stop during `#STOPS`, matching StepMania's behaviour.

### 3.3 TimingData

Port SM 3.9's `TimingData` (`GetElapsedTimeFromBeat` and `GetBeatAndBPSFromElapsedTime`, MIT) and restructure it as a **precomputed segment table**:

```cpp
struct TimingEvent { double beat; double second; enum {BPM, STOP, DELAY, WARP} type; double value; };
// Build once: walk events in beat order accumulating seconds → O(log n) lookups both ways.
double secondsAtBeat(double beat) const;   // binary search + linear within segment
double beatAtSeconds(double sec)  const;   // inverse; returns stop beat while inside a stop/delay
```

- **STOP** (`#STOPS`): time advances, the beat doesn't. The note *at* the stop beat is hit **before** the stop (SM's comment: "the exact beat of a stop comes before the stop", `TimingData.cpp:166`).
- **DELAY** (`.ssc #DELAYS`): like a stop, but the note at that beat is hit **after** the pause.
- **WARP** (`.ssc #WARPS`): beats skipped in zero time. Notes inside are unjudgeable (later milestone).
- `#SPEEDS`/`#SCROLLS`/`#FAKES`: parse and ignore at first, then implement as visual-only effects later.
- **Validation:** unit-test on the PC against `garcia/simfile`'s timing engine using the same files (doc 05, M4).

### 3.4 Judgement

Windows (StepMania defaults, verified in SM 3.9 `PrefsManager.cpp:87-92`), all as ± seconds from `noteTime`:

| Judgement | Window | Combo |
|---|---|---|
| Marvelous | 0.0225 | + |
| Perfect | 0.045 | + |
| Great | 0.090 | + |
| Good | 0.135 | breaks (DDR rule) / configurable |
| Boo | 0.180 | breaks |
| Miss | note passes −0.180 unhit | breaks |
| Hold OK/NG | release tolerance 0.250 (SM "OK" window) | — |

- On each `LaneEvent(pressed, t)`: take the **earliest unjudged note in that lane** with `|t − noteTime| ≤ BooWindow` (fixing Dance Clone's known issue "a step between two very close arrows counts for the earlier arrow instead of the closest one", `CHANGELOG v0.60`). Choose the closest note when two are inside the window.
- **Jumps:** notes on the same row are judged individually, but combo and score treat the row as one unit (worst judgement of the row). A row completes when all its lanes are hit or missed.
- **Holds:** the head is judged like a tap. While held, the hold is OK. If released for longer than 0.250 s before the tail, it's NG. Rolls (`4`) come later.
- Show the timing error (ms, early or late) for calibration.

### 3.5 Latency budget and calibration

| Stage | Estimate | Handling |
|---|---|---|
| Pad SI polling | ≤ 1 frame if scanned per frame; less with the sampling callback | Timestamp in the callback **[exp, M3]** |
| Audio output (DSP buffer + TV audio) | tens of ms | Absorbed by `globalOffset` |
| Video (render → scan-out → TV processing) | 1–3 frames + TV lag (often 20–100 ms on LCDs) | Separate `visualOffset`. Use the TV's "Game mode" |

A **calibration screen** (M5) plays a click track with flashing arrows. The player steps in time, and we average the error to set `globalOffset`. StepMania's equivalent is its global offset setting.

## 4. Audio system

| Element | Choice | Rationale |
|---|---|---|
| Mixer | **ASND** (libogc), voice 0 = music, voices 1–n = SFX | Has the timing queries we need (`ASND_GetTime`, `ASND_GetTimerVoice`, `ASND_GetSampleCounter`). AESND has none **[src]** |
| Music feed | `ASND_SetVoice(0, VOICE_STEREO_16BIT, rate, 0, buf, len, vol, vol, cb)` then `ASND_AddVoice` from the callback **[src: asndlib.h; pattern from SM-Wii `RageSoundDriver_Wii.cpp:125`]** | Double/triple buffering with ~2048-frame chunks (≈ 43 ms) |
| Sample rate | Play at the file's native rate (44.1 or 48 kHz) via ASND's `pitch` = rate | Avoids our own resampler |
| Decoders | **Tremor** (`ppc-libvorbisidec`) for `.ogg`, **libmad** (`ppc-libmad`) for `.mp3`, built-in for `.wav` | Integer decoders, already packaged. Most simfiles use OGG or MP3 |
| Memory | Read the whole **compressed** file into MEM2 at song start (3–8 MB), decode on the fly into a ~0.5 s PCM ring | Full PCM decode (~35 MB for 3 min) would use too much of the 88 MB |
| Menu preview | Same streamer, starting at `#SAMPLESTART` for `#SAMPLELENGTH` | — |
| SFX | WAVs preloaded and played on free voices (as in Dance Clone `Sound.cpp:240-250`) | — |
| Avoid | `MP3Player_*` for gameplay (no position query **[src: mp3player.h]**) | It's why Dance Clone needed a 109 ms fudge |

Bugs in StepMania-Wii's driver that we shouldn't copy: `printf` inside the IRQ callback (`RageSoundDriver_Wii.cpp:31`), a busy-spinning fill thread (`usleep(0)`, `:57`), and position granularity of one whole buffer (`:145-157`).

## 5. Rendering

- **GRRLIB** on GX: `GRRLIB_Init()`, `GRRLIB_LoadTexturePNG()`, `GRRLIB_DrawImg/DrawTile`, `GRRLIB_PrintfTTF`, `GRRLIB_Render()` (which swaps on vsync) **[exp: confirm names against the GRRLIB v4.6.1 headers]**.
- 640×480 logical canvas. Four lanes of 64 px, centred. Receptors at y ≈ 80 (upward scroll, DDR style).
- **Arrow texture atlas:** one 256×256 PNG holding 4 directions × quantisation colours (4th red, 8th blue, 16th yellow, …, like Dance Clone's `Media/Game/04thnote.png`…`192ndnote.png`, which shows the idea). Rotate one arrow sprite instead of storing four.
- **Receptor flash** on press, an **explosion** sprite on hit tinted by judgement, judgement text plus a combo counter (bitmap font), a life bar, and a score.
- **Holds:** body tiled between head and tail, a cap at the tail, dimmed after NG.
- Only draw notes within the visible beat range (a binary search into the sorted note array). Cost is O(visible).
- Menus: a vertical song wheel (banner + title + BPM + difficulty meters), with the same renderer.

## 6. Chart system

| Layer | Implementation | Origin |
|---|---|---|
| Tokenizer | `MsdParser`: splits `#KEY:v1:v2:...;`, handles `//` comments, escapes, a missing `;` before the next `#` | Port of SM 3.9 `MsdFile.cpp` (184 lines, MIT) |
| `.sm` loader | `#TITLE #SUBTITLE #ARTIST #BANNER #BACKGROUND #MUSIC #OFFSET #BPMS #STOPS(#FREEZES) #DISPLAYBPM #SAMPLESTART #SAMPLELENGTH #NOTES` | Port of SM 3.9 `NotesLoaderSM.cpp` (549 lines, MIT) |
| `.ssc` loader | Song header as above, plus `#VERSION #DELAYS #WARPS #SPEEDS #SCROLLS #FAKES #TIMESIGNATURES #TICKCOUNTS #COMBOS #LABELS`, then `#NOTEDATA:;` blocks with `#STEPSTYPE #DIFFICULTY #METER #NOTES`. **Per-chart timing tags override the song's** | New code, following the [ssc spec](https://github.com/stepmania/stepmania/wiki/ssc). Test against `simfile` |
| Prefer `.ssc` | If both `Song.ssc` and `Song.sm` exist, load `.ssc` (StepMania 5 behaviour) | — |
| Notes | For `dance-single`, each measure has *R* rows. Row *i* sits at beat `4·measure + 4·i/R`. Characters: `0` none, `1` tap, `2` hold head, `4` roll head, `3` tail, `M` mine, `L` lift, `F` fake, `K` keysound | sm spec |
| Prototype support | taps, holds, jumps. Ignore mines, fakes and lifts at first (parse them, don't judge them). Rolls are treated as holds | — |
| Output | `Chart{ difficulty, meter, TimingData, vector<Note{beat, time, lane, type, endBeat, endTime}> }` sorted by time | — |

`.dwi` support could come later from SM 3.9's `NotesLoaderDWI.cpp` if wanted.

## 7. Game state and screens

```
Boot ─► Title ─► SongSelect ─► DifficultySelect ─► Gameplay ─► Results ─┐
            ▲                                                         │
            └──────────────────────── back ◄──────────────────────────┘
Options (scroll speed, global offset, calibration) reachable from Title.
```

- **SongSelect:** a list from `SongLibrary` (scans `sd:/ddrwii/songs/*/*/`), sorted by pack then title. Arrows move, Start chooses, B/Z backs out. Preview audio plays after a 0.5 s pause on an entry.
- **SongLibrary cache:** parsing headers for hundreds of songs from SD is slow, so cache `{path, mtime, title, artist, bpm range, difficulties}` in `sd:/ddrwii/cache.bin` **[exp: measure the scan time]**.
- **Scoring (prototype):** "dance points" percentage, as in StepMania/ITG (Marvelous/Perfect = 100%, Great = 50%, Good/Boo/Miss = 0, holds OK = full credit) plus max combo and judgement counts. DDR-style money score can be added later (SM 3.9 `ScoreKeeperMAX2.cpp` as reference).
- **High scores:** `scores.dat` is a small versioned binary or INI keyed by `songHash + difficulty`, storing the best percentage, combo and grade. It's written atomically (write a temp file, then rename).
- **Settings:** `config.ini` holds `GlobalOffset`, `VisualOffset`, `ScrollSpeed`, `PadPort`, `LaneMap`.

## 8. Wii-specific lifecycle

- Init order: `VIDEO`/GRRLIB → `PAD_Init` → `fatInitDefault()` (retry for up to ~2 s; Dance Clone uses `sleep(1)` as a workaround, `WiiOS.cpp:49-52`) → `ASND_Init` → load config.
- **Reset/Power buttons:** register callbacks that only set a flag, then handle the flag in the main loop (Dance Clone's correct pattern, `WiiOS.cpp:28-66`). Check the current callback prototypes in `ogc/system.h` **[exp]**.
- **Exit:** return from `main()` or call `exit(0)` to go back to HBC/Dolphin.
- Memory: allocate large buffers (audio file, ring, textures) from MEM2 where possible. Watch the heap usage display in the debug overlay.

## 9. Initial repository structure

```
ddr-wii/
├── docs/                         # these documents
├── reference/                    # supplied archives + extracted copies (read-only, not modified)
├── tools/
│   ├── padtest/                  # M1/M3: diagnostic from doc 04 (own Makefile)
│   ├── deploy-dolphin.sh         # copy boot.dol + sd/ to Dolphin's WiiSDSync, launch Dolphin -e
│   └── simfile-oracle.py         # dumps beat→time tables via `simfile` for unit tests
├── game/
│   ├── Makefile                  # based on devkitPro wii-examples/templates/makefile
│   ├── source/
│   │   ├── main.cpp
│   │   ├── core/                 # portable C++17, no libogc includes
│   │   │   ├── chart/  MsdParser.*  SmLoader.*  SscLoader.*  Chart.h
│   │   │   ├── timing/ TimingData.*  SongClock.h (interface)
│   │   │   ├── judge/  Judge.*  Score.*
│   │   │   └── songs/  SongLibrary.*  HighScores.*  Config.*
│   │   ├── platform/wii/         # libogc / GRRLIB implementations
│   │   │   ├── WiiInput.*  WiiAudio.*  WiiVideo.*  WiiFs.*  WiiSystem.*
│   │   └── screens/  Title.*  SongSelect.*  Gameplay.*  Results.*  Options.*
│   ├── data/                     # small embedded assets (font, arrow atlas) → linked in
│   └── tests/                    # host-side unit tests for core/ (build with g++/MSVC on PC)
└── sd/                           # staging copy of the SD card layout (doc 03 §6.3)
    ├── apps/ddrwii/{boot.dol,meta.xml,icon.png}
    └── ddrwii/songs/<Pack>/<Song>/...
```

The `core/` code has no dependency on libogc, so `game/tests/` can compile it with your PC compiler, and parser and timing bugs can be found in seconds without Dolphin.

## 10. Design decisions driven by the Wii hardware

| Constraint | Decision |
|---|---|
| 88 MB RAM (24 + 64) | Stream-decode compressed audio. Downscale banners (≤ 256×80) and backgrounds (≤ 640×480). Lazy song-metadata cache |
| 729 MHz in-order PowerPC | Parse at load, never per frame. Integer audio decoders. Binary-searched visible note range |
| Fixed-function GX | 2D sprites via GRRLIB. Texture atlases. No shaders needed |
| 50 Hz PAL / 60 Hz NTSC, interlaced or progressive | All motion derived from song time. Optional 480p |
| SD card speed | Load the whole audio file before starting play (no SD reads during gameplay). Cache metadata |
| SI pad polling | Timestamp inputs at sampling time, not at frame time |
| No reliable wall clock for sync | The audio sample count is the master clock |
