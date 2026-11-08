# 07 — Review of the Existing Codebases (StepMania-Wii and Dance Clone Wii)

*Reviewed 2026-11-08. This review is based on reading the source in this workspace. Nothing was compiled or run, because no Wii toolchain is installed yet (see [Build feasibility](#5-build-feasibility)).*

*Revised 2026-11-08 after an audit ([07a-review-audit.md](07a-review-audit.md)). The audit added the Google Code Archive record for StepMania-Wii, checks against current upstream devkitPro, Dolphin and StepMania sources, and a closer reading of Dance Clone's chart loader and audio code.*

---

## 1. Repository inventory

### 1.1 Workspace layout (as found)

```
C:\Users\haler\Downloads\ddr-wii\           <- workspace root (NOT a git repository)
└── reference\
    ├── stepmaniawii.zip        34.7 MB  (original archive, untouched)
    └── danceclonewii.7z         6.8 MB  (original archive, untouched)
```

Both projects came as archives, so there was no source tree to inspect yet. To review them I extracted each archive into its own **new** folder and left the archives unchanged:

```
reference\extracted\
├── stepmaniawii\stepmania-wii\              <- unzip of stepmaniawii.zip
│   ├── .svn\                                 (Subversion 1.6-era working copy, format 10)
│   ├── trunk\        (584 entries)          <- the Wii port
│   ├── branches\original\                    <- StepMania 3.9 with the 2009–10 port edits + data tarball
│   ├── tags\         (empty)
│   └── wiki\Building.wiki
└── danceclonewii\
    ├── DanceClone v0.5 for Wii.rar           (from the .7z)
    ├── DanceClone-source.zip                 (from the .7z)
    ├── GameBrew.url                          (an internet shortcut to gamebrew.org)
    ├── source\DanceClone-master\             <- unzip of DanceClone-source.zip (v0.68 source)
    └── binary\DanceClone\                    <- unrar of the v0.5 release (boot.dol + assets)
```

> Only extraction tools were run (`unzip` and Windows `tar.exe`/libarchive). **No binaries or scripts from either project were executed.** The included `boot.dol` (v0.5) and the `StepMania-3.9a-linux.tar.gz` inside `branches/original` were not run.

### 1.2 StepMania-Wii — identity and history

| Item | Finding | Evidence |
|---|---|---|
| Upstream | `http://stepmania-wii.googlecode.com/svn`. Google Code shut down in 2016, but the **Google Code Archive** still hosts the project page, its source export and its downloads. Your `stepmaniawii.zip` (34,661,403 bytes) is the archive's `source-archive.zip` | `stepmania-wii/.svn/entries`, [project.json](https://storage.googleapis.com/google-code-archive/v2/code.google.com/stepmania-wii/project.json) |
| Committers | `eric.borbely` (r2, 2010) and `obsidianx@gmail.com` (r3–r11, 2012) | `.svn/entries` across 74 dirs |
| VCS | Subversion working copy at **r11**. There is no git history | `.svn/entries` |
| Commit timeline | r1 2009‑05‑21 (Google Code's empty structure; the repository UUID's timestamp matches) → **r2 2010‑04‑14** (`eric.borbely`: the 1,116 files now in `branches/original`) → r3 2012‑03‑30 (the `branches/original` directory changes, most likely a move of the 2010 import) → r5 2012‑03‑30 (trunk import; trunk holds 654 files) → r6 `RageDisplay_GX.cpp` → r7–r9 2012‑04‑02 (`arch_platform.h`, `RageUtil_CircularBuffer.cpp`, `GameSoundManager.cpp`, `Makefile`) → r10 2012‑04‑04 (wiki) → **r11 2012‑04‑12** (`StepMania.cpp`, `RageSoundDriver_Wii.*`) | per-file `committed-rev`/`committed-date` in `.svn/entries` |
| Earlier notes | Porting notes dated 2009‑04 (Linux build of SM 3.9 first, then GX/Wii) | `branches/original/Stepmania Port Notes` |
| Docs | `wiki/Building.wiki` (prerequisites: devkitPPC, Metaphrasis, Lua for Wii; then `make`). The archived project description is the best status report (see [Wii status](#wii-status-how-far-did-the-port-get)) | `wiki/Building.wiki`, `project.json` |
| Base engine | **StepMania 3.9** (2005). `branches/original` is **not** pristine 3.9. It already has the 2009–10 port edits (`_WII` paths in `config.h`, `global.h`, `StepMania.cpp`, `arch/*`, early `ArchHooks_Wii`/`InputHandler_Wii`, a custom Makefile). For pristine 3.9, use upstream `stepmania/stepmania` tag `v390release` | `branches/original/NEWS`, `log.txt`, `Stepmania Port Notes` |
| Licence | The project was published under **GPL v2** (archive metadata `"license": "gpl2"`). The inherited StepMania files carry StepMania's MIT text (601 of 637 `.cpp/.h`). The Wii-specific files carry **no licence text**. Bundled `pcre` (BSD) and `libresample` (LGPL) | `project.json`, file headers |
| Released binary | Not in the source archive, but the project's downloads list has **`Stepmania-alpha-SD.zip`**: "Alpha - No Sound Driver", 13.2 MB, uploaded 2012‑03‑30, still hosted. Not downloaded or run | [downloads-page-1.json](https://storage.googleapis.com/google-code-archive/v2/code.google.com/stepmania-wii/downloads-page-1.json) |

### 1.3 Dance Clone Wii — identity and history

| Item | Finding | Evidence |
|---|---|---|
| Upstream | https://github.com/lefrac01/DanceClone. The zip comment is commit `513768895055b3df7759cd1857f5a567ac50d4f6`, which is the GitHub HEAD ("v0.68…", 2013‑01‑16) | `unzip -z DanceClone-source.zip`, GitHub commit list |
| Authors | v0.1–v0.50 by **ThatOtherPerson** (2009–2010). v0.51–v0.68 by **Carl Lefrançois** (2012–2013) | `README`, `CHANGELOG`, `binary/DanceClone/meta.xml` |
| VCS | No `.git` in the zip. GitHub shows 41 commits, 1 open issue ("Prebuilt binaries request", 2018) | GitHub |
| Released binary | **v0.5** `boot.dol` (dated 2010‑07‑16), with `meta.xml`, `icon.png`, assets, a demo song (`Rekcahdam - Dance, NOW!.mp3`) and a `.mp3.dc` chart in v0.5's own format. **There is no binary of v0.68** | `binary/DanceClone/` |
| Docs | `README`, `COMPILING`, `CHANGELOG`, `README_SM_file_format(_extended)` (copies of the SM spec) | source root |
| Licence | `COPYING` = **LGPL 2.1** and the README says LGPL. **However, all 65 source files except `main.cpp` have a GPL v2-or-later header** ("Copyright 2012 Carl Lefrançois"). Treat the code as **GPLv2+** to be safe | `Source/**` headers |
| Size | 66 source files (33 `.cpp`, 33 `.h`). `Game/Game.cpp` (94 KB) and `Game/Song.cpp` (47 KB) hold most of the logic | file listing |

---

## 2. StepMania-Wii — architecture

**What it is:** a direct port attempt of the whole StepMania 3.9 engine (≈310 translation units, C++03, CString/STL, Lua 5.0-style API). The port **replaces the platform layer** ("arch" drivers) with Wii versions and keeps the rest of StepMania unchanged.

| Subsystem | Implementation | Source |
|---|---|---|
| Entry point | Stock StepMania `main()`. It constructs `RageDisplay_GX` first (which brings up a text console), then `MakeArchHooks()`, loads all managers (SONGMAN, THEME, etc.), then switches the console to GX | `trunk/StepMania.cpp:688-700`, `:841` |
| App lifecycle | `ArchHooks_Wii` calls `WPAD_Init`, `PAD_Init`, reset/power callbacks (`SYS_ResetSystem`) and `fatInitDefault()` + `chdir("sd:/")`. It hangs at "press HOME" if the SD card fails to mount | `trunk/arch/ArchHooks/ArchHooks_Wii.cpp:33-62` |
| Timer | `gettime()`/`diff_usec()` from libogc. `diff_usec()` returns a `u32`, so this time-since-start wraps after about 71.6 minutes | `ArchHooks_Wii.cpp:68-73` |
| Graphics | `RageDisplay_GX`: VIDEO/GX init, double XFB, direct-mode vertex format (pos/tex/colour), quads/fans/strips via `GX_Begin`. Textures are converted with **Metaphrasis** (`convertBufferToRGBA8`). Compiled geometry, lighting and sphere mapping are stubs. Max texture 1024 | `trunk/RageDisplay_GX.cpp` (597 lines; init `:92`, GX setup `:136-189`, textures `:285-330`, draw `:452+`) |
| Audio | `RageSoundDriver_Wii`: up to 16 ASND voices, each double-buffered (2 × 8 KB, about 43 ms). A worker thread (`SoundBufferThread`) fills buffers from StepMania's `GetPCM`. The ASND callback swaps buffers. The decoders come from StepMania (`RageSoundReader_MP3` via **libmad**, Vorbis via **tremor/vorbisidec**, WAV) and are resampled with bundled libresample. **The position query doesn't work:** it passes frame number 0 to every `GetPCM` (`:46`, `:116`), which StepMania needs to map driver time to song time, and it counts bytes queued rather than played (`:28`, `:122`). The author reports this driver crashed after a few seconds | `trunk/arch/Sound/RageSoundDriver_Wii.cpp` (`:36` thread, `:125` `ASND_SetVoice`, `:145` `GetPosition`) |
| Input | `InputHandler_Wii`: for channels 0–1 it ORs Wiimote (`WPAD_*`) and GameCube (`PAD_ButtonsDown|PAD_ButtonsHeld`) bits into StepMania joystick buttons JOY_UP/DOWN/LEFT/RIGHT, Start, Select(Z), A, B. HOME calls `exit(0)` | `trunk/arch/InputHandler/InputHandler_Wii.cpp:27-90` |
| Threads | `Threads_Wii` uses LWP (`LWP_CreateThread`, `LWP_JoinThread`, …). Every thread is created at priority 0, libogc's idle priority (`:54`). `TryLock` blocks (`:94-95`), and the semaphore can lose a wake-up (`:125-126`) | `trunk/arch/Threads/Threads_Wii.cpp` |
| File system | StepMania's RageFile over newlib/libfat (`sd:/`) | `RageFileDriverDirect*.cpp` |
| Menus/UI | All of StepMania 3.9's Screen* classes, driven by theme metrics. **This needs the full SM 3.9 data set** (Themes, NoteSkins, Characters, Announcers…), which lives only in `branches/original/StepMania-3.9a-linux.tar.gz` | `trunk/Screen*.cpp`, `branches/original/log.txt` ("'Characters/default' is missing") |
| Config | StepMania `PrefsManager`/INI | `PrefsManager.cpp` |
| Crash handling | `archutils/Wii/*` is a copy of the Unix crash handler (`fork`, `execve`). It is partly commented out and makes no sense on the Wii | `trunk/archutils/Wii/CrashHandler.cpp:82-317` |

### DDR functionality (inherited from StepMania 3.9)

These parts are full, mature StepMania implementations. They are excellent **reference code**:

- **Notes:** `NoteData` (tracks × rows, 48 rows per beat in SM 3.9), `NoteTypes`, holds.
- **Timing:** `TimingData` with BPM segments and stop/freeze segments. `GetBeatAndBPSFromElapsedTime()` (`TimingData.cpp:98`) and `GetElapsedTimeFromBeat()` (`:159`) are the core conversion functions (281 lines in total).
- **Charts:** `MsdFile.cpp` (the generic `#TAG:value;` tokenizer, 184 lines; handles `//` comments and missing `;`, but has no backslash escapes) and `NotesLoaderSM.cpp` (549 lines: `#OFFSET`, `#BPMS`, `#STOPS`/`#FREEZES`, `#NOTES`, all difficulties). DWI, KSF and BMS loaders are present too. **There is no .ssc support** (.ssc arrived with StepMania 5) and no DELAYS/WARPS.
- **Scoring:** `ScoreKeeperMAX2` (DDR MAX2 scoring), `Combo`, life meters, high scores via `Profile`/`HighScore`.
- **Difficulties:** all five, plus edits and courses.

### Wii status: how far did the port get?

| Question | Answer |
|---|---|
| Wii-specific? | Yes. `wii_rules`, `-DWII`, `arch_wii.h` |
| Toolchain | devkitPPC and libogc as of around 2012. The exact versions aren't recorded |
| Produces a .dol? | **Yes, at least without audio.** No `.dol`, `.elf` or `output/` folder was committed, but an alpha build was published on 2012‑03‑30 ("Alpha - No Sound Driver"). Trunk also `#include`s `RageUtil_CircularBuffer.cpp` into `GameSoundManager.cpp:66`, the usual workaround for template link errors |
| Ran on hardware/Dolphin? | **Yes, without sound.** The author's project description says the 2D drawing was tested with "everything but the health bar working", that ASND and AESND audio "both crash after a few second of audio playback", and that the uploaded build fakes the audio driver's progress. The 2009 log (`branches/original/log.txt`) is from the Linux build. The `emulate:` target points at **gcube**, a long-dead emulator, not Dolphin |
| Completeness | All 310 objects in `OFILES` have matching sources, so the tree is complete. But the Wii layer is early-stage: GX is half-implemented, the audio driver never worked (see the Audio row), there are `printf`s inside the audio IRQ callback (`RageSoundDriver_Wii.cpp:31`) and in `RageSound::GetPCM` (`RageSound.cpp:587-589`), a busy-looping fill thread (`usleep(0)`), `#elseif WII` (an invalid directive, harmless only because it sits in a skipped block, `StepMania.cpp:695`), and the crash handler uses `fork` |

**Bottom line:** StepMania-Wii is an **abandoned, early-stage port** of a 2005 engine. It booted and drew gameplay, but never had working audio. Work started in 2009–10 (r1–r2), stopped, was revived for two weeks in 2012 (r3–r11), and was last touched 2012‑04‑12.

---

## 3. Dance Clone Wii — architecture

**What it is:** a small, purpose-built DDR clone (C++98, SDL 1.2). It has a platform abstraction for Wii, Linux and Win32 (Win32 is only a stub in v0.68).

| Subsystem | Implementation | Source |
|---|---|---|
| Entry point / main loop | `main()` builds the platform `OS` object, `Constants`, `GUI`, `Dash` (menu) and `Game`. The loop runs `sys.Pump()` → GUI/dash update → `game.Run()` → drains SDL events → `VIDEO_WaitVSync()` (Wii). So it runs **one game update per video frame** | `Source/main.cpp:28-132` (loop `:86-123`, `VIDEO_WaitVSync` `:121`) |
| App lifecycle | `WiiOS::Init`: `fatInitDefault()`, `sleep(1)`, `WPAD_Init` + IR, reset/power callbacks that call `SYS_ResetSystem` from `Pump()` | `Source/Platform/Wii/WiiOS.cpp:41-66` |
| Graphics | **SDL 1.2 for Wii** software surfaces, 16-bit (`SDL_SetVideoMode(w,h,16,SDL_HWSURFACE|SDL_DOUBLEBUF)`), `SDL_BlitSurface` + `SDL_Flip`. PAL is forced to 480 lines. Images are loaded with SDL_image. Changelog v0.68 says it moved from alpha to colour-key (no reason given) | `Source/Platform/Wii/WiiVideo.cpp:31-70`, `Source/Platform/Video.cpp` |
| Audio | **Music:** the whole MP3 is read into RAM, then played with **libogc `MP3Player_PlayBuffer`** (libmad), busy-waiting until `MP3Player_IsPlaying()`. **Bug:** the buffer is then freed at once (`Sound.cpp:169-172`), but MP3Player keeps reading from the caller's pointer for the whole song (libogc `libmad/mp3player.c`), so this is a use-after-free. **SFX:** WAVs loaded by its own `WAVSample` and played on a free **ASND** voice | `Source/Game/Sound.cpp:47-52, 128-178, 240-250` |
| Input | `WiiInput::Update`: calls `WPAD_ScanPads` and `PAD_ScanPads` once per frame. The return bitmask of `PAD_ScanPads` sets `danceMatActive`. It maps `PAD_BUTTON_UP/DOWN/LEFT/RIGHT` from `PAD_ButtonsDown` (edge) and `PAD_ButtonsHeld` (level) into `directionDown[4]`/`directionHeld[4]` **per channel** (4 input channels; the game allows 2 players), plus A/B/Start/Z and Wiimote IR for the cursor | `Source/Platform/Wii/WiiInput.cpp:58-126` |
| File system | POSIX `opendir/readdir/stat` on the default libfat device. `ReadDirectory` never calls `closedir`, and `fatInitDefault()`'s result is ignored. Songs are discovered as `*.mp3` files directly in `Music/` (a flat folder, not StepMania's `Songs/<pack>/<song>/` layout). Each song is `Name.mp3` + `Name.sm` + `Name.records` side by side | `WiiOS.cpp:68-118`, `Game/Song.cpp:183-232`, `Game/Game.cpp:1248-1275`, `Constants.cpp` (`musicFileRoot = "Music/"`) |
| Menus/UI | Its own retained-mode GUI ("Gooey": Container/Button/Label/Image/FontBitmap, TTF via SDL_ttf), a "Dash" menu, and song wheel/difficulty/player-count screens inside `Game.cpp` | `Source/GUI/*`, `Source/Dash/*`, `Game/Game.cpp` |
| Config | Hard-coded in `Constants::Init` (judgement windows, energy, offsets, paths). There's a TODO to add an `.ini` | `Source/Game/Constants.cpp:30-95` |
| High scores | Plain-text `.records` file per song (per difficulty × 4 record slots) | `Game/Song.cpp:305-350` |

### DDR functionality

| Feature | Dance Clone v0.68 | Source |
|---|---|---|
| Dance-pad detection | GameCube channel presence from the `PAD_ScanPads()` bitmask. The mat is treated as a **standard GC controller** (D-pad = arrows) | `WiiInput.cpp:80, 99` |
| GC ports | Yes, channels 0–3 | `WiiInput.cpp` |
| Simultaneous arrows (jumps) | Yes. Each direction bit is independent. Jumps are rated as a unit (`DoJumpProcessing`, `jumpAllowDelay = 80 ms`) | `Game/Player.cpp`, `Constants.cpp` |
| Note representation | `Arrow{direction, beat → time(ms), type (note quantisation for colouring, or HOLD), rating, animStartTime}` per difficulty. Times come from a 1 ms step-by-step simulation of the scroll code | `Game/Arrow.h`, `Game/Song.cpp:700-740` |
| Timing source | **Wall clock**: `songTime = SDL_GetTicks() - songStartTime`, with `songStartTime = SDL_GetTicks() + 3000`. It is *not* derived from the audio position | `Game/Game.cpp:1818, 1831` |
| Music sync | Hand-tuned constant `internalMp3BufferDelay = 109` ms "experimentally obtained". The v0.68 changelog itself says sync is "not quite perfect" | `Constants.cpp:36-40`, `CHANGELOG` |
| Judgement | ms windows: Marvellous 25, Perfect 50, Great 100, Good 150, Boo 250. The **earliest** unrated arrow in the lane within ±250 ms is taken, not the nearest. That's the v0.60 changelog's known bug, never fixed. When the first foot of a jump lands, the loop can rate the *next* note in that lane instead (`:2661-2672`, `:2747-2801`) | `Constants.cpp:57-62`, `Game.cpp:2590-2630` |
| Scoring/combo/energy | Rating counts, combo, energy meter (+5/+4/+2/+1/−2/−4) and fail, freeze OK/NG. Also a DDR MAX-style "dance points" percentage and AAA–E grade, with wrong weights (Good −4 and Boo 0 instead of Good 0 and Boo −4; the maximum adds misses where it should add holds) | `Game.cpp:1420-1513, 2680-2700` |
| Abort | Hold Button5 + Button7 for 2 s. Because libogc gives `PAD_BUTTON_MENU` and `PAD_BUTTON_START` the same value, holding **Start alone** satisfies this | `WiiInput.cpp:105, 112`, `Game.cpp:1783` |
| Difficulties | Beginner…Challenge (5). The menu chooses per player | `Constants.cpp:28` |
| Multiplayer | Up to 2 players | `Constants.cpp` (`maxPlayers = 2`) |

### Chart support (Dance Clone)

| Feature | Supported? | Notes |
|---|---|---|
| `.sm` | **Partly** | A **line-based, positional** parser: it expects `#NOTES:` followed on the next lines by `dance-single:`, then the difficulty on line+2 and the radar values 4 lines down. Valid `.sm` files with different whitespace or line layout can break it (`Song.cpp:361-480, 525-580`) |
| `.ssc` | No | — |
| `#OFFSET` | **Negative only** | A positive offset makes `beat0Offset` negative, which is stored in `unsigned long currentMs` (`Song.cpp:402`, `:793`). That's undefined, and on GCC/PowerPC most likely wraps so every arrow gets the same time. Needs a test chart to confirm |
| `#BPMS` | **One-line only** | Multiple BPM changes, including non-integral beats (`Song.cpp:1151-1224`). But only the `#BPMS:` line is parsed, and it must end with `,` or `;` (`:499`, `:1177`). StepMania 5 writes one BPM per line, so **every `.sm` saved by StepMania 5 is rejected** |
| Meter | **Crashes on 8** | Digits are matched with `"012345679"` (no `8`, `:461`), so a meter of 8 throws an uncaught `std::out_of_range` when song select opens |
| Measure sizes | Partly | Only 4/8/12/16/24/32/48/64/96/192 rows. Notes in other measure sizes are silently dropped (`:632-696`) |
| `#STOPS` | **No** | No code references STOPS |
| `#DELAYS`, `#WARPS`, etc. | No | — |
| Holds (`2`/`3`) | Yes | `Song.cpp:715-743` |
| Rolls (`4`) | **No, and harmful** | The `4` head is ignored, but its `3` tail attaches to the previous arrow in that lane (`:728-743`), turning an earlier tap into a bogus hold |
| Mines (`M`), lifts, fakes | No | — |
| dance-double | No | "only loading dance-single" (`Song.cpp:1-2`) |
| `#BANNER`, `#TITLE`, radar | Yes | — |
| **Writes to your files** | **Yes** | Radar values default to −1 (`:120`). If a chart's radar line is missing, incomplete or all zeros, the game computes values and **rewrites the whole `.sm`**, putting the radar line at a fixed offset (`:1112`, `:1121-1133`). This runs when song select opens (`Game.cpp:717` → `:1268`). Only ever give it copies |
| v0.5 `.dc` charts | No | v0.68 has no `.dc` loader, so it can't play the bundled v0.5 demo song |

### Wii status (Dance Clone)

| Question | Answer |
|---|---|
| Wii-specific? | Yes (`Makefile_Wii`, `-DWII` paths). Linux is the development target, keyboard only |
| Toolchain | **devkitPPC r24, libogc 1.8.8, SDL-wii 1.2** (built from the old Google Code `sdl-wii` source), libfat 1.0.10, tremor-lowmem, libpng 1.5.4, freetype 2.4.2 (`COMPILING`) |
| Produces a .dol? | Yes. `TARGET := boot` → `boot.dol` (`Makefile_Wii`) |
| Ran on hardware? | **v0.5 did.** It was a public WiiBrew release (2010) and the shipped `boot.dol` is in the archive. v0.68 has no binary. Its changelog includes Wii-specific fixes ("fix gameplay slowdown on Wii", "fix problem with PAD_ScanPads being called too quickly"), which strongly suggests it was run on a Wii, but there is no independent confirmation |
| Dolphin? | Not mentioned anywhere |

---

## 4. Side-by-side comparison

| Area | StepMania-Wii | Dance Clone Wii | Better |
|---|---|---|---|
| Architecture | Full SM 3.9 engine (≈310 files, theme-driven, Lua, threads) + thin Wii drivers | Small purpose-built game, but `Game.cpp` is a 94 KB monolith | **Dance Clone** for size. **SM-Wii** for clean layering |
| Proven on Wii | Booted and drew gameplay; no working audio (alpha, 2012) | v0.5 released and played on Wii. v0.68 likely | **Dance Clone** |
| Controller support | GC + Wiimote OR'd together, 2 channels, pad treated as joystick | GC dance mat explicitly targeted, 4 channels, edge + level states | **Dance Clone** (equivalent API use, more DDR-specific) |
| Simultaneous inputs | Yes (independent bits) | Yes, with jump grouping | Tie / **Dance Clone** |
| Audio | ASND voice streaming with decoders (MP3/OGG/WAV). The shape is right, but it crashed (per the author) and its position query is broken | MP3Player (no position query, buffer freed while playing) + ASND SFX. Wall-clock sync with a 109 ms fudge | **SM-Wii** (shape only), Dance Clone (actually ran) |
| Timing engine | SM `TimingData` (BPM + stops), mature | BPM changes only, ms-based | **SM-Wii** |
| Chart compatibility | Robust `.sm` (MsdFile), DWI/KSF/BMS. No `.ssc` | Fragile `.sm` subset: no stops, rejects StepMania 5 files, crashes on meter 8, rewrites files | **SM-Wii** |
| Scoring | DDR MAX2 scoring, life meters, profiles | Ratings/energy/records, plus a dance-points grade with wrong weights | SM-Wii (richer), Dance Clone (simpler to adapt) |
| Graphics | GX (hardware), incomplete | SDL 1.2 software blits, 16-bit | Neither is ideal. GX/GRRLIB is the way forward |
| Build environment realism | Needs Metaphrasis (not packaged), Lua 5.1 (packaged), `-ldb` (still in libogc), plus SM 3.9 game data. Linked in 2012 | Needs SDL 1.2 Wii libs (still packaged in devkitPro), Boost and Makefile path fixes | **Dance Clone** (closer to buildable) |
| Licence | Project published as GPL v2. Its Wii files have no licence text. The inherited StepMania 3.9 files are MIT | GPLv2+ headers vs an LGPL 2.1 COPYING file (ambiguous, copyleft) | **Upstream StepMania 3.9** (MIT), not SM-Wii |
| Last activity | 2012‑04‑12 | 2013‑01‑16 | Both abandoned |

---

## 5. Build feasibility

**Not yet tested.** devkitPro is not installed on this PC (`C:\devkitPro` doesn't exist, and `DEVKITPRO`/`DEVKITPPC` are unset). Installing it needs your approval (see [03-development-environment.md](03-development-environment.md)). This assessment is **static** (from reading the code), and each prediction needs an experiment to confirm.

### 5.1 Dance Clone v0.68 on a 2026 devkitPPC/libogc — "plausible with moderate patching"

| Issue | Where | Expected effect | Fix effort |
|---|---|---|---|
| SDL 1.2 Wii libs | `Makefile_Wii:38-40` | devkitPro still packages SDL 1.2 for Wii (`pacman-packages/wii/SDL`, `SDL_image`, `SDL_mixer`, `SDL_ttf`, `SDL_gfx`). **There is no `SDL_net`** in that list, but nothing in the source uses SDL_net, so drop `-lSDL_net` | Low |
| Include/library paths | `Makefile_Wii:87-99` | Hard-codes `-I/usr/local/boost_1_49_0`, `-I../SDL_svn/SDL/include/`, `-I$(LIBOGC_INC)/SDL`, and links only from `-L$(LIBOGC_LIB)`. Point them at `$(PORTLIBS)` (defined in `wii_rules`), where the SDL packages install | Low |
| `dirStruct->dirData` | `Platform/Wii/WiiOS.cpp:86` | **Compiles.** devkitPro's newlib still has a public `DIR_ITER* dirData` in `DIR` (checked on the `devkitPro` and `devkitPPC_r47` branches). The check is merely redundant | None |
| `SND_Init(INIT_RATE_48000)` | `Game/Sound.cpp:49` | **Compiles.** Current `asndlib.h` still defines `INIT_RATE_48000` (`:98`) and `SND_Init` as `ASND_Init` (`:156`) | None |
| `MP3Player_*` | `Sound.cpp` | Still in libogc (`gc/mp3player.h`, needs `ppc-libmad`). **Must fix:** keep `mp3Buffer` alive until `StopMusic` (`:169-172` frees it while it plays) | Low |
| **Boost** | `Game/Game.h` includes `<boost/algorithm/string.hpp>` for one `boost::iequals` call in `Game.cpp` (`PreloadSongs`) | Boost isn't in devkitPro's packages. `COMPILING` doesn't mention it, though `Makefile_Wii:92` hard-codes `boost_1_49_0`. Replace with a 5-line case-insensitive compare | Low |
| C++98 code on GCC 13+ | everywhere | Expect warnings and perhaps missing `<cstdlib>`/`<cstring>` includes. (`abs()` at `Game.cpp:1943-1946` takes `long` operands, so it isn't ambiguous) | Low–medium |
| Assets | `Media/`, v0.5 `binary/` | Present in the archive. All 48 `Media/` paths v0.68 loads exist. v0.68 can't use the v0.5 `.dc` demo chart, so a test needs a hand-made `.sm` (see §3) | — |

### 5.2 StepMania-Wii on a modern toolchain — "low feasibility, high effort"

| Issue | Where | Effect |
|---|---|---|
| **Metaphrasis** (a 2008 WiiBrew texture-conversion lib) | `Makefile:65`, `RageDisplay_GX.cpp:27,288` | Not in devkitPro pacman. You would need to find and rebuild the old source, or rewrite the texture upload. **The only truly missing library** |
| **Lua** (`lua_open`, `luaopen_base` called directly, `lua_load`, `lua_pcall`) | `LuaHelpers.cpp:138-146` | **Packaged:** devkitPro ships `ppc-liblua51` (5.1.5). The 5.1 manual wants `luaopen_*` called via `lua_call`, a one-line change |
| `-ldb` (libogc debug stub), `-lwiiuse -lbte` | `Makefile:65` | Still in libogc (`libdb/`) |
| Old libpng internals | `RageSurface_Load_PNG.cpp:43, 69, 79` | Direct `png->io_ptr` / `png->error_ptr` reads are compile errors with libpng ≥ 1.5 (opaque `png_struct`). Use `png_get_io_ptr` / `png_get_error_ptr`. The `png_jmpbuf` fallback at `:24-25` is never used |
| 2005-era C++ (StepMania 3.9 code, `CString`, implicit conversions) | 310 TUs | The port notes already list 12 GCC 4.3 fixes. GCC 13+ will surface many more |
| Unix crash handler (`fork`/`execve`) | `archutils/Wii/*.cpp` | Must be stubbed |
| Game data | `branches/original/StepMania-3.9a-linux.tar.gz` | Must be unpacked to SD and loaded at startup. About 7.4 MB of data (the tarball's other 12.9 MB is the Linux binary). Memory/time on the Wii are untested |
| Even after linking | — | GX driver gaps (the health bar, per the author) and an audio driver that never worked |

**Recommendation on build experiments:** after the toolchain is installed (Milestone 1), it's worth spending **at most half a day** on a build of a **copy** of Dance Clone v0.68 (`build-experiments/danceclone/`, recording every patch). That gives a working reference DDR on modern tools to compare timing and input behaviour against. Patch the MP3 buffer use-after-free, give it **copies** of simfiles only, and test with a hand-made chart: one-line `#BPMS`, negative `#OFFSET`, no meter of 8, only 4/8/16-row measures.

**A StepMania-Wii build experiment isn't worth the time.** Its useful parts are algorithms, which can be read without compiling. Its 2012 alpha (no sound) is still on the Google Code Archive. It could be run in Dolphin out of curiosity, but it's an untrusted binary and isn't needed.

---

## 6. Reusable components

Legend: **Reuse** = copy with attribution. **Port** = rewrite closely following the code. **Reference** = read for ideas and edge cases, write fresh.

| # | Component | Project · location | What it does | Functional? | External deps | Independent? | Verdict |
|---|---|---|---|---|---|---|---|
| 1 | GC pad / dance-mat input | DC `Source/Platform/Wii/WiiInput.cpp:58-126`. SM-Wii `arch/InputHandler/InputHandler_Wii.cpp:27-90` | `PAD_ScanPads` → `PAD_ButtonsDown/Held` → 4 direction bits per channel | DC: yes (shipped). SM-Wii: unknown | libogc | Yes | **Reference.** It's about 20 lines using the libogc API. Write it fresh. Don't copy DC's `Button5` overwrite (`:104-105`): since `PAD_BUTTON_MENU` equals `PAD_BUTTON_START`, Z never registers and Start alone triggers the abort |
| 2 | Mat button-mapping notes | DC `WiiInput.cpp:68-77` | Documents mat → GC bits (arrows = D-pad, A, B, Start, Z) | Arrows, A and B match Dolphin. The +/− labels are the reverse of Dolphin's ("Z = + button, Start = - button"), and DC's author was unsure. Doc 04 §3 has the Dolphin mapping | — | — | **Reference** (+/− needs a hardware check) |
| 3 | Wii video/console init | SM-Wii `RageDisplay_GX.cpp:92-189`. DC `WiiVideo.cpp` (PAL handling) | VIDEO + GX setup, PAL 576→480 fallback | Plausible | libogc | Yes | **Reference.** The current devkitPro templates and GRRLIB do this for you |
| 4 | Reset/Power/HOME lifecycle | DC `WiiOS.cpp:28-66`. SM-Wii `ArchHooks_Wii.cpp:19-31` | Sets a flag in the callback, calls `SYS_ResetSystem` from the main loop | DC pattern is correct (it doesn't act inside the callback) | libogc | Yes | **Reference** (copy DC's pattern) |
| 5 | SD mount + directory scan | DC `WiiOS.cpp:41-118`. SM-Wii `ArchHooks_Wii.cpp:46-62` | `fatInitDefault`, `opendir/readdir/stat` | Yes, but leaks a `DIR` per scan (no `closedir`) and ignores mount failure. The `dirData` check is redundant | libfat | Yes | **Rewrite** (trivial) |
| 6 | ASND streaming voice | SM-Wii `arch/Sound/RageSoundDriver_Wii.cpp` | Double-buffered `ASND_SetVoice`/`ASND_AddVoice` fed by a thread, with a position counter | **Never worked** (the author reports crashes). Position is broken (frame number always 0, counts queued not played). Clips song endings. No IRQ/thread sync. Fill thread at idle priority. Also printf in IRQ, busy loop | libogc ASND | Fairly | **Re-implement the shape only.** No licence text in the file and the project is GPL v2, so don't copy code. Use the asndlib docs: `ASND_TestPointer`/`ASND_TestVoiceBufferReady` before refilling, and `ASND_GetTimerVoice` to cross-check position (doc 06) |
| 7 | MP3 playback | DC `Game/Sound.cpp:128-178` | `MP3Player_PlayBuffer` | Yes, but frees the buffer while it plays (`:169-172`) | libogc + libmad | Yes | **Don't use for gameplay** (no position query, so sync must be faked). Fine for menu previews, if the buffer outlives playback |
| 8 | SFX playback (WAV → ASND voice) | DC `Game/Sound.cpp:240-250`, `Game/WAVSample.cpp` | One-shot voices | Yes | ASND | Yes | **Reference** |
| 9 | `.sm` tokenizer | `MsdFile.cpp` (184 lines). Identical in SM-Wii trunk and upstream StepMania tag `v390release` | Robust `#TAG:a:b:c;` parsing with `//` comments and missing-`;` recovery. **No backslash escapes** | Mature (StepMania) | none | **Yes** | **Reference.** Superseded by porting ITGmania's SM5-era `MsdFile` (with escapes), per doc 02 |
| 10 | `.sm` loader | `NotesLoaderSM.cpp` (549 lines), identical upstream | OFFSET/BPMS/STOPS/NOTES → `TimingData` + `NoteData` | Mature | SM types (`CString`, `Song`, `Steps`) | Needs decoupling | **Port the logic** (from upstream) into our own small structs. Not DC's loader (see §3) |
| 11 | Timing engine | `TimingData.cpp/.h` (370 lines), identical upstream | Beat ↔ seconds with BPM changes and stops | Mature | minimal | **Yes** | **Reference.** Superseded by porting ITGmania's `TimingData`, which already has DELAYS/WARPS (doc 02) |
| 12 | DDR scoring | SM-Wii `trunk/ScoreKeeperMAX2.cpp` | DDR MAX2 score formula | Mature | SM types | Medium | **Reference** (pick a simpler scheme first) |
| 13 | Judgement windows/energy constants | DC `Game/Constants.cpp:30-95` | Tuned values | Played on hardware | — | — | **Reference** (but use StepMania-style windows and judge the *nearest* note, see doc 06. DC judges the earliest) |
| 14 | Jump detection | DC `Game/Player.cpp` (`DoJumpProcessing`) | Groups simultaneous steps | Yes | — | — | **Reference** |
| 15 | Song/records persistence | DC `Game/Song.cpp:305-350` | Text record files | Yes | — | — | **Rewrite** |
| 16 | GUI toolkit | DC `Source/GUI/*` | SDL widget set | Yes | SDL 1.2 | No | **Don't reuse** |
| 17 | Rendering | SM-Wii `RageDisplay_GX.cpp`. DC SDL blits | — | — | — | — | **Don't reuse.** Use GRRLIB/GX |
| 18 | Assets | DC `Media/Game/*.png, *.wav`, `Media/Gui/*.ttf`. DC v0.5 demo song | Arrow sprites, ratings, SFX, seven third-party fonts, demo track | Yes | — | — | **Placeholder only.** Licensing of the art is undocumented (the background is "ripped from google images" per `CHANGELOG` v0.56), and the fonts have no licence files, so **don't ship them** |

---

## 7. Major limitations

**StepMania-Wii**
1. It booted and drew gameplay in 2012, but never had working audio, which a rhythm game can't do without. The 2012 work was two weeks on top of a 2009–10 start.
2. It drags in the whole SM 3.9 engine: Lua, themes, about 7 MB of data, threads and a crash handler. Its memory and CPU fit on the Wii is unknown.
3. Metaphrasis is no longer packaged (Lua 5.1 is).
4. The audio position query doesn't work (frame number always 0, counts queued not played). Even fixed, its granularity would be one 43 ms buffer. Rhythm-game judgement needs ≤ 1–2 ms resolution (with interpolation).
5. Input is sampled once per frame, as in Dance Clone.

**Dance Clone**
1. **Sync model:** wall clock + a 109 ms magic offset, with the MP3 started by busy-wait. That kind of drift and jitter is what makes rhythm games feel "off", and the author says so in the changelog.
2. The `.sm` parser is fragile and incomplete: no STOPS (common in DDR charts), one-line `#BPMS` only (so StepMania 5 files are rejected), a crash on meter 8, probably broken positive offsets, and lost notes in unusual measure sizes. It also **rewrites `.sm` files** that lack radar values.
3. SDL 1.2 16-bit software blitting, which limits effects and performance (the changelog has a "fix gameplay slowdown on Wii" entry).
4. A monolithic `Game.cpp`, hard-coded config, mixed-licence headers.
5. Input is sampled once per frame (~16.7 ms at 60 Hz, 20 ms at PAL 50 Hz), so judgement resolution is limited to the frame.
6. The MP3 buffer is freed while it plays (use-after-free), and holding Start alone aborts a song.

---

## 8. Licensing considerations

| Source | Licence | Implication |
|---|---|---|
| StepMania 3.9 code (`TimingData`, `MsdFile`, `NotesLoaderSM`, …) | MIT-style (Chris Danford, Glenn Maynard et al.) | You may port or copy into any project. **Keep the copyright and permission notice** in the ported files. Take them from upstream `stepmania/stepmania` tag `v390release`; the SM-Wii copies are identical, but upstream keeps the provenance clean. This project ports the SM5-era versions from ITGmania `v1.3.0` instead (doc 02) and uses these as the readable reference |
| ITGmania `v1.3.0` (`MsdFile`, `TimingData`/`TimingSegments`, `NotesLoaderSM`/`SSC`, `InputFilter`, `Group`, …) | **GPLv3** (MIT up to `v0.9.0`) | Compatible with this project's GPL-3.0-or-later. Keep the original notices and mark our changes (doc 02) |
| StepMania-Wii's own Wii files (`RageDisplay_GX.*`, `ArchHooks_Wii.*`, `InputHandler_Wii.cpp`, `RageSoundDriver_Wii.*`, `Threads_Wii.*`, `arch_wii.h`) | **No licence text in the files.** The project was published as **GPL v2** | Treat as GPL v2, possibly v2-only, which can't be combined with our GPLv3. Re-implement ideas; don't copy code |
| `libresample` (bundled in SM-Wii) | LGPL | Not needed |
| Dance Clone | **Ambiguous:** COPYING = LGPL 2.1, file headers = **GPL v2+** | Both allow use in a GPLv3 project, which this project now is (doc 02). It's usable, but its code quality makes it mostly a reference |
| DC art, sounds and fonts | Undocumented (the background is acknowledged as taken from Google Images; the seven TTF fonts are third-party with no licence files) | Don't redistribute |
| DC v0.5 demo song "Dance, NOW!" by RekcahDam | Credited in `meta.xml`. No permission or terms are stated | Use for local testing only |
| StepMania simfiles you download | Varies per pack, and the music is usually copyrighted | Keep them private on your own SD card |

---

## 9. Recommendation

**Replace, borrowing selectively.** Don't continue either codebase as the base of the new game:

- **StepMania-Wii** is the wrong shape (a full desktop engine). It booted without sound in 2012 but its author never got audio working, it needs the unpackaged Metaphrasis, and the project is GPL v2. Its value is the **MIT-licensed StepMania 3.9 algorithms**: `MsdFile`, `NotesLoaderSM` and `TimingData` are worth studying. Doc 02 now ports their SM5-era successors from ITGmania (GPLv3, matching this project), with the 3.9 versions as the readable reference. Its ASND driver shows the right shape (a double-buffered voice fed by a thread), but its position and buffer logic shouldn't be copied.
- **Dance Clone** is the best evidence that **GameCube dance mats work through libogc `PAD_*` as standard controllers** and that a Wii DDR game is practical. It is a useful **behavioural reference** for input handling and jump grouping. For judgement, note that it rates the earliest arrow in the window, not the nearest. Optionally, a patched copy of v0.68 (or the v0.5 `boot.dol` in Dolphin) can be used as an early "does my mat work" check, with the caveats in §5.1. Because of its sync model, chart loader, SDL 1.2 renderer and GPL headers, it isn't a good foundation.

Build a **new lightweight Wii-native engine** (devkitPPC + libogc + GRRLIB, ASND streaming audio with an audio-clock-driven timing engine). See [02-architecture-options.md](02-architecture-options.md) and [06-technical-design.md](06-technical-design.md).
