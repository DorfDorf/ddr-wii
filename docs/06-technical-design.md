# 06 — Technical Design: Lightweight Wii Rhythm Engine

*Proposed design for Option D (Hybrid) from [02](02-architecture-options.md). It's deliberately small. Anything marked **[exp]** must be validated in the milestone that introduces it ([05](05-development-roadmap.md)).*

---

## 1. Goals and non-goals (prototype)

**Goals:** a single-player four-lane `dance-single` game that reads `.sm`/`.ssc` from SD, plays OGG/MP3, scrolls arrows in sync, judges taps and holds, shows a combo and score, and saves high scores. It runs in Dolphin and on a real Wii with a GameCube dance mat.

**Local multiplayer:** **2 players is the target.** 4 players passed both the performance and readability checks on a real Wii (M2b), but it is **deferred until there's a working product** (after M6), by decision on 2026-11-08 (see §11). Each player uses their own mat on their own GameCube port and plays the same song with their own difficulty and scroll speed. The core is built per player from the start, even though the first playable milestones run with one player.

**Non-goals (for now):** background videos, BGCHANGES, mods and attacks, Lua or themes, courses, doubles (one player on two mats, which the per-port design makes easy to add later), online play, editing. All of these can be added later without changing the core.

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
| CPU timer | libogc `gettime()` (64-bit time base ticks) and `ticks_to_microsecs()` on 64-bit tick differences. `diff_usec()` returns a `u32` and wraps after ~71.6 min, so use it only for short intervals **[src: lwp_watchdog.h; StepMania-Wii `ArchHooks_Wii.cpp:68-73` has this wrap]** | Timestamps for inputs and the audio callbacks. Smoothing |
| Audio position | Frames handed to the DSP and completed, counted in our ASND voice callback. Cross-checked with `ASND_GetTimerVoice(voice)` (ms since the voice started) and `ASND_GetSampleCounter()` **[src: asndlib.h]** | **The master song clock** |

**SongClock** keeps a linear map `audioSeconds ≈ a·cpuSeconds + b`. Each time the voice callback fires it records `(gettime(), framesConsumed)` and updates the map with a gentle low-pass, so time never jumps or runs backwards. This is the same idea as StepMania's `RageSoundPosMap`. `SongClock::now()` uses `gettime()` and the map, which gives sub-millisecond resolution between callbacks. **[exp, M4]:** measure jitter and drift against `ASND_GetTimerVoice` over a 3-minute song.

Why not Dance Clone's approach? It uses `SDL_GetTicks()` from a start point plus a fixed 109 ms "experimentally obtained" delay (`Constants.cpp:36-40`). That breaks as soon as start-up latency varies, and its own changelog says sync is "not quite perfect".

### 3.2 From audio time to chart time

Using StepMania's convention (verified in SM 3.9 `TimingData.cpp:98-200` and `NotesLoaderSM.cpp:93-94`):

```
noteTime(beat) = TimingData.secondsAtBeat(beat) − (#OFFSET + packOffset) − globalOffset
chartNow       = SongClock::now()          // seconds of music played (0 = first audio sample)
```

- `#OFFSET` is per song (from the file). `globalOffset` is per machine (a calibration covering TV, audio and pad latency, stored in `config.ini`).
- `packOffset` is per pack, following ITGmania's `pack.ini` convention:
  - `SyncOffset=ITG` → −0.009 s (the pack carries ITG's 9 ms bias).
  - `SyncOffset=NULL` → 0.
  - No `pack.ini` → the `DefaultSyncOffset` setting.

  ITGmania adds it to the song offset (`m_fBeat0OffsetInSeconds + m_fBeat0GroupOffsetInSeconds`). **[exp: confirm the sign with an ITG-synced pack]**
- **Precompute** `noteTime` (as a `double`) for every note at load. Judging is then a simple comparison. SM 3.9 used `float`, but we use `double` for seconds to avoid precision loss on long songs.
- For rendering, `currentBeat = TimingData.beatAtSeconds(chartNow + OFFSET + packOffset + globalOffset)`, and a note's y position = `(note.beat − currentBeat) · pixelsPerBeat · speedMod`. Scrolling by **beat** is what makes arrows stop during `#STOPS`, matching StepMania's behaviour.

### 3.3 TimingData

Port the `TimingData` algorithms from ITGmania tag `v1.3.0` (GPLv3, like this project; see [doc 02](02-architecture-options.md#itgmania-what-to-borrow)). That version includes the 2026 lookup-table and time-signature fixes:
- `GetElapsedTimeInternal` and `GetBeatInternal`, which already handle BPMs, stops, delays and warps together
- `GetDisplayedBeat`, for speeds and scrolls

SM 3.9's `GetElapsedTimeFromBeat`/`GetBeatAndBPSFromElapsedTime` are the small, readable version of the same idea. Restructure the result as a **precomputed segment table**:

```cpp
struct TimingEvent { double beat; double second; enum {BPM, STOP, DELAY, WARP} type; double value; };
// Build once: walk events in beat order accumulating seconds → O(log n) lookups both ways.
double secondsAtBeat(double beat) const;   // binary search + linear within segment
double beatAtSeconds(double sec)  const;   // inverse; returns stop beat while inside a stop/delay
```

- **STOP** (`#STOPS`): time advances, the beat doesn't. The note *at* the stop beat is hit **before** the stop (SM's comment: "the exact beat of a stop comes before the stop", `TimingData.cpp:166`).
- **DELAY** (`.ssc #DELAYS`): like a stop, but the note at that beat is hit **after** the pause.
- **WARP** (`.ssc #WARPS`): beats skipped in zero time. Notes inside are unjudgeable (later milestone).
- **Negative BPMs and negative stops in `.sm` files become warps**, as SM5's `SMLoader::ProcessBPMsAndStops` does. ITG-era packs use this trick, and SM 3.9 doesn't handle it.
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

- On each `LaneEvent(pressed, t)`: take the **closest unjudged note in that lane** with `|t − noteTime| ≤ BooWindow`. This fixes Dance Clone's known issue "a step between two very close arrows counts for the earlier arrow instead of the closest one" (`CHANGELOG v0.60`), which v0.68 still has (`Game.cpp:2595-2605`). A press for an incomplete jump must not fall through to a later note in the same lane.
- **Early-hit rule** (ITGmania's fix for "way off towers", `27c6f7757a`; adapted from its `Player.cpp`):
  - An early hit graded worse than Great doesn't use up the note. Keep that grade, apply its life change once, and still accept a better re-hit.
  - If the note passes without a re-hit, it keeps the early grade instead of becoming a Miss.
  - This stops mashing and pad bounce from eating notes. Make it an option, on by default.
- **Debounce** pad input in `WiiInput`, following ITGmania's `InputFilter` (default 20 ms for pads):
  - Report a press or release at once, with its original timestamp.
  - Hold back further changes on that panel for the window.
  - Drop a release that is followed by a re-press inside the window.

  Tune the window from the bounce measured in M3.
- **Held misses:** record when a note is missed while its lane is held down. It flags stuck or dead panels, as ITGmania does for pad debugging.
- **Jumps:** notes on the same row are judged individually, but combo and score treat the row as one unit (worst judgement of the row). A row completes when all its lanes are hit or missed.
- **Holds:** the head is judged like a tap. While held, the hold is OK. If released for longer than 0.250 s before the tail, it's NG. Rolls (`4`) come later.
- Show the timing error (ms, early or late) for calibration.
- **Implementation (M4c):** `core/game/Judge`, one per player. It takes song seconds, so the caller converts `LaneEvent` timestamps with SongClock and the offsets. Dance points are per row (Marvelous/Perfect 2, Great 1, the rest 0) plus 2 per held hold. The early-hit rule follows ITGmania v1.3.0 `Player.cpp` [src]: only one-note rows, the first early bad grade is kept, and after that only a Great-or-better re-hit replaces it. `MakeAutoplay` gives the perfect inputs for the autoplay test and debug mode.

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
| MP3 timing | Decode like ITGmania v1.3.0 [src]: no gapless trim (LAME delay and libmad's 529 samples stay in), skip a first `Xing` frame, but play a first `Info` frame as audio | Simfile offsets were synced against ITGmania's output; trimming would make MP3 songs ~25–50 ms early |
| Avoid | `MP3Player_*` for gameplay (no position query **[src: mp3player.h]**) | It's why Dance Clone needed a 109 ms fudge |

Bugs in StepMania-Wii's driver that we shouldn't copy (its author reports it crashed after a few seconds):
- `printf` inside the IRQ callback (`RageSoundDriver_Wii.cpp:31`)
- a busy-spinning fill thread (`usleep(0)`, `:57`) created at priority 0, libogc's idle priority (`Threads_Wii.cpp:54`)
- a frame number of 0 passed to every `GetPCM` (`:46`, `:116`), which breaks StepMania's position map
- a position that counts bytes queued, not played (`:28`, `:122`), with the granularity of one whole buffer (`:145-157`)
- clipped song endings: `StopPlaying()` from the fill thread on a short read (`:49`), and full 8 KB buffers queued even when partly filled (`:33`)
- no synchronisation between the IRQ callback and the fill thread

How ASND behaves, from `libasnd/asndlib.c` **[src]**: it mixes in ticks of 1024 output samples at 48 kHz (21.3 ms), whatever the voice's rate. A voice callback is called at interrupt level on **every tick while the second buffer slot is empty**, not once. `ASND_TestPointer` checks both slots by base address. `ASND_GetTimerVoice`/`ASND_GetTickCounterVoice` count those ticks since `ASND_SetVoice`, so they step in 21.3 ms. `VOICE_STEREO_16BIT` is big-endian; WAV data uses `VOICE_STEREO_16BIT_LE`. Data the CPU wrote must be flushed (`DCFlushRange`) before the DSP reads it.

The M4b implementation (`platform/wii/WiiAudio`) uses a ring of 16 buffers of 2048 frames. A fill thread polls every 4 ms and fills free buffers. The voice callback retires every buffer that `ASND_TestPointer` no longer reports (adding its frames to "played") and queues the next ready buffer.

**SongClock's source is the tick, not the buffer.** A global `ASND_SetCallback` records `{gettime(), ASND_GetTickCounterVoice(0)}` every tick, and one tick is 64000/3 µs of song at any voice rate. The buffer-finished reports are late by a rate-dependent amount: exactly 1 tick at 48 kHz, where 2048 frames = 2 ticks, and 1–2 ticks at 44.1 kHz, where buffers end mid-tick. Driving the clock from them made 44.1 kHz songs about 11 ms later than 48 kHz ones (doc 05 M4b). They remain a cross-check, because they count real file content and the tick counter doesn't (it keeps counting through an underrun). `SongClock` (§3.1, `core/timing/SongClock`) moves its offset 1/64 of the way to each report, snaps on errors over 50 ms, never runs backwards, and stops 200 ms after the last report.

Use `ASND_TestPointer()` / `ASND_TestVoiceBufferReady()` (`asndlib.h:354`, `:363`) so a buffer is never refilled while it is still playing.

## 5. Rendering

- **GRRLIB** on GX: `GRRLIB_Init()`, `GRRLIB_LoadTexturePNG()`, `GRRLIB_DrawImg/DrawTile`, `GRRLIB_PrintfTTF`, `GRRLIB_Render()` (which swaps on vsync) **[exp: confirm names against the GRRLIB v4.6.1 headers]**.
- 640×480 logical canvas. Four lanes of 64 px, centred. Receptors at y ≈ 80 (upward scroll, DDR style). With 2–4 players the lane width and playfield positions come from a layout table (§11.3).
- **Arrow texture atlas:** one 256×256 PNG holding 4 directions × quantisation colours (4th red, 8th blue, 16th yellow, …, like Dance Clone's `Media/Game/04thnote.png`…`192ndnote.png`, which shows the idea). Rotate one arrow sprite instead of storing four.
- **Receptor flash** on press, an **explosion** sprite on hit tinted by judgement, judgement text plus a combo counter (bitmap font), a life bar, and a score.
- **Holds:** body tiled between head and tail, a cap at the tail, dimmed after NG.
- Only draw notes within the visible beat range (a binary search into the sorted note array). Cost is O(visible).
- Menus: a vertical song wheel (banner + title + BPM + difficulty meters), with the same renderer.

## 6. Chart system

| Layer | Implementation | Origin |
|---|---|---|
| Tokenizer | `MsdParser`: splits `#KEY:v1:v2:...;`, handles `//` comments, escapes, a missing `;` before the next `#` | Port of `MsdFile.cpp` from ITGmania `v1.3.0` (GPLv3, with escapes and the 2024 escape fix). SM 3.9's 184-line version has no escapes |
| `.sm` loader | `#TITLE #SUBTITLE #ARTIST #BANNER #BACKGROUND #MUSIC #OFFSET #BPMS #STOPS(#FREEZES) #DELAYS #DISPLAYBPM #SAMPLESTART #SAMPLELENGTH #NOTES`. Negative BPMs and stops become warps | Port of the tag handling and `ProcessBPMsAndStops` from ITGmania `v1.3.0` `NotesLoaderSM.cpp`. SM 3.9's 549-line loader is the readable reference |
| `.ssc` loader | Song header as above, plus `#VERSION #DELAYS #WARPS #SPEEDS #SCROLLS #FAKES #TIMESIGNATURES #TICKCOUNTS #COMBOS #LABELS`, then `#NOTEDATA:;` blocks with `#STEPSTYPE #DIFFICULTY #METER #NOTES`. **Per-chart timing tags override the song's** | Port of ITGmania `v1.3.0` `NotesLoaderSSC.cpp`, following the [ssc spec](https://github.com/stepmania/stepmania/wiki/ssc). Test against `simfile` |
| Prefer `.ssc` | If both `Song.ssc` and `Song.sm` exist, load `.ssc` (StepMania 5 behaviour) | — |
| Notes | For `dance-single`, each measure has *R* rows. Row *i* sits at beat `4·measure + 4·i/R`. Characters: `0` none, `1` tap, `2` hold head, `4` roll head, `3` tail, `M` mine, `L` lift, `F` fake, `K` keysound | sm spec |
| Prototype support | taps, holds, jumps. Ignore mines, fakes and lifts at first (parse them, don't judge them). Rolls are treated as holds | — |
| Output | `Chart{ difficulty, meter, TimingData, vector<Note{beat, time, lane, type, endBeat, endTime}> }` sorted by time | — |

`.dwi` support could come later from ITGmania `v1.3.0`'s `NotesLoaderDWI.cpp` (SM 3.9's version as the reference) if wanted.

## 7. Game state and screens

```
Boot ─► Title ─► SongSelect ─► DifficultySelect ─► Gameplay ─► Results ─┐
            ▲                                                         │
            └──────────────────────── back ◄──────────────────────────┘
Options (scroll speed, global offset, calibration) reachable from Title.
```

- **SongSelect:** a list from `SongLibrary` (scans `sd:/ddrwii/songs/*/*/`), sorted by pack then title. If a pack has a `pack.ini` (ITGmania's format: a `[Group]` section with `DisplayTitle`, `SortTitle`, `Series`, `Banner`, `Year`, `SyncOffset`), use its display title, sort title and banner, and apply its `SyncOffset` (§3.2). Ported from ITGmania's `Group.cpp`. Preview audio plays after a 0.5 s pause on an entry. **Built (M5 step 2):** `screens/SongSelect`. Its layout follows ITGmania's default theme, Simply Love (GPLv3), in 4:3: banner top left (a title placeholder until step 6), artist/BPM/length box, a difficulty grid with the meter in the difficulty's colour and ten meter blocks, one pane per player along the bottom with the chart's steps/jumps/holds/mines/rolls, and the music wheel on the right (pack headers, the open pack's songs under its header). It's redrawn with our own rects and bitmap font; no theme files are used. Colours are Simply Love's (its default colour index). Controls follow ITG on a mat: Left/Right move the wheel (held: repeat after 0.4 s), Up/Down change that player's difficulty, START opens/closes a pack or picks the song. Each player's difficulty is remembered from song to song (nearest chart if the song lacks it). Debug: Z toggles that player's autoplay, X leaves, Y on port 1 adds/removes an autoplay player, L/R on port 1 set the global offset in 5 ms steps (until `config.ini`, step 3), B on port 1 goes to the test scenes. START+Z for 2 s exits.
- **SongLibrary cache:** parsing headers for hundreds of songs from SD is slow, so cache `{path, mtime, title, artist, bpm range, difficulties}` in `sd:/ddrwii/cache.bin` **[exp: measure the scan time]**.
- **Scoring (prototype):** "dance points" percentage, as in StepMania/ITG (Marvelous/Perfect = 100%, Great = 50%, Good/Boo/Miss = 0, holds OK = full credit) plus max combo and judgement counts. DDR-style money score can be added later (SM 3.9 `ScoreKeeperMAX2.cpp` as reference).
- **High scores:** `scores.dat` is a small versioned binary or INI keyed by `songHash + difficulty`, storing the best percentage, combo and grade. It's written atomically (write a temp file, then rename).
- **Settings:** `config.ini` holds `GlobalOffset`, `VisualOffset`, `DefaultSyncOffset` (`NULL` or `ITG`, for packs without `pack.ini`), `InputDebounceMs`, and per port (`[Port1]`…`[Port4]`) `LaneMap`, `InputOffset` and the last-used `ScrollSpeed`. See §11 for how ports become players.

## 8. Wii-specific lifecycle

- Init order: `VIDEO`/GRRLIB → `PAD_Init` → `fatInitDefault()` (retry for up to ~2 s; Dance Clone uses `sleep(1)` as a workaround, `WiiOS.cpp:49-52`) → `ASND_Init` → load config.
- **Reset/Power buttons:** register callbacks that only set a flag, then handle the flag in the main loop (Dance Clone's correct pattern, `WiiOS.cpp:28-66`). Check the current callback prototypes in `ogc/system.h` **[exp]**.
- **Exit:** return from `main()` or call `exit(0)` to go back to HBC/Dolphin.
- Memory: allocate large buffers (audio file, ring, textures) from MEM2 where possible. Watch the heap usage display in the debug overlay.

## 9. Repository structure

```
ddr-wii/
├── CLAUDE.md                     # notes for Claude: commands, code rules, key decisions
├── docs/                         # these documents
├── reference/                    # supplied archives + extracted copies (local only, read-only, not in git)
├── tools/
│   ├── padtest/                  # M1/M3: pad diagnostic from doc 04 (own Makefile)
│   ├── run-dolphin.sh            # boot a .dol in Dolphin (used by `make run-dolphin`)
│   ├── setup-devkitpro.sh        # one-time toolchain + GRRLIB setup (doc 03)
│   └── simfile-oracle.py         # dumps beat→time tables via `simfile` for the tests
├── game/
│   ├── Makefile                  # based on devkitPro wii-examples/templates/makefile
│   ├── source/
│   │   ├── main.cpp
│   │   ├── core/                 # portable C++17, no libogc includes
│   │   │   ├── chart/  MsdFile.*  SmLoader.*  SscLoader.*  NoteData.*  Chart.*  PackIni.*  StringUtil.*
│   │   │   ├── timing/ TimingData.*  (later) SongClock.h (interface)
│   │   │   ├── judge/  (later) Judge.*  Score.*
│   │   │   └── songs/  SongLibrary.*  (later) HighScores.*  Config.*
│   │   ├── platform/wii/         # libogc / GRRLIB wrappers: WiiVideo.*
│   │   │                         #   (later) WiiInput.*  WiiAudio.*  WiiFs.*  WiiSystem.*
│   │   ├── render/               # GRRLIB drawing: ArrowArt.*  Font.*  Draw.*
│   │   └── screens/  PlayfieldStress.*  RenderTest.*
│   │                 Session.h  SongSelect.*  Gameplay.* (with results)
│   │                 (later) Title.*  Options.*
│   ├── data/                     # (later) small embedded assets (font, arrow atlas) → linked in
│   └── tests/                    # host-side tests for core/ (built with g++ on the PC)
└── sd/                           # staging copy of the SD card layout (doc 03 §6.3)
    ├── apps/ddrwii/{boot.dol,meta.xml,icon.png}
    └── ddrwii/songs/<Pack>/<Song>/...
```

Entries marked (later) are planned and don't exist yet. The `core/` code has no dependency on libogc, so `game/tests/` can compile it with your PC compiler, and parser and timing bugs can be found in seconds without Dolphin.

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
| 4 GameCube ports (original Wii models only) | Up to 4 local players, one mat per port. The Family Edition (RVL-101) and Wii Mini have no GameCube ports, so this game needs an original-model Wii, even for one player |

## 11. Local multiplayer (2–4 players)

**Scope (decided 2026-11-08): build 2 players now; 4 players later.** M2b showed 4P is fast enough and readable on a real Wii, but it waits until the game works end to end. The per-player design below costs nothing extra for 2P, so the only 4P-specific work left for later is the 3–4P layout (§11.3), the join flow for ports 3–4, and testing with four mats.

### 11.1 What is shared and what is per player

| Shared (one per song) | Per player |
|---|---|
| `SongClock`, the audio stream, `globalOffset` / `visualOffset` (one TV, one sound path) | Chart (their difficulty), its precomputed note times |
| Song selection, pack offset, `TimingData` for song-level timing | `Judge` state, `Score`, combo, life bar, held-miss log |
| Background, banner, SFX voices | Scroll speed, lane map, `InputOffset` (small per-mat latency difference) |
| | Debounce state (per port, per panel) |

`.ssc` charts can carry their own timing, so each player keeps a pointer to their chart's `TimingData` (which falls back to the song's). Two players on different difficulties of a split-timing song are then both correct.

Memory cost is small: a chart is a few thousand notes, so four of them are tens of KB. The audio buffers and textures are shared.

### 11.2 Input and joining

- `PAD_ScanPads()` already reads all four ports, and `LaneEvent` already carries `port` (doc 04 §8). The gameplay loop routes each event to the player bound to that port. Nothing else in input changes.
- **Joining:** on the title screen and song select, pressing Start on a port joins it as the next player, DDR style. Leaving goes through B/Z on that port before the song starts. The player number follows join order, not the port number, so one mat in port 3 is still P1.
- **Menus:** any joined player can move the song wheel. Difficulty and scroll speed are chosen per player on the difficulty screen, with each player's cursor in their own column. The song starts when every joined player has confirmed.
- **Pause:** Start on any joined port pauses for everyone.
- **Built (M5 step 2):** song select does the joining (there's no title screen yet), and nobody is joined until someone presses START. Leaving is X for now (B on port 1 still opens the test scenes). Y on port 1 adds an autoplay player on a free port for testing with one pad or the keyboard; START on that port turns it into a human player. The players go to Gameplay in a `Session`, in join order. START during a song stops it and shows the results (there's no pause yet).

### 11.3 Playfield layout

The logical canvas is 640 px wide. Four lanes at 64 px are 256 px, so:

| Players | Lane width | Playfield width | Layout |
|---|---|---|---|
| 1 | 64 px | 256 px | Centred |
| 2 | 64 px | 256 px | Centred at x = 160 and 480, DDR versus style. Fits with room for score and life bars |
| 3–4 | ~36 px (arrows scaled ≈ 0.55) | ~144 px, plus gaps | Four columns of 160 px. Judgement and combo text sits inside each column |

- Every draw goes through a `Playfield{ x, laneWidth, scale }` built from this table, so 1, 2 and 4 players use the same rendering code.
- **Overscan safe area (found in the M2b hardware run).** CRTs and many LCDs crop the edges of the Wii picture. On the test TV (Sony Bravia, 480p) the 4P layout lost the left edge of P1's left lane, the top of the life bars and the bottom of the screen. So:
  - Playfields stay inside the **action-safe area** (5% margin: x 32–608, y 24–456). For 3–4P that means four 144 px columns with **32 px lanes** (scale 0.5), slightly smaller than the 36 px tested.
  - Text and HUD (scores, life bars, judgement, combo, debug overlay) stay inside the **title-safe area** (10% margin: x 64–576, y 48–432).
  - An **overscan adjustment** in Options (shrink or grow the safe area per TV, stored in `config.ini`) covers TVs that crop more or less.
- **Correction (2026-11-08, M4c):** every scene drew the arrow sprites 32 px left of and above where they should be. The textures use `GRRLIB_SetMidHandle`, so GRRLIB already draws them centred on the given position, and the code subtracted half the sprite size as well. So the M2b finding that P1's left lane lost its left edge in 4P was at least partly this bug. Recheck that edge when 4P is picked up. The life-bar and bottom-edge losses came from rectangles and still stand.
- **4-player readability: passed** (M2b, 2026-11-08). 36 px arrows were readable from playing distance on the test TV. When 4P is picked up, recheck at 32 px lanes inside the safe area. A 16:9 mode (Wii set to widescreen, drawing for an 854 px-wide logical canvas, lanes about 48 px wide on screen) remains an option if needed.

### 11.4 Rules

- The song keeps playing until it ends or **every** player has failed. A failed player's playfield greys out and stops judging. A per-player "no fail" option is planned with the rest of the options.
- Results show every player side by side. High scores are still keyed by `songHash + difficulty`. Player names or profiles come later (a three-letter entry on the results screen is enough at first).
- Assist clap and other SFX play once per row, not once per player.

### 11.5 Performance budget **[exp]**

| Cost | 1 player | 4 players | Notes |
|---|---|---|---|
| Visible notes and hold pieces | ~30–80 sprites | ~120–320 sprites | Dense 16ths at high speed are the worst case |
| Receptors, explosions, judgement, combo, life, score | ~15 draws | ~60 draws | Text uses the bitmap font (`render/Font`), not `GRRLIB_PrintfTTF` per frame |
| Judging | trivial | trivial | O(events) per frame |
| Audio decode | one stream | one stream | Unchanged: the music is shared |

The M2 render test already holds full frame rate with **1,600 sprites** in Dolphin, so the sprite count alone is well inside the budget. The open questions are:

- **Dolphin isn't a performance model.** It draws on the PC's GPU and doesn't model how long the Wii's GPU takes, so GPU cost is invisible there. Its CPU timing follows emulated instructions but not cache misses, so CPU time measured in Dolphin is only a lower bound. The 2P/4P stress test must also run on the real Wii with the frame-time overlay (via `make -C game run` and wiiload, doc 03).
- **Fill rate is probably not the limit.** A 4-player worst case is roughly 1.5 Mpx per frame: a full-screen background (0.3 Mpx), four lane backgrounds (~0.28 Mpx), ~320 notes at 36² px (~0.4 Mpx), plus hold bodies and explosions. At 60 Hz that's ~90 Mpx/s, about a tenth of Hollywood's theoretical ~970 Mpx/s (4 pixel pipes × 243 MHz). Real texturing and blending lower the ceiling, but leave a wide margin.
- **CPU time per draw is the more likely limit.** Each `GRRLIB_DrawImg` sets up a matrix, loads its texture object and submits a quad, all on the CPU. Hundreds of those per frame, plus texture switches, add up. Mitigations if it shows: one atlas texture for all arrows (fewer texture loads), batching notes into a single GX draw per playfield, and drawing text from a glyph atlas.
- **Measure what's measurable anywhere:** the render test reports per frame the CPU time from frame start to the vsync wait (`gettime()`), the number of draws, texture switches and the total drawn pixel area. The draw and area counts are the same in Dolphin and on hardware. They show where the budget goes before the hardware run decides.
- **Four mats polled at once:** check that SI polling with four devices connected doesn't change the PAD sampling interval measured in M3.

The test plan is in doc 05 (M2b, then the M3 and M6 hardware checks).

**Result (M2b on a real Wii, 2026-11-08):** performance is settled. With the worst-case scene, 4 players took 1.58 ms of CPU and about 1 ms of GPU wait per 16.7 ms frame, with no dropped frames at any player count (full table in doc 05, M2b). That's roughly 5× headroom, so neither draw calls nor fill rate limits 4 players, and the atlas and batching mitigations above aren't needed for now. Dolphin's CPU numbers turned out slightly *higher* than hardware for this code. The open questions for 4P are readability and the overscan safe area (§11.3), not speed.
