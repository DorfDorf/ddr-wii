# 02 — Architecture Options and Recommendation

*Based on the code review in [07](07-existing-codebase-review.md) and the ecosystem research in [01](01-project-research.md). Date: 2026-11-08.*

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

## Option A′ — Finish StepMania-Wii (SM 3.9 port in your workspace)

| Criterion | Assessment |
|---|---|
| Technical feasibility | Low–medium. Smaller than SM5, and a Wii arch layer already exists, but it's unproven: it was never shown to link or boot, and it depends on missing libraries (Metaphrasis, old Lua-for-Wii) |
| Development required | Large and open-ended: rebuild missing libraries, fix 2005 C++ for a 2026 GCC, get it to boot at all, then audio sync, GX gaps and memory tuning. Every step is a hardware/emulator debugging loop with unknown depth |
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
| Reusable code | StepMania 3.9 algorithms (MIT) ported in. Dance Clone as behavioural reference |
| Custom charts | `.sm` and `.ssc` (single + per-chart timing), designed in from day one |
| GC dance pads | Yes, standard `PAD_*` API |
| **Feasibility** | **4 / 5** |

## Option C — Adapt Dance Clone v0.68 (or StepMania-Wii) as the base

| Criterion | Assessment |
|---|---|
| Technical feasibility | Medium for Dance Clone. It ran on the Wii, but a modern build needs patching (SDL 1.2 package paths, `dirData`, Boost, `SND_Init`). StepMania-Wii is covered under A′ |
| Development required | A short rebuild effort, then substantial refactoring: replace the wall-clock sync, rewrite the `.sm` parser (no STOPS, layout-fragile), break up the 94 KB `Game.cpp`, likely replace SDL 1.2 software rendering |
| Difficulty | Moderate, but spent fighting legacy code rather than building |
| Performance | Acceptable (it shipped), but limited by 16-bit SDL software blits |
| Maintainability | Fair to poor (monolith, hard-coded config) |
| Reusable code | Input, jump grouping, judgement/energy logic, GUI, records |
| Custom charts | Needs a parser rewrite anyway |
| GC dance pads | **Proven** |
| Licence | Copyleft (GPLv2+ headers), inherited by your project |
| **Feasibility** | **3 / 5.** It works, but you'd end up rewriting most of it |

## Option D (recommended) — Hybrid: Option B, built with the best parts of both codebases

Build a new, small, layered engine (Option B) and **deliberately harvest**:

| From | What | How |
|---|---|---|
| StepMania-Wii (MIT, SM 3.9 code) | `MsdFile.cpp` tokenizer, `NotesLoaderSM.cpp` logic, `TimingData` beat↔time algorithm | **Port** into platform-independent C++ with your own structs. Keep the MIT notice. Extend for `.ssc`, DELAYS and WARPS |
| StepMania-Wii | `RageSoundDriver_Wii` double-buffered ASND streaming pattern | **Re-implement** the pattern, fixing its flaws: no printf in the IRQ, ring buffer, sample-accurate position |
| Dance Clone | PAD-based dance-mat input, mat button map, jump grouping, judgement-window feel, reset/power callback pattern | **Reference only** (to keep your licence free). Re-implement, about 100 lines in total |
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
│  songs/  SongLibrary (scan, metadata cache), HighScores           │
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

**Final recommendation: Option D (Hybrid).** Don't port an engine, and don't adopt either codebase wholesale.
