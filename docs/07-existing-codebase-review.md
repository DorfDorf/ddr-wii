# 07 — Review of the Existing Codebases (StepMania-Wii and Dance Clone Wii)

*Reviewed 2026-11-08. This review is based on reading the source in this workspace. Nothing was compiled or run, because no Wii toolchain is installed yet (see [Build feasibility](#5-build-feasibility)).*

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
│   ├── branches\original\                    <- unmodified StepMania 3.9 + data tarball
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
| Upstream | `http://stepmania-wii.googlecode.com/svn` (Google Code, shut down in 2016; no live mirror found) | `stepmania-wii/.svn/entries` |
| Author | `obsidianx@gmail.com` (the only committer) | `.svn/entries` across 74 dirs |
| VCS | Subversion working copy at **r11**. There is no git history | `.svn/entries` |
| Commit timeline | r3 2012‑03‑30 (original SM 3.9 imported into `branches/original`) → r5 2012‑03‑30 (bulk trunk import, 660 files) → r6 `RageDisplay_GX.cpp` → r7–r9 2012‑04‑02 (`arch_platform.h`, `RageUtil_CircularBuffer.cpp`, `GameSoundManager.cpp`, `Makefile`) → r10 2012‑04‑04 (wiki) → **r11 2012‑04‑12** (`StepMania.cpp`, `RageSoundDriver_Wii.*`) | per-file `committed-rev`/`committed-date` in `.svn/entries` |
| Earlier notes | Porting notes dated 2009‑04 (Linux build of SM 3.9 first, then GX/Wii) | `branches/original/Stepmania Port Notes` |
| Docs | Only `wiki/Building.wiki` (prerequisites: devkitPPC, Metaphrasis, Lua for Wii; then `make`) | `wiki/Building.wiki` |
| Base engine | **StepMania 3.9** (2005) | `branches/original/NEWS`, `log.txt` |
| Licence | StepMania's MIT-style licence ("Permission is hereby granted…") on most files. Bundled `pcre` (BSD) and `libresample` (LGPL) | `branches/original/Copying.txt`, file headers |
| Released binary | None in the archive. None known | — |

### 1.3 Dance Clone Wii — identity and history

| Item | Finding | Evidence |
|---|---|---|
| Upstream | https://github.com/lefrac01/DanceClone. The zip comment is commit `513768895055b3df7759cd1857f5a567ac50d4f6`, which is the GitHub HEAD ("v0.68…", 2013‑01‑16) | `unzip -z DanceClone-source.zip`, GitHub commit list |
| Authors | v0.1–v0.50 by **ThatOtherPerson** (2009–2010). v0.51–v0.68 by **Carl Lefrançois** (2012–2013) | `README`, `CHANGELOG`, `binary/DanceClone/meta.xml` |
| VCS | No `.git` in the zip. GitHub shows 41 commits, 1 open issue ("Prebuilt binaries request", 2018) | GitHub |
| Released binary | **v0.5** `boot.dol` (dated 2010‑07‑16), with `meta.xml`, `icon.png`, assets, a demo song (`Rekcahdam - Dance, NOW!.mp3`) and a `.mp3.dc` chart in v0.5's own format. **There is no binary of v0.68** | `binary/DanceClone/` |
| Docs | `README`, `COMPILING`, `CHANGELOG`, `README_SM_file_format(_extended)` (copies of the SM spec) | source root |
| Licence | `COPYING` = **LGPL 2.1** and the README says LGPL. **However, all 65 source files except `main.cpp` have a GPL v2-or-later header** ("Copyright 2012 Carl Lefrançois"). Treat the code as **GPLv2+** to be safe | `Source/**` headers |
| Size | 54 C++ files. `Game/Game.cpp` (94 KB) and `Game/Song.cpp` (47 KB) hold most of the logic | file listing |

---

## 2. StepMania-Wii — architecture

**What it is:** a direct port attempt of the whole StepMania 3.9 engine (≈310 translation units, C++03, CString/STL, Lua 5.0-style API). The port **replaces the platform layer** ("arch" drivers) with Wii versions and keeps the rest of StepMania unchanged.

| Subsystem | Implementation | Source |
|---|---|---|
| Entry point | Stock StepMania `main()`. It constructs `RageDisplay_GX` first (which brings up a text console), then `MakeArchHooks()`, loads all managers (SONGMAN, THEME, etc.), then switches the console to GX | `trunk/StepMania.cpp:688-700`, `:841` |
| App lifecycle | `ArchHooks_Wii` calls `WPAD_Init`, `PAD_Init`, reset/power callbacks (`SYS_ResetSystem`) and `fatInitDefault()` + `chdir("sd:/")`. It hangs at "press HOME" if the SD card fails to mount | `trunk/arch/ArchHooks/ArchHooks_Wii.cpp:33-62` |
| Timer | `gettime()`/`diff_usec()` from libogc | `ArchHooks_Wii.cpp:68-73` |
| Graphics | `RageDisplay_GX`: VIDEO/GX init, double XFB, direct-mode vertex format (pos/tex/colour), quads/fans/strips via `GX_Begin`. Textures are converted with **Metaphrasis** (`convertBufferToRGBA8`). Compiled geometry, lighting and sphere mapping are stubs. Max texture 1024 | `trunk/RageDisplay_GX.cpp` (597 lines; init `:92`, GX setup `:136-189`, textures `:285-330`, draw `:452+`) |
| Audio | `RageSoundDriver_Wii`: up to 16 ASND voices, each double-buffered (2 × 8 KB, about 43 ms). A worker thread (`SoundBufferThread`) fills buffers from StepMania's `GetPCM`. The ASND callback swaps buffers. The decoders come from StepMania (`RageSoundReader_MP3` via **libmad**, Vorbis via **tremor/vorbisidec**, WAV) and are resampled with bundled libresample | `trunk/arch/Sound/RageSoundDriver_Wii.cpp` (`:36` thread, `:125` `ASND_SetVoice`, `:145` `GetPosition`) |
| Input | `InputHandler_Wii`: for channels 0–1 it ORs Wiimote (`WPAD_*`) and GameCube (`PAD_ButtonsDown|PAD_ButtonsHeld`) bits into StepMania joystick buttons JOY_UP/DOWN/LEFT/RIGHT, Start, Select(Z), A, B. HOME calls `exit(0)` | `trunk/arch/InputHandler/InputHandler_Wii.cpp:27-90` |
| Threads | `Threads_Wii` uses LWP (`LWP_CreateThread`, `LWP_JoinThread`, …) | `trunk/arch/Threads/Threads_Wii.cpp` |
| File system | StepMania's RageFile over newlib/libfat (`sd:/`) | `RageFileDriverDirect*.cpp` |
| Menus/UI | All of StepMania 3.9's Screen* classes, driven by theme metrics. **This needs the full SM 3.9 data set** (Themes, NoteSkins, Characters, Announcers…), which lives only in `branches/original/StepMania-3.9a-linux.tar.gz` | `trunk/Screen*.cpp`, `branches/original/log.txt` ("'Characters/default' is missing") |
| Config | StepMania `PrefsManager`/INI | `PrefsManager.cpp` |
| Crash handling | `archutils/Wii/*` is a copy of the Unix crash handler (`fork`, `execve`). It is partly commented out and makes no sense on the Wii | `trunk/archutils/Wii/CrashHandler.cpp:82-317` |

### DDR functionality (inherited from StepMania 3.9)

These parts are full, mature StepMania implementations. They are excellent **reference code**:

- **Notes:** `NoteData` (tracks × rows, 48 rows per beat in SM 3.9), `NoteTypes`, holds.
- **Timing:** `TimingData` with BPM segments and stop/freeze segments. `GetBeatAndBPSFromElapsedTime()` (`TimingData.cpp:98`) and `GetElapsedTimeFromBeat()` (`:159`) are the core conversion functions (281 lines in total).
- **Charts:** `MsdFile.cpp` (the generic `#TAG:value;` tokenizer, 184 lines) and `NotesLoaderSM.cpp` (549 lines: `#OFFSET`, `#BPMS`, `#STOPS`/`#FREEZES`, `#NOTES`, all difficulties). DWI, KSF and BMS loaders are present too. **There is no .ssc support** (.ssc arrived with StepMania 5) and no DELAYS/WARPS.
- **Scoring:** `ScoreKeeperMAX2` (DDR MAX2 scoring), `Combo`, life meters, high scores via `Profile`/`HighScore`.
- **Difficulties:** all five, plus edits and courses.

### Wii status: how far did the port get?

| Question | Answer |
|---|---|
| Wii-specific? | Yes. `wii_rules`, `-DWII`, `arch_wii.h` |
| Toolchain | devkitPPC and libogc as of around 2012. The exact versions aren't recorded |
| Produces a .dol? | The Makefile is written to (`$(OUTPUT).dol: $(OUTPUT).elf`), but **there is no evidence it ever linked**. No `.dol`, `.elf` or `output/` folder was committed |
| Ran on hardware/Dolphin? | **No evidence.** The only run log (`branches/original/log.txt`) is from the 2009 Linux build and fails on missing data. Its `emulate:` target points at **gcube**, a long-dead emulator, not Dolphin |
| Completeness | All 310 objects in `OFILES` have matching sources, so the tree is complete. But the Wii layer is early-stage: GX is half-implemented, there are `printf`s inside the audio IRQ callback (`RageSoundDriver_Wii.cpp:31`), a busy-looping fill thread (`usleep(0)`), `#elseif WII` (an invalid directive, harmless only because it sits in a skipped block, `StepMania.cpp:695`), and the crash handler uses `fork` |

**Bottom line:** StepMania-Wii is an **abandoned, unverified, early-stage port** of a 2005 engine. It was last touched 2012‑04‑12 after 11 commits in two weeks.

---

## 3. Dance Clone Wii — architecture

**What it is:** a small, purpose-built DDR clone (C++98, SDL 1.2). It has a platform abstraction for Wii, Linux and Win32 (Win32 is only a stub in v0.68).

| Subsystem | Implementation | Source |
|---|---|---|
| Entry point / main loop | `main()` builds the platform `OS` object, `Constants`, `GUI`, `Dash` (menu) and `Game`. The loop runs `sys.Pump()` → GUI/dash update → `game.Run()` → drains SDL events → `VIDEO_WaitVSync()` (Wii). So it runs **one game update per video frame** | `Source/main.cpp:28-110` |
| App lifecycle | `WiiOS::Init`: `fatInitDefault()`, `sleep(1)`, `WPAD_Init` + IR, reset/power callbacks that call `SYS_ResetSystem` from `Pump()` | `Source/Platform/Wii/WiiOS.cpp:41-66` |
| Graphics | **SDL 1.2 for Wii** software surfaces, 16-bit (`SDL_SetVideoMode(w,h,16,SDL_HWSURFACE|SDL_DOUBLEBUF)`), `SDL_BlitSurface` + `SDL_Flip`. PAL is forced to 480 lines. Images are loaded with SDL_image. Changelog v0.68 says it moved from alpha to colour-key for speed | `Source/Platform/Wii/WiiVideo.cpp:31-70`, `Source/Platform/Video.cpp` |
| Audio | **Music:** the whole MP3 is read into RAM, then played with **libogc `MP3Player_PlayBuffer`** (libmad), busy-waiting until `MP3Player_IsPlaying()`. **SFX:** WAVs loaded by its own `WAVSample` and played on a free **ASND** voice | `Source/Game/Sound.cpp:47-52, 128-178, 240-250` |
| Input | `WiiInput::Update`: calls `WPAD_ScanPads` and `PAD_ScanPads` once per frame. The return bitmask of `PAD_ScanPads` sets `danceMatActive`. It maps `PAD_BUTTON_UP/DOWN/LEFT/RIGHT` from `PAD_ButtonsDown` (edge) and `PAD_ButtonsHeld` (level) into `directionDown[4]`/`directionHeld[4]` **per channel** (up to 4 players), plus A/B/Start/Z and Wiimote IR for the cursor | `Source/Platform/Wii/WiiInput.cpp:58-126` |
| File system | POSIX `opendir/readdir/stat` on the default libfat device. Songs are discovered as `*.mp3` files directly in `Music/` (a flat folder, not StepMania's `Songs/<pack>/<song>/` layout). Each song is `Name.mp3` + `Name.sm` + `Name.records` side by side | `WiiOS.cpp:68-118`, `Game/Song.cpp:183-232`, `Game/Game.cpp:1248-1275`, `Constants.cpp` (`musicFileRoot = "Music/"`) |
| Menus/UI | Its own retained-mode GUI ("Gooey": Container/Button/Label/Image/FontBitmap, TTF via SDL_ttf), a "Dash" menu, and song wheel/difficulty/player-count screens inside `Game.cpp` | `Source/GUI/*`, `Source/Dash/*`, `Game/Game.cpp` |
| Config | Hard-coded in `Constants::Init` (judgement windows, energy, offsets, paths). There's a TODO to add an `.ini` | `Source/Game/Constants.cpp:30-95` |
| High scores | Plain-text `.records` file per song (per difficulty × 4 record slots) | `Game/Song.cpp:305-350` |

### DDR functionality

| Feature | Dance Clone v0.68 | Source |
|---|---|---|
| Dance-pad detection | GameCube channel presence from the `PAD_ScanPads()` bitmask. The mat is treated as a **standard GC controller** (D-pad = arrows) | `WiiInput.cpp:80, 98` |
| GC ports | Yes, channels 0–3 | `WiiInput.cpp` |
| Simultaneous arrows (jumps) | Yes. Each direction bit is independent. Jumps are rated as a unit (`DoJumpProcessing`, `jumpAllowDelay = 80 ms`) | `Game/Player.cpp`, `Constants.cpp` |
| Note representation | `Arrow{direction, beat → time(ms), type TAP/HOLD, rating, animStartTime}` per difficulty | `Game/Arrow.h`, `Game/Song.cpp:700-740` |
| Timing source | **Wall clock**: `songTime = SDL_GetTicks() - songStartTime`, with `songStartTime = SDL_GetTicks() + 3000`. It is *not* derived from the audio position | `Game/Game.cpp:1818, 1831` |
| Music sync | Hand-tuned constant `internalMp3BufferDelay = 109` ms "experimentally obtained". The v0.68 changelog itself says sync is "not quite perfect" | `Constants.cpp:36-40`, `CHANGELOG` |
| Judgement | ms windows: Marvellous 25, Perfect 50, Great 100, Good 150, Boo 250. The nearest unrated arrow in the lane within the window is taken | `Constants.cpp:57-62`, `Game.cpp:2590-2630` |
| Scoring/combo/energy | Rating counts, combo, energy meter (+5/+4/+2/+1/−2/−4) and fail, freeze OK/NG | `Game.cpp:1420-1450, 2680-2700` |
| Difficulties | Beginner…Challenge (5). The menu chooses per player | `Constants.cpp:28` |
| Multiplayer | Up to 2 players | `Constants.cpp` (`maxPlayers = 2`) |

### Chart support (Dance Clone)

| Feature | Supported? | Notes |
|---|---|---|
| `.sm` | **Partly** | A **line-based, positional** parser: it expects `#NOTES:` followed on the next lines by `dance-single:`, then the difficulty on line+2 and the radar values 4 lines down. Valid `.sm` files with different whitespace or line layout can break it (`Song.cpp:361-480, 525-580`) |
| `.ssc` | No | — |
| `#OFFSET`, `#BPMS` | Yes | Multiple BPM changes, including non-integral beats (`Song.cpp:1151-1245`) |
| `#STOPS` | **No** | No code references STOPS |
| `#DELAYS`, `#WARPS`, etc. | No | — |
| Holds (`2`/`3`) | Yes | `Song.cpp:715-735` |
| Rolls (`4`), mines (`M`), lifts, fakes | No | — |
| dance-double | No | "only loading dance-single" (`Song.cpp:1-2`) |
| `#BANNER`, `#TITLE`, radar | Yes | — |

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
| Proven on Wii | No evidence | v0.5 released and played on Wii. v0.68 likely | **Dance Clone** |
| Controller support | GC + Wiimote OR'd together, 2 channels, pad treated as joystick | GC dance mat explicitly targeted, 4 channels, edge + level states | **Dance Clone** (equivalent API use, more DDR-specific) |
| Simultaneous inputs | Yes (independent bits) | Yes, with jump grouping | Tie / **Dance Clone** |
| Audio | ASND voice streaming with decoders (MP3/OGG/WAV) and a position query. Architecturally the right idea, but immature | MP3Player (no position query) + ASND SFX. Wall-clock sync with a 109 ms fudge | **SM-Wii** (design), Dance Clone (actually ran) |
| Timing engine | SM `TimingData` (BPM + stops), mature | BPM changes only, ms-based | **SM-Wii** |
| Chart compatibility | Robust `.sm` (MsdFile), DWI/KSF/BMS. No `.ssc` | Fragile `.sm` subset, no stops | **SM-Wii** |
| Scoring | DDR MAX2 scoring, life meters, profiles | Simple ratings/energy/records | SM-Wii (richer), Dance Clone (simpler to adapt) |
| Graphics | GX (hardware), incomplete | SDL 1.2 software blits, 16-bit | Neither is ideal. GX/GRRLIB is the way forward |
| Build environment realism | Needs Metaphrasis, an old Lua 5.0/5.1 port, `-ldb`, plus SM 3.9 game data. Never shown to link | Needs SDL 1.2 Wii libs (a `wii-sdl` package still exists in devkitPro) and old libfat internals | **Dance Clone** (closer to buildable) |
| Licence | MIT-style (reuse-friendly) | GPLv2+ headers vs an LGPL 2.1 COPYING file (ambiguous, copyleft) | **SM-Wii** |
| Last activity | 2012‑04‑12 | 2013‑01‑16 | Both abandoned |

---

## 5. Build feasibility

**Not yet tested.** devkitPro is not installed on this PC (`C:\devkitPro` doesn't exist, and `DEVKITPRO`/`DEVKITPPC` are unset). Installing it needs your approval (see [03-development-environment.md](03-development-environment.md)). This assessment is **static** (from reading the code), and each prediction needs an experiment to confirm.

### 5.1 Dance Clone v0.68 on a 2026 devkitPPC/libogc — "plausible with moderate patching"

| Issue | Where | Expected effect | Fix effort |
|---|---|---|---|
| SDL 1.2 Wii libs | `Makefile_Wii:38-40` | devkitPro still packages SDL 1.2 for Wii (`pacman-packages/wii/SDL`, `SDL_image`, `SDL_mixer`, `SDL_ttf`, `SDL_gfx`). **There is no `SDL_net`** in that list, but nothing in the source uses SDL_net, so drop `-lSDL_net` | Low |
| Include/library paths | `Makefile_Wii:20-46` (`INCLUDES`/`LIBDIRS` empty) | The modern template adds `$(PORTLIBS)` and SDL include dirs. Expect header-not-found errors until these are added | Low |
| `dirStruct->dirData` | `Platform/Wii/WiiOS.cpp:86` | Reaches into old libfat/newlib `DIR` internals. **Very likely a compile error** with modern newlib. Delete the check | Low |
| `SND_Init(INIT_RATE_48000)` | `Game/Sound.cpp:49` | An old ASND compatibility alias. It may no longer exist in the current `asndlib.h` (only `ASND_Init(void)` was confirmed). Replace with `ASND_Init()` | Low |
| `MP3Player_*` | `Sound.cpp` | Still in libogc (`gc/mp3player.h`, needs `ppc-libmad`) | Low |
| **Boost** (undocumented dependency) | `Game/Game.h` includes `<boost/algorithm/string.hpp>` for one `boost::iequals` call in `Game.cpp` (`PreloadSongs`) | Boost isn't in devkitPro's packages and `COMPILING` doesn't mention it. Replace with a 5-line case-insensitive compare | Low |
| C++98 code on GCC 13+ | everywhere | Expect warnings and some errors (missing `<cstdlib>`/`<cstring>` includes, `abs(long)` ambiguity at `Game.cpp:1943-1946`) | Low–medium |
| Assets | `Media/`, v0.5 `binary/` | Present in the archive | — |

### 5.2 StepMania-Wii on a modern toolchain — "low feasibility, high effort"

| Issue | Where | Effect |
|---|---|---|
| **Metaphrasis** (a 2008 WiiBrew texture-conversion lib) | `Makefile:64`, `RageDisplay_GX.cpp:26,288` | Not in devkitPro pacman. You would need to find and rebuild the old source, or rewrite the texture upload |
| **Lua for Wii** (Lua 5.0/5.1 API: `lua_open`, `luaopen_base` called directly) | `LuaHelpers.cpp:138-146` | Not packaged. You'd need to port Lua 5.1 or patch for 5.4 |
| `-ldb` (old libogc debug stub), `-lwiiuse -lbte` | `Makefile:64` | Probably still in libogc, but check |
| Old libpng internals (`png->jmpbuf` fallback) | `RageSurface_Load_PNG.cpp:24-25` | Breaks if the macro path is hit with modern libpng |
| 2005-era C++ (StepMania 3.9 code, `CString`, implicit conversions) | 310 TUs | The port notes already list 12 GCC 4.3 fixes. GCC 13+ will surface many more |
| Unix crash handler (`fork`/`execve`) | `archutils/Wii/*.cpp` | Must be stubbed |
| Game data | `branches/original/StepMania-3.9a-linux.tar.gz` | Must be unpacked to SD and loaded at startup. Memory/time on the Wii are untested |
| Even after linking | — | GX driver gaps, an unproven audio driver, no evidence of a working boot |

**Recommendation on build experiments:** after the toolchain is installed (Milestone 1), it's worth spending **at most half a day** on a build of a **copy** of Dance Clone v0.68 (`build-experiments/danceclone/`, recording every patch). That gives a working reference DDR on modern tools to compare timing and input behaviour against. **A StepMania-Wii build experiment isn't worth the time.** Its useful parts are algorithms, which can be read without compiling.

---

## 6. Reusable components

Legend: **Reuse** = copy with attribution. **Port** = rewrite closely following the code. **Reference** = read for ideas and edge cases, write fresh.

| # | Component | Project · location | What it does | Functional? | External deps | Independent? | Verdict |
|---|---|---|---|---|---|---|---|
| 1 | GC pad / dance-mat input | DC `Source/Platform/Wii/WiiInput.cpp:58-126`. SM-Wii `arch/InputHandler/InputHandler_Wii.cpp:27-90` | `PAD_ScanPads` → `PAD_ButtonsDown/Held` → 4 direction bits per channel | DC: yes (shipped). SM-Wii: unknown | libogc | Yes | **Reference.** It's about 20 lines using the libogc API. Write it fresh (and fix DC's bug where `Button5` is overwritten at `:104-105`) |
| 2 | Mat button-mapping notes | DC `WiiInput.cpp:68-77` | Documents mat → GC bits (arrows = D-pad, A, B, Start, Z/Select) | Matches Dolphin's dance-mat model | — | — | **Reference** |
| 3 | Wii video/console init | SM-Wii `RageDisplay_GX.cpp:92-189`. DC `WiiVideo.cpp` (PAL handling) | VIDEO + GX setup, PAL 576→480 fallback | Plausible | libogc | Yes | **Reference.** The current devkitPro templates and GRRLIB do this for you |
| 4 | Reset/Power/HOME lifecycle | DC `WiiOS.cpp:28-66`. SM-Wii `ArchHooks_Wii.cpp:19-31` | Sets a flag in the callback, calls `SYS_ResetSystem` from the main loop | DC pattern is correct (it doesn't act inside the callback) | libogc | Yes | **Reference** (copy DC's pattern) |
| 5 | SD mount + directory scan | DC `WiiOS.cpp:41-118`. SM-Wii `ArchHooks_Wii.cpp:46-62` | `fatInitDefault`, `opendir/readdir/stat` | Yes, but uses the `dirData` hack | libfat | Yes | **Rewrite** (trivial) |
| 6 | ASND streaming voice | SM-Wii `arch/Sound/RageSoundDriver_Wii.cpp` | Double-buffered `ASND_SetVoice`/`ASND_AddVoice` fed by a thread, with a position counter | Unproven. Has bugs (printf in IRQ, busy loop, 43 ms position granularity) | libogc ASND | Fairly | **Port the idea** (MIT). Our audio module copies this structure, with a lock-free ring buffer and sample-accurate position |
| 7 | MP3 playback | DC `Game/Sound.cpp:128-178` | `MP3Player_PlayBuffer` | Yes | libogc + libmad | Yes | **Don't use for gameplay** (no position query, so sync must be faked). Fine for menu previews |
| 8 | SFX playback (WAV → ASND voice) | DC `Game/Sound.cpp:240-250`, `Game/WAVSample.cpp` | One-shot voices | Yes | ASND | Yes | **Reference** |
| 9 | `.sm` tokenizer | SM-Wii `trunk/MsdFile.cpp` (184 lines) | Robust `#TAG:a:b:c;` parsing with comments and escapes | Mature (StepMania) | none | **Yes** | **Port** (MIT, keep the copyright notice). It is also the basis for `.ssc` |
| 10 | `.sm` loader | SM-Wii `trunk/NotesLoaderSM.cpp` (549 lines) | OFFSET/BPMS/STOPS/NOTES → `TimingData` + `NoteData` | Mature | SM types (`CString`, `Song`, `Steps`) | Needs decoupling | **Port the logic** into our own small structs |
| 11 | Timing engine | SM-Wii `trunk/TimingData.cpp/.h` (370 lines) | Beat ↔ seconds with BPM changes and stops | Mature | minimal | **Yes** | **Port** (MIT). Extend with DELAYS/WARPS from the SSC spec |
| 12 | DDR scoring | SM-Wii `trunk/ScoreKeeperMAX2.cpp` | DDR MAX2 score formula | Mature | SM types | Medium | **Reference** (pick a simpler scheme first) |
| 13 | Judgement windows/energy constants | DC `Game/Constants.cpp:30-95` | Tuned values | Played on hardware | — | — | **Reference** (but use StepMania-style windows, see doc 06) |
| 14 | Jump detection | DC `Game/Player.cpp` (`DoJumpProcessing`) | Groups simultaneous steps | Yes | — | — | **Reference** |
| 15 | Song/records persistence | DC `Game/Song.cpp:305-350` | Text record files | Yes | — | — | **Rewrite** |
| 16 | GUI toolkit | DC `Source/GUI/*` | SDL widget set | Yes | SDL 1.2 | No | **Don't reuse** |
| 17 | Rendering | SM-Wii `RageDisplay_GX.cpp`. DC SDL blits | — | — | — | — | **Don't reuse.** Use GRRLIB/GX |
| 18 | Assets | DC `Media/Game/*.png, *.wav`. DC v0.5 demo song | Arrow sprites, ratings, SFX, CC demo track | Yes | — | — | **Placeholder only.** Licensing of the art is undocumented (the background is "ripped from google images" per `CHANGELOG` v0.56), so **don't ship it** |

---

## 7. Major limitations

**StepMania-Wii**
1. Never shown to build or boot. It's 2 weeks of work from 2012 on top of 2009 notes.
2. It drags in the whole SM 3.9 engine: Lua, themes, ~100 MB of data, threads and a crash handler. Its memory and CPU fit on the Wii is unknown.
3. Its dependencies (Metaphrasis, old Lua-for-Wii) are no longer packaged.
4. Audio position granularity is about 43 ms. Rhythm-game judgement needs ≤ 1–2 ms resolution (with interpolation).

**Dance Clone**
1. **Sync model:** wall clock + a 109 ms magic offset, with the MP3 started by busy-wait. That kind of drift and jitter is what makes rhythm games feel "off", and the author says so in the changelog.
2. The `.sm` parser is fragile and incomplete: no STOPS, which are common in DDR charts.
3. SDL 1.2 16-bit software blitting, which limits effects and performance (the changelog has several slowdown fixes).
4. A monolithic `Game.cpp`, hard-coded config, mixed-licence headers.
5. Input is sampled once per frame (~16.7 ms at 60 Hz), so judgement resolution is limited to the frame.

---

## 8. Licensing considerations

| Source | Licence | Implication |
|---|---|---|
| StepMania 3.9 code inside StepMania-Wii (`TimingData`, `MsdFile`, `NotesLoaderSM`, …) | MIT-style (Chris Danford, Glenn Maynard et al.) | You may port or copy into any project. **Keep the copyright and permission notice** in the ported files |
| StepMania-Wii's own Wii files (`*_Wii.*`, `RageDisplay_GX.*`) | No separate licence. The headers carry the StepMania MIT text | Treat as MIT, keep the notices |
| `libresample` (bundled in SM-Wii) | LGPL | Not needed |
| Dance Clone | **Ambiguous:** COPYING = LGPL 2.1, file headers = **GPL v2+** | If you copy any DC code, the project effectively becomes GPLv2+. **Recommendation:** use DC as reference only, so you keep your licence choice free |
| DC art and sounds | Undocumented (the background is acknowledged as taken from Google Images) | Don't redistribute |
| DC v0.5 demo song "Dance, NOW!" by RekcahDam | Bundled with permission in the original release. Terms are not stated | Use for local testing only |
| StepMania simfiles you download | Varies per pack, and the music is usually copyrighted | Keep them private on your own SD card |

---

## 9. Recommendation

**Replace, borrowing selectively.** Don't continue either codebase as the base of the new game:

- **StepMania-Wii** is the wrong shape (a full desktop engine), has an unproven build, and its dependencies are gone. Its value is the **MIT-licensed StepMania 3.9 algorithms**: `MsdFile`, `NotesLoaderSM` and `TimingData` are worth porting almost line-for-line into small, platform-independent C++. Its ASND streaming driver is a useful structural template.
- **Dance Clone** is the best evidence that **GameCube dance mats work through libogc `PAD_*` as standard controllers** and that a Wii DDR game is practical. It is the **best behavioural reference** for input handling, jump handling and judgement feel. Optionally, a patched copy of v0.68 (or the v0.5 `boot.dol` in Dolphin) can be used as an early "does my mat work" check. Because of its sync model, SDL 1.2 renderer and GPL headers, it isn't a good foundation.

Build a **new lightweight Wii-native engine** (devkitPPC + libogc + GRRLIB, ASND streaming audio with an audio-clock-driven timing engine). See [02-architecture-options.md](02-architecture-options.md) and [06-technical-design.md](06-technical-design.md).
