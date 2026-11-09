# Code review, 2026-11-09

*A full read of `game/source` (core/, platform/wii/, render/, screens/, main.cpp), `game/tests`, `tools/` and the Makefiles at `main` 48f35a3, with docs 05 and 06, CLAUDE.md and the two research sets checked against the code. Build (`make -C game`) and the PC tests (565 checks) both pass on this commit. Nothing was changed. Markers as in the docs: **[src]** read in source, **[mem]** from memory (verify), **[exp]** to be tested.*

**Summary.** The game is in good shape: the core is small, portable and well tested, the threading is simple (one worker, one fill thread, two interrupt callbacks, all single-producer/single-consumer), and the ITGmania ports keep their notices and list their changes. No finding below breaks the game as it is. The real problems are robustness gaps on hardware (stack sizes, unhandled Reset/Power, no SD retry), a few latent races and edge cases, heavy duplication across the screens, and some doc rows that were never closed. Part 2 (the graphics plan) depends on the duplication being folded into `screens/MenuUi` and `render/`, so that is also the first thing to do.

---

## 1. Findings, ranked

Severity: **M** = could cause a rare hang, corruption or data loss on hardware; **L** = wrong in an edge case or against a stated rule, harmless day to day; **N** = tidy-up. Effort: S = under an hour, M = an afternoon, L = a day or more.

| # | Sev | Where | What goes wrong | Suggested fix | Effort |
|---|---|---|---|---|---|
| 1 | M | `platform/wii/WiiAudio.cpp:532` | The fill thread gets a **16 KB stack** and runs libmad (`mad_frame_decode` keeps several KB of subband arrays on the stack **[mem]**) and Tremor, while interrupts (SI poll with `PAD_Read`, the ASND DSP callback and mixer) run on whatever stack is current **[mem]**. An overflow corrupts the heap block below the stack silently; Dolphin behaves the same, but the damage shows up elsewhere and later. This is the kind of thing that gives a one-off freeze. | `LWP_CreateThread(..., 64 * 1024, 80)`. Memory is not a constraint. Same for `SongMedia` (64 KB is fine, libpng/libjpeg are modest). | S |
| 2 | M | `WiiAudio.cpp:450-455` | `g_bytes[i]`, `g_frames[i]` (plain) are written before the volatile publish `g_filled = g_filled + 1`, but `volatile` doesn't stop the compiler moving plain stores past it. If GCC ever sinks them, the interrupt queues a chunk with a stale length. `WiiInput.cpp:46` already uses a compiler barrier for the same pattern. | Add `__asm__ __volatile__("" ::: "memory");` before the publish (and before `g_queued`/`g_finished` updates in the callback, which read `g_bytes`). | S |
| 3 | M | `main.cpp` (nowhere), doc 06 §8 | **Reset and Power buttons are not handled** (doc 06 §8 says "not built yet"). On the Wii the buttons do nothing; `config.ini`/`scores.txt` changes pending in `Session` are never flushed; Dolphin's Stop has to be forced (memory note). | `SYS_SetResetCallback` / `SYS_SetPowerCallback` set a flag; `runScene` treats the flag like the exit chord; after the loop, Power calls `SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, 0)`, Reset returns to the loader. ~20 lines. | S |
| 4 | M | `main.cpp:80` | `fatInitDefault()` is called once and its result ignored. doc 06 §8 planned a retry of up to ~2 s (slow cards). A failed mount means no config, no scores, "can't open sd:/ddrwii/songs" and, worse, the first save later **creates** files on nothing. | Retry every 100 ms for 2 s; if it still fails, keep `session.scanned = false` and show one hint on the title ("No SD card"). Also skip saves when the mount failed. | S |
| 5 | L | `WiiInput.cpp:136-150` | The per-frame `PAD_Read` fallback only triggers if **no** SI sample has ever arrived. If every pad is unplugged mid-session, the SI poll stops (no enabled channel), no samples come, `missing_all` stays 0, `PAD_Reset` is never called, and a re-plugged pad is never found until reboot. Booting with no pad is fine (fallback), hot-plugging a second pad is fine (the first pad's samples carry the NO_CONTROLLER bit). | Remember the time of the last sample; if none for 0.5 s, do the fallback read (which re-probes). ~5 lines. | S |
| 6 | L | `WiiAudio.cpp:531-533`, `screens/SongMedia.cpp:31` | `lwp_t t;` is uninitialised and `LWP_CreateThread`'s result is ignored. On failure (no memory for the stack) `lwp_` holds garbage and `LWP_JoinThread(garbage)` runs later. | Check the return value; on failure, mark the stream/worker as dead and return false from `open()` / skip the join. | S |
| 7 | L | `WiiAudio.cpp:51-59` | The 24 MB song buffer is reserved lazily by `SYS_SetArena2Hi` from whichever thread first plays, usually the **SongMedia worker** (first preview), racing newlib's sbrk in theory. | Reserve it once in `main()` (or `AudioStream::initAsnd`) before any thread exists. | S |
| 8 | L | `screens/Gameplay.cpp:514, 529-537` | `playerLabel()` builds a `std::string` every frame per player (over the 15-char SSO limit, so a heap allocation), against doc 06 §2 "nothing is allocated per frame during gameplay". `Judge::events_` also grows on the first events of a song. | Format into a `char[64]` on the stack; `events_.reserve(64)` in `Judge::load`. | S |
| 9 | L | `screens/MenuUi.cpp:46-54`, `Gameplay.cpp:337-343`, `Results.cpp:119-124`, the three test scenes, `Players.cpp:161,188`, `Title.cpp:30`, `Options.cpp:59`, `SongSelect.cpp:388,671` | Frame-counted timing: the exit chord is "120 frames" (2.4 s at PAL50), the pulsing edges and the Players blink step per frame, the wheel eases `0.35` per frame (20% slower at 50 Hz). The research's rule is time-based animation everywhere. | `ExitChord` and `quit_frames_` → `nowUs()` and 2 s; pulses → `sin(now_s * 6)` (as `Gameplay::drawPause` already does); wheel → `1 - exp(-dt / tau)` now, `core/anim` later. | S |
| 10 | L | `main.cpp:41`, `RenderTest.cpp:69`, `PlayfieldStress.cpp:74`, `AudioClockTest.cpp:88` | B to leave a test scene and START+Z inside them read **port 0 only**; everywhere else any pad works (CLAUDE.md menu grammar). A mat on port 2 alone can't leave the test scenes except by waiting... it can't. | Use `ui::ExitChord` and check B on all ports (`input.down(p)` loop). | S |
| 11 | L | `screens/SongSelect.cpp:185-190` | The "Players are chosen on the Players screen" hint fires on `A | START` from a non-playing port, including the START of the START+Z exit chord, so exiting from song select with a spare pad beeps the error. | Use `ui::startPressed(input_, port)`. | S |
| 12 | L | `render/Banner.cpp:171`, `Banner.h:142-143` | `readFile` accepts files up to 16 MB and `kMaxPixels` allows an 8 MB RGBA buffer, both on the worker: up to ~24 MB transient. The build has exceptions **enabled** (`__EH_FRAME_BEGIN__` is 217 KB in the ELF) and nothing catches `std::bad_alloc`, so a failed `resize` aborts the game with no message. | Cap the file at 4 MB (a 2 Mpx PNG banner is well under); or `try { ... } catch (const std::bad_alloc&) { return nullptr; }` around the decode. Consider `-fno-exceptions -fno-rtti` (the code already avoids `std::stoi` for this reason, `StringUtil.cpp:19`); it also saves ~300 KB of MEM1. | S |
| 13 | L | `game/tests/test_main.cpp:556-618` | The fuzz pass covers simfiles only. `HighScoreTable::parse`, `ParseConfig`, `IniFile`, `SongCache::Parse` read hand-editable SD files with unit tests but no fuzzing. A crash there would stop every boot. | Run the same truncate/mutate loop over a scores.txt, config.ini, Pack.ini and cache.txt sample. ~20 lines. | S |
| 14 | L | `screens/SongSelect.cpp:42, 108-116` | `kStartFolder = "Click 120"`: on the first visit song select always jumps to the test song if it exists ("the hardware checklist's test song"). Dev convenience in shipping code. | Owner's call. Simplest: drop it; nicer: remember the last pack/song in `config.ini`. | S |
| 15 | L | `screens/OffsetCalibration.cpp:55-64` | Writes `config.ini` directly with `SaveConfig` while everything else sets `session.config_dirty` and lets `main.cpp` write. Two write paths, two messages. Harmless. | Set `config_dirty` and `session.message`; let `main.cpp` save when the scene returns. | S |
| 16 | N | `screens/Players.cpp:94-102, 180-181`, `Players.h:50` | Dead since slice 4: the `lead_ < 0` branch ("Press A or an arrow on your pad") can't run because the Title always sets `session.lead_port`. | Delete, or keep only as a guard with the comment fixed. | S |
| 17 | N | `screens/SongSelect.h:38` | Forward-declares `render::ArrowArt`, unused. | Delete. | S |
| 18 | N | `screens/Results.cpp:198` | A long title at 2× (16 px/glyph) runs past 640 px after 36 characters; no `printFit`. | Cut to 30 characters as the popups do. | S |

### 1.1 Duplication (architecture)

Every screen carries its own copy of the same pieces. None of this is a bug; it is what makes a second look expensive, so it comes first in the graphics plan (PR 1 there).

| Piece | Copies | Where |
|---|---|---|
| Palette (`kBg 0x101828FF`, `kBox`, `kDark`, `kWhite`, `kGrey`, `kPink`/`kPlayerColour[2]`) | 7 | Title, Players, Options, SongSelect, Results, Gameplay, MenuUi (`kHelpGrey`) |
| `printCentred` | 6 variants | MenuUi, Gameplay, Players, Results, SongSelect (std::string + fit), OffsetCalibration (x fixed) |
| `printRight` | 2 | Options, Results |
| `gradeColour` | 2 identical | Gameplay.cpp:40, Results.cpp:26 |
| `withAlpha` | 2 identical | Gameplay.cpp:61, PlayfieldStress.cpp:35 |
| `startPressed` (START without Z) | 3 | MenuUi.cpp:56, Results.cpp:60, Gameplay.cpp:201 |
| Exit chord (START+Z 120 frames) | 6 | `ui::ExitChord` plus hand-rolled `quit_frames_` in Gameplay, Results, RenderTest, PlayfieldStress, AudioClockTest |
| Pulsing selected edge (`0.6 + 0.4 sin`) | 4 | Title `choiceBox`, Players, Options `choiceRow`, Gameplay pause (time-based, the only correct one) |
| Hint box at y 440 | 3 | `ui::Hint`, Results.cpp:222-228, Gameplay.cpp:590-594 (both could use `ui::Hint`) |
| Header bar (`rect 0,0,w,46` + title) | 6 | Options `header()`, SongSelect, Players, Title (none), Results, OffsetCalibration |
| `kLaneDegrees[4]` | 5 | Title, Gameplay, OffsetCalibration, RenderTest, PlayfieldStress |
| `kLaneButton[4]` | 3 | Gameplay, RenderTest, PlayfieldStress |
| Player message (2 s yellow text) vs `ui::Hint` | 1 | Players.cpp:75-79, 201-202 |

### 1.2 Architecture notes (not defects)

- **Scene flow.** `main.cpp` is a hand-written state machine: each screen is constructed on the stack, run by `runScene`, and reports its result through getters (`picked()`, `back()`, `next()`). It is clear and it works. Its limits are what the research's `SceneManager` addresses: no transitions, every screen reconstructs itself (SongSelect rescans its items, SongMedia restarts its thread, Gameplay reads the song on the main thread so the picture freezes for the SD read), and results travel as 3–4 booleans per screen. Fine to keep until phase 4 of the plan.
- **`Session`** as the shared bag works well; `config_dirty`/`scores_dirty` with `main.cpp` saving at scene boundaries is a good rule (except #15).
- **Core boundary.** `core/` is clean (no libogc). Screens hold logic that could be portable and PC-tested but isn't worth moving yet: the wheel item list and wrap-around (`SongSelect::buildItems/moveCursor`), the popup cursor rules, the Results name entry. The wheel model will move to `core/ui` anyway with ITGmania's `WheelMotion` (plan PR 4).
- **Threading** is sound: `WiiInput` ring (IRQ → main, barriers in place), `WiiAudio` ring (fill thread → IRQ, see #2), `SongMedia` (mutex, no blocking under the lock, lock order mutex → malloc only). The "one `AudioStream` at a time" rule is enforced only by `main.cpp`'s scoping (SongSelect is destroyed before Gameplay/OffsetCalibration are built) and the class comment. A `static bool g_open` assert-style guard in `AudioStream::open` would make it explicit (S).
- **Texture lifetimes** are safe because `WiiVideo::endFrame` blocks on `GX_DrawDone` every frame, so a texture freed at the start of a frame (`SongMedia::request`) is never still being read. The research's per-scene arena freed after `GX_DrawDone` (Nintendo pattern 18) is therefore unnecessary while the present path stays blocking; see Part 2.
- **Uninitialised state** that Dolphin's zeroed RAM would hide: none found. Every member has an initialiser; the only uninitialised locals are the two `lwp_t t` (#6) and arrays filled before use.
- **Memory.** The ELF is 1.85 MB text + 0.65 MB data + 0.34 MB BSS (`powerpc-eabi-size`), the main stack 128 KB. The 24 MB song buffer sits at the top of MEM2; newlib's heap starts in MEM1 and spills into MEM2 **[mem: libogc's `__libogc_sbrk_r`]**. There is no heap display yet (doc 06 §8 asks for one); the plan's banner cache needs one, so add `SYS_GetArena1Size/2Size`-style numbers to the Options Test scenes or the gameplay HUD when that work starts.
- **Interrupt-level code** (`WiiInput::onPoll`, `WiiAudio::voiceCallback`, `tickCallback`) is small and allocation-free as it should be.

### 1.3 The unexplained hardware freeze (doc 05 M6, commit 7673460)

A on the Players screen froze the game with the Players screen still shown; the retry worked. From the code, the picture on screen is the last presented frame, so the hang happened between Players' last `endFrame()` and SongSelect's first: `saveConfigIfChanged` (a no-op on that path), the `SongSelect` constructor (`buildItems`, `SongMedia` thread creation), SongSelect's first `frame()` (no banner, no preview yet), or its `endFrame()` → `GX_DrawDone()`. Nothing in that path blocks or allocates much, and nothing differs between the first and the second try. Reading the code gives no single cause. Ranked guesses:

1. **Earlier corruption** from #1 (fill-thread stack) if a preview or song had played before that moment in the same boot. Ask: was it the first time song select was opened after boot?
2. **An SI/PAD edge** that leaves input dead while the main loop runs (doc 05 already asks whether the pink edge still pulsed; the pulse is frame-driven, so a moving edge means the loop was alive and the pads were not).
3. **A GX hang in `GX_DrawDone`** from a bad quad. Nothing in SongSelect's first frame draws anything unusual, so this is the least likely.

Cheapest diagnostic for the next hardware session: fix #1, #2 and #6 (they are one-liners), and add a frame counter to the header bar of every menu (it doubles as the heartbeat the research's debug overlay wants). If it freezes again, the counter says whether the loop is alive.

### 1.4 Doc drift

| Doc | Says | Code / status | Fix |
|---|---|---|---|
| README docs table | No row for `research/` | Two research sets exist; both ask for a row | Add one row |
| doc 05 M3 status | "Hardware check pending: the render test's `input` line shows the real-Wii sample rate"; bounce duration not measured | Later full builds passed on hardware; the rate and bounce were never written down | Close the rows ("superseded by the M5/M6 runs") or record the numbers next time |
| doc 05 M4b status | "The tick-driven build (2c77f2c and later) still needs its hardware run" | M5 and the menu flow (tick-driven) passed on hardware | Close the row |
| doc 05 M5 step 2 | "Hardware still open: the 20-song-loads check of the MEM2 buffer" | Not explicitly reported | Close it after the next long session, or drop it |
| doc 06 §2 diagram | "PAD sampling callback -> diff buttons, push timestamped LaneEvents"; no SongMedia worker, no ASND tick callback | The SI handler pushes raw samples; diffing and debounce happen in `update()`; the worker (prio 40) and the tick callback exist | Redraw the diagram (5 lines) |
| doc 06 §4 table | "SFX: WAVs preloaded and played on free voices (as in Dance Clone)" | Menu sounds are generated in code on voice 1 | Replace the row |
| doc 06 §3.5 | "START saves it to config.ini" (calibration) | A or START saves (`OffsetCalibration.cpp:110`); §7 says A | Say "A or START" in one place |
| doc 06 §5 | "`GRRLIB_PrintfTTF` ... [exp]" | Never used; the research found it unusable per frame | Note it, or leave until the font work rewrites §5 |
| CLAUDE.md | "the Makefile only has a build rule for `.jpg`" | True (`game/Makefile:141`) | Stays true until the asset work |
| research set 1 §3, §6 step 4 | `PlayModeSelect`, "port PlayModeSelect first", players join by port in song select | Players screen, Title, Options, Results, popups, MenuUi exist; PlayModeSelect is gone | Part 2 updates the plan; the research files themselves can stay as the record |

### 1.5 Readiness for the graphics plan (Part 2)

- **GX/GRRLIB outside `render/` and `platform/`:** `GRRLIB_SetAntiAliasing` in 11 screen files (toggled around bitmap text), raw `GRRLIB_Rectangle`/`GRRLIB_DrawImg` in `RenderTest` (7) and `AudioClockTest` (3), `GRRLIB_FreeTexture` in `SongMedia` (6), and `<grrlib.h>` included by every `render/` header and `SongMedia.h`, so `GRRLIB_texImg*` is the texture type everywhere. A `render::Texture` handle plus `render::setTextureFilter()` would close the leak in one PR.
- **Per-frame easing instead of clocks:** listed in #9.
- **Hard-coded layout:** every screen uses absolute 640×480 coordinates with its own constants; the safe-area margins (x 32–608 / 64–576, y 24–456 / 48–432, doc 06 §11.3) are comments, not code. There is no shared layout table except `PlayfieldStress::layoutFor`.
- **Already in place and reusable:** `render::Draw` counters (draws, texture switches, area) and `WiiVideo` timing (CPU, GPU wait, dropped frames) give the plan's budgets for free; `core/input/MenuRepeat` is the research's `RepeatTimer`; `Judge::events_` is the Nintendo set's judgement event ring in all but name; `SongEntry::titleTranslit` is the CJK fallback the font work needs; `SongMedia` is the worker the banner cache extends; `GRRLIB_LoadTextureTPL`, `GRRLIB_CreateEmptyTextureFmt` and `gxtexconv` (`C:\devkitPro\tools\bin\gxtexconv.exe`, `%.tpl : %.scf` in `wii_rules:23`) are all present **[src]**.

---

## 2. What I'd do first

1. **One small-fixes PR** (all S): #1 fill-thread stack, #2 barrier, #6 thread-create checks, #7 reserve the song buffer at boot, #3 Reset/Power callbacks, #4 SD retry, #5 pad re-probe, #8 no per-frame string, #10/#11 any-port B and START. Build, Dolphin smoke run (title → song → results → exit, pause, Options tests), then it rides along with the next hardware session as the freeze diagnostic (§1.3).
2. **Fuzz the SD text parsers** (#13) in `game/tests`: ten minutes, and it protects boot.
3. **Fold the duplicates into `MenuUi` and `render/`** (§1.1, #9, §1.5): no visible change, checked by pixel-identical Dolphin screenshots. This is PR 1 of the graphics plan, so it is written up there.
4. **Doc tidy** (§1.4): README row, close the stale doc 05 rows, fix the doc 06 §2 diagram and §4 SFX row. Ten minutes.

Then the graphics plan (`graphics_plan_2026-11-09.md`).
