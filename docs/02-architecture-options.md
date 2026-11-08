# 02 — Architecture Options and Recommendation

*Based on the code review in [07](07-existing-codebase-review.md) and the ecosystem research in [01](01-project-research.md). Date: 2026-11-08. Updated the same day with a review of ITGmania, the actively developed StepMania fork (see [ITGmania: what to borrow](#itgmania-what-to-borrow)).*

Ratings: **Feasibility 1–5** (5 = very likely to succeed). "Development required" describes the **amount and kind of work**, not calendar time. With Claude writing the code, code volume matters less. What still costs real time is debugging unknowns on hardware, legacy-toolchain archaeology, and anything you have to test by hand (see [05](05-development-roadmap.md#where-the-time-actually-goes-ai-assisted-development)). AI assistance doesn't change the ranking: Options A and A′ are limited by the Wii's hardware (memory, no OpenGL) and by unproven legacy code, not by how fast code can be written.

---

## Option A — Port a StepMania-based engine (SM 5.x / ITGmania / Etterna / OutFox)

| Criterion | Assessment |
|---|---|
| Technical feasibility | Very low. The renderer needs desktop OpenGL/GLEW (Etterna adds Vulkan). Video and audio need FFmpeg. The Lua theme system plus the song cache plus textures won't fit in 88 MB. OutFox's engine source isn't verifiably public |
| Development required | Months: a GX renderer behind the RageDisplay abstraction, sound/input/threads/file drivers, FFmpeg removal, memory diet, theme rewrite |
| Difficulty | Very high (expert-level, deep StepMania internals) |
| Performance | Unknown and likely poor (theme Lua, actor tree overhead on a 729 MHz CPU) |
| Maintainability | Poor. A permanent fork of a large moving target |
| Reusable code | Algorithms only |
| Custom charts | Excellent, if it ever ran |
| GC dance pads | Would need a new input driver (trivial compared with the rest) |
| **Feasibility** | **1 / 5** |

**ITGmania as a port target.** It's the fork worth watching:
- **Status:** v1.3.0 shipped on 2026‑07‑01, and commits continue (latest 2026‑11‑07). StepMania itself hasn't released since 5.1.0‑b2 (2018). Its repository only gets occasional build and CI fixes, plus an August 2026 backport of ITGmania's MIT-era engine fixes.
- **Wii fit:** ITGmania faces the same barriers as SM5.
  - Its renderers are desktop OpenGL, Direct3D and a shader-based GLES2. None maps onto the Wii's fixed-function GX.
  - Video needs FFmpeg, although a null movie driver exists.
  - Themes are Lua 5.1 (Simply Love, GPLv3).
  - It has about 1,000 source files, and desktop-only sound and input drivers.

Option A stays at 1/5 with ITGmania. Its value to us is as a source of fixes and conventions (see below).

## Option A′ — Finish StepMania-Wii (SM 3.9 port in your workspace)

| Criterion | Assessment |
|---|---|
| Technical feasibility | Low–medium. Smaller than SM5, and a Wii arch layer already exists. A 2012 alpha booted without sound, but the author never got the audio driver stable. It depends on Metaphrasis, which isn't packaged (Lua 5.1 is) |
| Development required | Large and open-ended: rebuild Metaphrasis or rewrite texture upload, fix 2005 C++ for a 2026 GCC, rewrite the audio driver, then audio sync, GX gaps and memory tuning. Every step is a hardware/emulator debugging loop with unknown depth |
| Difficulty | High |
| Performance | Unknown. SM 3.9 ran on 2005 PCs (and the original Xbox, which has 64 MB), so it's not impossible |
| Maintainability | Poor. Large legacy codebase, dead upstream |
| Reusable code | The whole engine, if it works. Realistically: `TimingData`, `MsdFile`, `NotesLoaderSM`, the ASND driver structure |
| Custom charts | `.sm`/`.dwi` good. No `.ssc` |
| GC dance pads | Yes (existing `InputHandler_Wii`) |
| **Feasibility** | **2 / 5** |

## Option B — New lightweight Wii-native engine (devkitPPC + libogc + GRRLIB)

| Criterion | Assessment |
|---|---|
| Technical feasibility | High. Every building block is packaged, current and documented: GX/GRRLIB, ASND, libmad/Tremor, libfat, PAD |
| Development required | Moderate code volume, all of it routine for AI-assisted development. The pace is set by the hands-on milestones (pad test, audio-sync checks, play-testing), as described in doc 05 |
| Difficulty | Moderate. The hard part is timing, and that's a contained problem |
| Performance | Excellent. A 2D four-lane game is trivial for the hardware |
| Maintainability | Good. Small codebase, you own it, platform-independent core unit-tested on the PC |
| Reusable code | ITGmania's current timing, chart, input and judgement code (GPLv3) ported in, with SM 3.9 as a readable reference. Dance Clone's code (GPLv2+) is usable too, but mostly as a behavioural reference |
| Custom charts | `.sm` and `.ssc` (single + per-chart timing), designed in from day one |
| GC dance pads | Yes, standard `PAD_*` API |
| **Feasibility** | **4 / 5** |

## Option C — Adapt Dance Clone v0.68 (or StepMania-Wii) as the base

| Criterion | Assessment |
|---|---|
| Technical feasibility | Medium for Dance Clone. It ran on the Wii, but a modern build needs patching (SDL 1.2 package paths, Boost, and the MP3 buffer use-after-free). StepMania-Wii is covered under A′ |
| Development required | A short rebuild effort, then substantial refactoring: replace the wall-clock sync, rewrite the `.sm` parser (no STOPS, layout-fragile, rejects StepMania 5 files, rewrites files), break up the 94 KB `Game.cpp`, likely replace SDL 1.2 software rendering |
| Difficulty | Moderate, but spent fighting legacy code rather than building |
| Performance | Acceptable (it shipped), but limited by 16-bit SDL software blits |
| Maintainability | Fair to poor (monolith, hard-coded config) |
| Reusable code | Input, jump grouping, judgement/energy logic, GUI, records |
| Custom charts | Needs a parser rewrite anyway |
| GC dance pads | **Proven** |
| Licence | GPLv2+ headers, which fit the project's GPLv3 choice |
| **Feasibility** | **3 / 5.** It works, but you'd end up rewriting most of it |

## Option D (recommended) — Hybrid: Option B, built with the best parts of both codebases

Build a new, small, layered engine (Option B) and **deliberately harvest**:

| From | What | How |
|---|---|---|
| ITGmania tag `v1.3.0` (2026‑07‑01, GPLv3) | `MsdFile` (with escapes), `TimingData` + `TimingSegments` (BPM, stop, delay, warp, speed, scroll, fake and more, with the 2026 lookup-table and time-signature fixes), `NotesLoaderSSC` and `NotesLoaderSM` (which turns negative BPMs and stops into warps) | **Port** into platform-independent C++ with our own structs. Skip the Lua bindings, editor helpers and `RString` scaffolding. Keep the copyright notices and mark our changes. This replaces extending SM 3.9 ourselves for `.ssc` |
| ITGmania `v1.3.0` (GPLv3) | Pad debounce (`InputFilter`), the early-hit rule against "way off towers", held-miss tracking, `pack.ini` handling (`Group`), and later StepParity/TechCounts/MeasureInfo | **Port** the logic. Details below |
| StepMania 3.9 upstream (`stepmania/stepmania` tag `v390release`, MIT) | The same timing and parser components, about a fifth of the size | **Readable reference** for the core beat↔time idea. MIT code can sit inside a GPLv3 project |
| StepMania-Wii (GPL v2, maybe v2-only) | `RageSoundDriver_Wii` double-buffered ASND streaming shape | **Re-implement** the shape only, without copying code. GPLv2-only code can't be combined with GPLv3, and its position logic is broken anyway. Use a ring buffer, IRQ-safe handoff, `ASND_TestPointer` before refills, and a position from consumed frames |
| Dance Clone (GPLv2+) | PAD-based dance-mat input, mat button map (+/− need a hardware check), jump grouping, judgement-window feel, reset/power callback pattern | **Usable under GPLv3**, but mostly a reference: the code is small and buggy, so re-implementing (about 100 lines) is simpler. It judges the earliest note, not the nearest, so don't copy that |
| Dance Clone | v0.5 `boot.dol` and a patched v0.68 build | **Optional test tools:** a known-good DDR to compare mat behaviour and feel against (run in Dolphin, or on the Wii via HBC) |
| Neither | Rendering, menus, config, persistence | Write fresh on GRRLIB, keep it small |

| Criterion | Assessment |
|---|---|
| Technical feasibility | High |
| Development | As Option B. Porting the proven timing and parser code removes the riskiest design work, and the oracle tests make it verifiable without hardware |
| Difficulty | Moderate |
| Performance | Excellent |
| Maintainability | Good |
| Custom charts | `.sm` + `.ssc` |
| GC dance pads | Yes |
| **Feasibility** | **4.5 / 5** |

---

## ITGmania: what to borrow

*Checked 2026-11-08 against [itgmania/itgmania](https://github.com/itgmania/itgmania) (branch `release`, v1.3.0) through the GitHub API. Nothing was built or run.*

### Licence: it depends on the version

- **Up to tag `v0.9.0` (2024‑06‑18), ITGmania used StepMania's MIT-style terms.**
  - The README said "you can do anything you like with the game".
  - `Docs/Licenses.txt` carried StepMania's MIT permission notices.
  - The source files still carry "(c) 2001-2004 Chris Danford, Glenn Maynard".
- **On 2024‑09‑10 the licence changed to GPLv3** (commit `9a288e1b73`, "Change license to GPL v3"). v1.0.0 (2025‑03‑13) and later are GPLv3. The README now says "All of our source code is under the GPLv3 license".
- **StepMania 5 only has some of the MIT-era fixes.** Its August 2026 backport (`6dcb310c21`) describes itself as "all the MIT licensed commits from ITGmania" from 2021–24, minus ITG-specific changes. It doesn't include the sync-drift fix below, so `v0.9.0` is the better MIT snapshot.

So anything at or before `v0.9.0` is MIT, and anything newer is GPLv3. The features added since then (`pack.ini`, StepParity, TechCounts, MeasureInfo) are all absent from the last pre-GPL commit, so they are GPLv3 only. **Since this project is GPLv3 (decision below), we can port from the current release, `v1.3.0`.**

### What's worth taking

| Item | Where in ITGmania | Licence | Why it matters to us | Verdict |
|---|---|---|---|---|
| SM5-era chart and timing code | `src/MsdFile.cpp`, `TimingData.cpp/.h`, `TimingSegments.cpp/.h`, `NotesLoaderSSC.cpp`, `NotesLoaderSM.cpp`. At `v0.9.0` they were 227, ~1,970, ~1,060, 1,249 and 1,558 lines | GPLv3 (MIT up to `v0.9.0`) | It is the reference behaviour that real simfiles, and our `simfile` oracle, follow. It covers every `.ssc` segment type and split timing, and handles escapes (including a 2024 fix, `1d332bf960`). The `.sm` loader also turns negative BPMs and stops into warps (`SMLoader::ProcessBPMsAndStops`), a trick ITG-era packs rely on that SM 3.9 doesn't handle | **Port from `v1.3.0`:** `GetBeatInternal`, `GetElapsedTimeInternal`, `GetDisplayedBeat` (speeds and scrolls), the lookup tables, and the tag handlers. Skip the Lua, editor and cache code. This replaces writing `.ssc`, DELAYS and WARPS support ourselves |
| Pad debounce | `InputFilter.cpp`. Default `InputDebounceTime` has been 20 ms since 2022 (`b6cc8f90fc`) | MIT | The code comment says "For pad play, a value of 20ms-50ms seems to result in a better experience." A press or release is reported at once with its original timestamp, then further changes are held back for the window. A release followed quickly by a re-press is never reported | **Port** the logic into `WiiInput` (a few lines). Measure the mat's bounce in M3 |
| Early-hit rule ("way off towers") | `Player.cpp` + `NoteTypes.h`, commits `27c6f7757a` (2023) and `23ddf9c155` | MIT (also in current GPLv3 code) | An early hit graded worse than Great doesn't use up the note. The grade is remembered, life changes once, and the player can re-hit for a better grade. If they don't, the note keeps the early grade instead of a Miss. This stops a mash or an early pad bounce from eating notes | **Port** into `JudgeEngine` (it's woven through `Player.cpp`, so expect to adapt rather than copy). Make it an option, on by default |
| Held-miss tracking | `f18695df59` (2021) | MIT (also in current GPLv3 code) | Flags a miss while the lane's panel was held down. That points to a stuck or dead panel rather than a player error | **Port** into padtest and the results screen (M3, M6) |
| Sync-drift fix | `RageTimer` + `RageSoundPosMap`, `705a069400` (2024) | MIT | SM5's audio clock drifted. The fix uses 64-bit values and exact µs→s conversion | **Reference only.** It confirms doc 06's approach: 64-bit ticks, `double` seconds, and a position counted from consumed frames |
| `pack.ini` | `src/Group.cpp` (October 2024 on) | GPLv3 | A `[Group]` section with `DisplayTitle`, `SortTitle`, `TranslitTitle`, `Series`, `Banner`, `Year` and **`SyncOffset=ITG` or `NULL`**. Many community packs are synced with ITG's 9 ms bias, and ITGmania shifts those by −9 ms. A `DefaultSyncOffset` preference covers packs that have no `pack.ini` | **Port** `Group`'s parsing (218 lines) into `SongLibrary` |
| Later timing fixes | `7137e7c2e6` (time signatures in `NoteRowToMeasureAndBeat`), `afbc74243b` (global offset baked into cached song bounds), `e742323d38` (lazy lookup tables) | GPLv3 | Real bugs fixed after `v0.9.0` | **Included** by porting from `v1.3.0` |
| StepParity, TechCounts, MeasureInfo | `StepParity*.cpp`, `TechCounts.cpp`, `MeasureInfo.cpp` (~1,700 lines) | GPLv3 | Foot-placement analysis (crossovers, footswitches, jacks) and stream breakdowns for song select | **Port later** (after M5), as an optional song-select feature |

**Not useful for the Wii:**
- the renderers, the FFmpeg movie code, and the sound and input drivers
- Lua theming and Simply Love (GPLv3)
- GrooveStats and the other networking code
- parallel song loading
- courses, and any mode beyond `dance-single`

**Bundled songs:** they are CC‑NC (the StepMania 5 folder) or "provided free of charge" by Club Fantastic. They make good test songs on your own SD card, but don't bundle them with the game.

### Licence decision: GPLv3

**Decided 2026-11-08: the project is licensed GPL-3.0-or-later**, the same terms as ITGmania. The full text is in `LICENSE` at the repository root.

**What it lets us do:**
- Port current ITGmania code directly (`v1.3.0`), StepParity included.
- Use Dance Clone's GPLv2-or-later code where it helps.
- Keep using MIT, zlib and BSD code (StepMania 3.9/5, GRRLIB, libogc, libfat, Tremor). Those can all sit inside a GPLv3 project.
- libmad (GPL v2 or later) is compatible as well.

**What it requires:**
- **Every new source file** gets a header with `SPDX-License-Identifier: GPL-3.0-or-later` and a copyright line.
- **Ported files** keep their original copyright and permission notices, and add a note that we changed them, with the date (GPLv3 §5a). Record the upstream file and tag (for example `itgmania v1.3.0 src/TimingData.cpp`).
- **Every distributed `.dol`** ships with its source, or a link to the exact tagged commit, plus the licence text.
- **Dependencies must be GPLv3-compatible.**
  - GPLv2-*only* code is not. That rules out copying StepMania-Wii's code, whose licence is just "gpl2".
  - FreeType (pulled in by GRRLIB for TTF) is dual-licensed. **[exp: confirm its licence suits a GPLv3 binary when M2 adds fonts]**

**What it costs:** MIT projects (StepMania 5, Etterna, permissive homebrew) can't take our code. Switching back to MIT later would need every contributor's agreement.

**Songs, art and fonts are separate.** Their own terms still apply, so we still don't ship Dance Clone's art, the CC‑NC songs, or ITGmania's Simply Love assets without checking each one.

---

## Recommended architecture (summary; full design in [06](06-technical-design.md))

```
┌───────────────────────── game (screens) ─────────────────────────┐
│  Title → SongSelect → DifficultySelect → Gameplay → Results       │
└───────────────▲───────────────────────────────▲───────────────────┘
                │ uses                          │ uses
┌───────────────┴─────────── core (portable C++, unit-tested on PC) ┐
│  chart/  MsdParser, SmLoader, SscLoader  → Chart{TimingData,Notes}│
│  timing/ TimingData (BPM/STOP/DELAY/WARP), SongClock interface    │
│  judge/  JudgeEngine (windows, nearest-note, holds), Score, Combo │
│  songs/  SongLibrary (scan, pack.ini, cache), HighScores          │
└───────────────▲───────────────────────────────▲───────────────────┘
                │ interfaces                    │
┌───────────────┴────────── platform/wii (libogc, GRRLIB) ──────────┐
│  WiiVideo (GRRLIB/GX)  WiiInput (PAD_*)  WiiAudio (ASND stream +   │
│  libmad/Tremor decoders, sample clock)  WiiFs (libfat)  WiiSystem  │
└───────────────────────────────────────────────────────────────────┘
```

**Key decisions**

1. **C++17, used conservatively** (no exceptions or RTTI in hot paths, no iostreams). This gives `std::vector`/`std::string` for parsing and keeps the code easy to port from StepMania. devkitPPC's GCC supports it.
2. **The audio clock is the master clock.** Song time comes from the number of samples the DSP has consumed, smoothed with the CPU timer. It never comes from frame counts or a free-running wall clock (Dance Clone's main weakness).
3. **Note times are precomputed in seconds at load.** Judgement compares input timestamps with note times. Rendering converts the current song time to a beat for scroll positions, which makes stops freeze arrows correctly.
4. **GRRLIB for 2D.** It's MIT-licensed, wraps GX, loads PNGs and renders TTF text. We can drop to raw GX later if needed.
5. **The portable core builds on the PC as well.** The parser and timing are unit-tested with the host compiler against the `simfile` Python library as an oracle, so most logic bugs never need Dolphin to find.
6. **Use devkitPro libogc, not libogc2.** It's the standard install, and libogc2 brings no dance-mat advantage.
7. **The project is GPL-3.0-or-later**, so current ITGmania code can be ported directly, keeping its notices and marking our changes. See the licence decision above.

**Final recommendation: Option D (Hybrid).** Don't port an engine, and don't adopt either codebase wholesale.
