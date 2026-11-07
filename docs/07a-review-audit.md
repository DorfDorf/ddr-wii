# 07a — Audit of the Existing-Codebase Review (doc 07)

*Audited 2026-11-08. I checked the claims and citations in [07-existing-codebase-review.md](07-existing-codebase-review.md) against the extracted source in `reference/extracted/`. Claims about today's toolchain were checked against upstream sources: devkitPro libogc, newlib and pacman-packages, Dolphin and StepMania, read through the GitHub API, plus the Google Code Archive metadata for `stepmania-wii`. Nothing was compiled or run. Only text files and JSON metadata were downloaded. The archived StepMania-Wii binary was only checked with a HEAD request. Line numbers like "(line 100)" refer to doc 07.*

## Verdict

**Doc 07's bottom line holds.** Don't build on either codebase. Port StepMania 3.9's timing and chart code, and write a new GRRLIB/ASND engine. Most file and line citations are accurate (see §4).

But four gaps matter:

- **It missed the Google Code Archive.** The archive is still online, and `stepmaniawii.zip` came from it. It shows three things doc 07 gets wrong:
  - StepMania-Wii **did build and run**. A no-sound alpha was released on 2012‑03‑30.
  - The project was **published under GPL v2**, not MIT.
  - The author **never got the audio driver stable**.

  Docs 01, 02, 05 and `sources.md` repeat the wrong versions.
- **It undersells Dance Clone v0.68's run-time bugs.** v0.68:
  - rewrites `.sm` files
  - crashes on any chart rated 8
  - rejects every `.sm` saved by StepMania 5
  - frees the MP3 buffer while the song is still playing

  This changes doc 05's optional M1.5 "patched Dance Clone" step.
- **Some predicted build problems don't exist.** Two predicted Dance Clone build breaks (`dirData`, `SND_Init`) and the `abs(long)` ambiguity aren't real. "Lua isn't packaged" is also wrong.
- **It overrates the StepMania-Wii sound driver as a template.** Its position query doesn't work at all. It isn't merely coarse.

---

## 1. Errors that change a conclusion

### 1.1 StepMania-Wii did link, boot and ship an alpha

**Doc 07 says:**
- "there is no evidence it ever linked" (line 100)
- "Ran on hardware/Dolphin? **No evidence.**" (101)
- "Released binary: None… None known" (50)
- "Never shown to build or boot" (248)
- "no live mirror found" (42)

**Evidence:**
- **The project page survives.** The Google Code Archive still hosts it ([`project.json`](https://storage.googleapis.com/google-code-archive/v2/code.google.com/stepmania-wii/project.json)). In the project description, the author (signed ObsidianX) says that:
  - they tested the 2D drawing of the graphics driver and had "everything but the health bar working"
  - ASND and AESND audio "both crash after a few second of audio playback"
  - they had "uploaded a working build that will fake the audio driver's progress"
- **The build is still hosted.** The [downloads list](https://storage.googleapis.com/google-code-archive/v2/code.google.com/stepmania-wii/downloads-page-1.json) has `Stepmania-alpha-SD.zip`: "Alpha - No Sound Driver", 13,239,596 bytes, uploaded 2012‑03‑30, the same morning as r5 and r6. A HEAD request for it returns HTTP 200.
- **The trunk shows signs of linking.** Trunk `#include`s `RageUtil_CircularBuffer.cpp` (out-of-line template definitions) into `GameSoundManager.cpp:66`. That's the usual workaround for template *link* errors. Both files were last changed on 2012‑04‑02 (r8, r9).
- **The archive is where your zip came from.** `reference/stepmaniawii.zip` is 34,661,403 bytes, the exact size of the archive's `source-archive.zip`.

**Correct to:** "Linked and ran without audio. Alpha ('No Sound Driver') released 2012‑03‑30. The author couldn't stabilise ASND/AESND playback. Never finished."

The recommendation doesn't change, because a silent build is no use for a rhythm game. The alpha could be run in Dolphin out of curiosity. It's an untrusted binary, and downloading it needs your OK.

### 1.2 StepMania-Wii is GPL v2, and its Wii files have no licence text

**Doc 07 says:**
- "StepMania-Wii's own Wii files… The headers carry the StepMania MIT text… Treat as MIT" (267)
- Licence row: "MIT-style (reuse-friendly)… Better: SM-Wii" (179)
- "Port the idea (MIT)" (229)

**Evidence:**
- The archive metadata says `"license": "gpl2"`.
- These Wii-specific files carry **no licence or copyright text at all**:
  - `RageDisplay_GX.cpp/.h`
  - `ArchHooks_Wii.cpp/.h`
  - `InputHandler_Wii.cpp`
  - `RageSoundDriver_Wii.cpp/.h`
  - `Threads_Wii.cpp/.h`
  - `arch_wii.h`
- The MIT text appears in 601 of 637 trunk `.cpp/.h` files. Every one of them is a StepMania 3.9 file or a copy of one.
- **Good news for the plan:** the six files to port (`TimingData`, `MsdFile` and `NotesLoaderSM`, each `.cpp` + `.h`) match upstream StepMania's `v390release` tag exactly, ignoring whitespace. That upstream copy carries the MIT notice "(c) 2001-2004 Chris Danford, Glenn Maynard".

**Correct to:**
- Port the six files from `stepmania/stepmania` at tag **`v390release`**, not from the SM-Wii trunk. That keeps their provenance cleanly MIT.
- Treat the Wii-specific code as GPL v2. Re-implement ideas from it; don't copy code.
- In §4, the licence row's winner is "StepMania 3.9 upstream (MIT)", not SM-Wii.

### 1.3 The StepMania-Wii sound driver's position query doesn't work

**Doc 07 says:**
- "a position query. Architecturally the right idea" (173)
- "Has bugs (printf in IRQ, busy loop, 43 ms position granularity)" (229)
- "a useful structural template" (280)

**What it misses.** Line numbers are in `trunk/arch/Sound/RageSoundDriver_Wii.cpp` unless noted.

- **`frameno` is always 0.** The driver passes `0` as the frame number in every `GetPCM()` call (`:46`, `:116`). StepMania uses that argument to map driver time to song position. The comment at `RageSound.cpp:569-579` says "frameno is the audio driver's conception of time… Keep track of frameno->position mappings". So `GetPositionSeconds()` can't be right.
- **`position` counts bytes queued, not bytes played.**
  - It is set to the prebuffer size before anything plays (`:122`).
  - It grows when the next buffer is queued (`:28`).
  - So it runs about one buffer (≈43 ms) ahead, and moves in 43 ms steps.
- **Song endings get clipped.**
  - On a short read, the fill thread calls `StopPlaying()` (`:49`), which stops the voice. Queued audio never plays.
  - `ASND_AddVoice` always queues the full 8 KB (`:33`), even when only part of the buffer was filled.
- **There is no synchronisation.**
  - `bufferReady`, `activeBuffer` and `position` are shared by the IRQ callback and the fill thread, with no lock and no `volatile`.
  - The per-voice mutex is only used in `StartMixing`/`StopMixing`.
- **The fill thread runs at the lowest priority.** `Threads_Wii.cpp:54` creates every `RageThread` at priority 0, which is libogc's `LWP_PRIO_IDLE` (`lwp.h:49`). A busy main thread starves the audio.
- **The author reports crashes.** By the author's own account, the driver crashed after a few seconds. `RageSound::GetPCM` still contains debug `printf`s (`RageSound.cpp:587-589`).

**Correct to:** "Shape only: a double-buffered ASND voice fed by a thread. Its position logic and buffer handling are not a model."

Doc 06 §3.1 already plans the right clock: count consumed frames and cross-check with `ASND_GetTimerVoice`. In doc 06 §4, add `ASND_TestPointer()` / `ASND_TestVoiceBufferReady()` (`asndlib.h:354`, `:363`), so a buffer is never refilled while it is still playing.

### 1.4 Dance Clone v0.68 is risky as a test bed

Doc 07 calls the `.sm` parser "fragile". Doc 05 M1.5 plans a patched v0.68 build whose success test is "plays its demo song". The code is worse than "fragile":

| # | Problem | Evidence | Effect |
|---|---|---|---|
| a | **Rewrites your `.sm` files** | Radar values default to −1 (`Song.cpp:120`). If a chart's radar line is missing or all zeros, `VerifyRadarData()` calls `Prepare()`, which writes the whole file back (`:1121-1133`). The new radar line goes at a fixed offset, `noteLineIndex+4` (`:1112`). This runs when song select opens (`Game.cpp:717` → `:1268`) | Can damage simfiles. Only ever give it copies |
| b | **Crashes on a chart rated 8** | The meter digits are matched with `find_first_of("012345679")`, which leaves out `8` (`Song.cpp:461`). A meter line of `"     8:"` reaches `substr(npos)` → `std::out_of_range`. Nothing catches it | Abort as soon as song select opens |
| c | **Rejects every `.sm` saved by StepMania 5** | `ParseBpms` only receives the `#BPMS:` line (`:499`) and needs a `,` or `;` on that same line (`:1177`). StepMania 5 writes one BPM per line, with the `;` on its own line (`NotesWriterSM.cpp:82-92`, branch `5_1-new`) | It finds no BPMs, so the song isn't listed |
| d | **A positive `#OFFSET` very likely breaks the chart** | `beat0Offset` goes negative (`:402`) and is stored in `unsigned long currentMs` (`:793`). That conversion is undefined, and on GCC/PowerPC it most likely wraps to ~4.29×10⁹. The 1 ms simulation would then stamp every arrow with the same time | Confirm with a test chart |
| e | **Some measures lose their notes** | `switch(ticksPerMeasure)` only handles measures of 4/8/12/16/24/32/48/64/96/192 rows (`:632-696`) | Notes in other measure sizes are silently dropped |
| f | **Rolls corrupt nearby notes** | The `4` head is ignored, but the `3` tail attaches to the previous arrow in that lane (`:728-743`) | An earlier tap becomes a bogus hold |
| g | **v0.68 can't play the bundled demo** | The v0.5 demo chart is `.mp3.dc`, and v0.68 has no `.dc` loader | Doc 05's success test can't pass |
| h | **The MP3 buffer is freed while the song plays** | `StartMusic` frees `mp3Buffer` as soon as playback starts (`Sound.cpp:169-172`). libogc's `MP3Player_PlayBuffer` keeps the caller's pointer and reads from it for the whole song (`libmad/mp3player.c`: `rambuffer.buf_addr = buffer`, `_mp3ramcopy`) | Use-after-free. Doc 07 §6 row 7 says "Functional? Yes" |

**Correct to:** add these to doc 07 §3 "Chart support" and to §6 rows 7 and 10. For doc 05 M1.5:
- Patch (h): free the buffer in `StopMusic` instead.
- Only ever give it copies of simfiles.
- Test with a hand-made chart that has:
  - a one-line `#BPMS`
  - a negative `#OFFSET`
  - no meter of 8
  - only 4-, 8- and 16-row measures

### 1.5 Some of the predicted Dance Clone build fixes aren't needed

| Doc 07 §5.1 says | Actually |
|---|---|
| `dirStruct->dirData` is "very likely a compile error" (194) | **Compiles.** devkitPro's newlib still defines `DIR` as `{ long int position; DIR_ITER* dirData; struct dirent fileData; }`. This is in `newlib/libc/machine/powerpc/sys/dirent.h` on both the `devkitPro` branch (2026‑04‑11) and `devkitPPC_r47`. The check is merely redundant |
| `SND_Init(INIT_RATE_48000)` "may no longer exist" (195) | **Compiles.** `asndlib.h:98` has `#define INIT_RATE_48000`, and `:156` has `#define SND_Init ASND_Init` |
| `abs(long)` ambiguity at `Game.cpp:1943-1946` (198) | **No ambiguity.** Both operands are `long` (`Game.h:114`, `Player.h:46`), so the call binds to `std::abs(long)` |
| Include/library paths are at `Makefile_Wii:20-46` (193) | **The paths that matter are at `:87-99`:** `-I/usr/local/boost_1_49_0`, `-I../SDL_svn/SDL/include/`, `-I$(LIBOGC_INC)/SDL`, and only `-L$(LIBOGC_LIB)` for libraries. Point them at `$(PORTLIBS)` (`wii_rules:7`) |
| Boost is "undocumented" (197) | `COMPILING` doesn't mention it, but `Makefile_Wii:92` hard-codes it |

The patch lists in doc 05 M1.5 and doc 02 (line 55) should drop `dirData` and `SND_Init`, and add the MP3 buffer fix from 1.4(h).

### 1.6 Lua 5.1 is packaged

**Doc 07 says:** "Lua for Wii… Not packaged" (206), and "Its dependencies (Metaphrasis, old Lua-for-Wii) are no longer packaged" (250).

**Evidence:**
- devkitPro ships `ppc-liblua51` 5.1.5 (`pacman-packages/ppc/lua51`, group `ppc-portlibs`).
- StepMania 3.9's Lua use is tiny. `lua_open()`, direct `luaopen_base/math/string` calls (`LuaHelpers.cpp:138-146`), `lua_load` and `lua_pcall` all exist in Lua 5.1. The 5.1 manual wants `luaopen_*` called through `lua_call`, which is a one-line change.
- **Metaphrasis is the only missing library.**

The "don't build SM-Wii" verdict is unchanged.

### 1.7 Dance Clone judges the earliest arrow, not the nearest

**Doc 07 says:** "The nearest unrated arrow in the lane within the window is taken" (134). It calls Dance Clone the best reference for "judgement feel" (281).

**Evidence:**
- `RateArrows` walks the lane from the oldest visible arrow and rates the first unrated one that is no more than 250 ms ahead (`Game.cpp:2595-2605`). Arrows more than 250 ms late were already marked as misses (`:2555-2576`).
- The v0.60 changelog lists exactly this as a known bug ("counts for the earlier arrow instead of the closest one"). It was never fixed.
- **Jumps can misjudge the next note.** When the first foot of a jump lands, the jump gets `RATING_NONE` and the loop doesn't `break` (`:2661-2672`, `:2747-2801`). That press can then judge the *next* note in the same lane.

**Correct to:** "the earliest unrated arrow within ±250 ms (a known bug)". The nearest-note rule planned for the new engine (doc 04 §4, doc 02) is right. Don't copy Dance Clone here.

### 1.8 The mat mapping only half matches Dolphin, and Start alone aborts a song

**Doc 07 says:** Dance Clone's mapping notes match "Dolphin's dance-mat model" (225).

**Evidence:**
- The arrows, A and B match.
- The +/− buttons don't. Dolphin says "Z = + button, Start = - button" (`SI_DeviceDanceMat.cpp`, `MapPadStatus`). Dance Clone's comment says the reverse, and its author was unsure ("both?", `WiiInput.cpp:68-71`).
- **Doc 04 (lines 85-91) already gets this right, so doc 07 contradicts doc 04.**

**The `Button5` overwrite has a bigger effect than doc 07 says.** Doc 07 found the overwrite (`:104-105`). libogc gives `PAD_BUTTON_MENU` and `PAD_BUTTON_START` the same value, 0x1000 (`pad.h:32-33`). So:
- Z never registers as Button5-down.
- Holding **Start alone** satisfies the Button5 + Button7 abort check (`:112`, `Game.cpp:1783`). After `songAbortDelay` (2 s), the song ends and the score screen appears.

---

## 2. Smaller factual errors

1. **SVN history** (lines 43-45, 104). obsidianx was **not the only committer**.
   - **r1:** Google Code's empty structure, on 2009‑05‑21. The repository UUID's v1 timestamp decodes to the same second, 13:40:58 UTC.
   - **r2:** committed by `eric.borbely` on 2010‑04‑14. All 1,116 files in `branches/original` come from it.
   - **r3** (2012‑03‑30): only changed the `branches/original` directory entry itself. It was most likely a move or copy of the 2010 import.
   - **r5:** last touched 646 files. Trunk holds 654 files, not 660.

   So the history isn't "11 commits in two weeks". r1–r2 are from 2009–10, and r3–r11 span two weeks in 2012. The author also wrote that the project "hadn't been touched in two years".
2. **`branches/original` is not unmodified StepMania 3.9** (line 25). It is the 2009–10 port in progress:
   - `_WII` code paths in `config.h`, `global.h`, `RageFileManager.cpp`, `RageThreads.cpp`, `RageUtil.cpp`, `StepMania.cpp` and `arch/*`
   - early `ArchHooks_Wii`, `InputHandler_Wii` and `archutils/Wii` files
   - a Makefile marked "THIS IS A CUSTOM MAKEFILE TO BE MADE FOR THE WII!!"

   For pristine 3.9, use upstream `v390release`.
3. **"~100 MB of data"** (249). The SM 3.9 tarball unpacks to 20.4 MB, and 12.9 MB of that is the Linux `stepmania` binary. The game data is about 7.4 MB: Themes 6.5 MB, NoteSkins 0.6 MB, plus Courses and Data.
4. **libpng** (208). The `png_jmpbuf` fallback at `RageSurface_Load_PNG.cpp:24-25` is never used, because modern libpng defines the macro. The real compile errors are elsewhere. They are the direct struct reads `png->io_ptr` (`:43`) and `png->error_ptr` (`:69`, `:79`). `png_struct` has been opaque since libpng 1.5.
5. **MsdFile "with comments and escapes"** (232; doc 06 line 133 repeats it).
   - SM 3.9's `MsdFile` handles `//` comments and a missing `;`.
   - It has **no backslash escapes**. Nothing in `ReadBuf` checks for a `\` character. The only backslashes in the file are the `\r`, `\n` and `\t` literals.
   - The port has to add escapes for `.ssc`.
6. **Dance Clone's scoring isn't "simple ratings"** (176). It computes a DDR MAX "dance points" percentage and an AAA–E grade (`Game.cpp:1470-1513`). Its weights are wrong, though:
   - Good counts −4 and Boo 0. It should be Good 0 and Boo −4.
   - The maximum adds misses where it should add holds.

   It isn't a reliable scoring reference.
7. **"54 C++ files"** (62). There are 66 (33 `.cpp`, 33 `.h`). This agrees with doc 07's own "65 files except `main.cpp`".
8. **"Per channel (up to 4 players)"** (118) contradicts "Up to 2 players" (137). There are 4 input channels (`WPAD_MAX_WIIMOTES`) but `maxPlayers = 2`.
9. **"The changelog has several slowdown fixes"** (256). It has one: "fix gameplay slowdown on Wii" (v0.67). The v0.68 colour-key entry (line 116) also gives no reason, so "for speed" is a guess.
10. **The demo song was "bundled with permission"** (271). `meta.xml` only credits "Dance, NOW! music by RekcahDam", and states no permission or terms.
11. **Once-per-frame input sampling isn't specific to Dance Clone** (258). SM-Wii also polls once per frame (`InputHandler_Wii::Update` → `UpdateTimer`). On PAL 50 Hz a frame is 20 ms, not 16.7.
12. **Citation drift:**
    - The main loop is `main.cpp:86-123`. `VIDEO_WaitVSync` (`:121`) is outside the cited `28-110`.
    - `LIBS` is at `Makefile:65`.
    - The Metaphrasis include is at `RageDisplay_GX.cpp:27`.
    - `danceMatActive` is set at `WiiInput.cpp:99`.
    - `Arrow.type` is a `NoteType` (note quantisation, or `HOLD`). There is no "TAP" type.

## 3. Problems doc 07 missed (minor)

- **Dance Clone `WiiOS::ReadDirectory`:**
  - It never calls `closedir` (`WiiOS.cpp:84-121`), so every scan leaks one directory handle. The Linux version does close it (`LinuxOS.cpp:99`).
  - It ignores the result of `fatInitDefault()` (`:49`).
- **Dance Clone `WiiInput.cpp:115`:** it overwrites the A-held flag with `!held && down`, which is always false. Nothing reads that flag today, so it's harmless.
- **Dance Clone `WAVSample::Load`:**
  - It compares `fread`'s return value, an item count of 1, with `sizeof(header)`. So the RIFF, `fmt ` and `data` checks never run (`WAVSample.cpp:58-96`).
  - It never closes the file.
  - It assumes the canonical 44-byte header.
- **SM-Wii timer:** `diff_usec()` returns a `u32` (`lwp_watchdog.h:64`), so `GetMicrosecondsSinceStart` (`ArchHooks_Wii.cpp:68-73`) wraps after about 71.6 minutes. Doc 06 line 38 lists the same call for the new engine. For time since start, subtract the 64-bit ticks first and convert afterwards.
- **SM-Wii `Threads_Wii`:**
  - `TryLock` blocks, because it calls `LWP_MutexLock` (`:94-95`).
  - The semaphore only signals when its count goes from 0 to 1 (`:125-126`), so a second waiter can sleep forever.
- **Dance Clone fonts:** `Media/Gui` contains seven third-party TTF fonts with no licence files. Add them to the "don't ship" list in doc 07 §6 row 18 and §8.

## 4. Claims that check out

- **Inventory and history:**
  - The workspace and archive layout are as described.
  - Trunk has 584 entries, and there are 74 `.svn` directories (format 10, r11).
  - The upstream URL and the per-file r5–r11 details are correct.
- **Dance Clone identity and licence:**
  - The zip comment equals GitHub HEAD `5137688…` (2013‑01‑16).
  - The repository has 41 commits and one open issue (2018).
  - Authorship is as stated.
  - `COPYING` is LGPL 2.1. 65 files carry GPLv2+ headers, all "Copyright 2012 Carl Lefrançois", and `main.cpp` has none.
  - The file sizes are right, and Win32 is a stub.
- **StepMania-Wii build and platform layer:**
  - All 310 objects in `OFILES` have a source file.
  - `#elseif WII` is at `StepMania.cpp:695`, and is harmless.
  - `RageDisplay_GX` is created before `MakeArchHooks()`, and `EnableConsole(false)` is at `:841`.
  - The `ArchHooks_Wii` init and its "press HOME" hang are as described.
  - SM-Wii calls `SYS_ResetSystem` inside the callbacks, while Dance Clone defers it to `Pump()`.
- **StepMania-Wii graphics and audio:**
  - `RageDisplay_GX` matches the description: 597 lines, double XFB, direct vertex format, Metaphrasis RGBA8, maximum texture 1024, and stubs.
  - The audio driver has 16 voices with 2 × 8 KB buffers (≈42.7 ms). The IRQ `printf` is at `:31` and the `usleep(0)` loop at `:57`.
- **StepMania-Wii input, crash handler and Makefile:**
  - `InputHandler_Wii` behaves as described.
  - The crash handler uses `execve`/`fork`.
  - The `emulate:` target uses gcube.
  - `-ldb` still exists in libogc (`libdb/`).
- **StepMania 3.9 code to port:**
  - `TimingData` is 281 + 89 lines, with its functions at `:98` and `:159`.
  - `MsdFile` is 184 lines.
  - `NotesLoaderSM` is 549 lines. It handles OFFSET, BPMS and STOPS/FREEZES, with no DELAYS or WARPS.
  - `ROWS_PER_BEAT` is 48.
  - The MIT text is on most files. PCRE is BSD and libresample is LGPL.
- **Dance Clone main loop and input:**
  - The main loop's shape is as described.
  - `WiiOS` init runs `fatInitDefault()` then `sleep(1)`.
  - Reset and power handling is deferred to `Pump()`.
  - `dirData` is at `:86`.
  - The Button5 overwrite is at `:104-105`.
  - `PAD_ScanPads()` returns a `1<<chan` bitmask (`pad.c`), so `danceMatActive` is correct. Edge and level states are kept per channel.
- **Dance Clone video and audio:**
  - Video is 16-bit SDL with `HWSURFACE|DOUBLEBUF`, and PAL is forced to 480 lines.
  - The whole MP3 is read into RAM, then played with `MP3Player_PlayBuffer` and a busy-wait.
  - Sound effects play on a free ASND voice.
- **Dance Clone songs and constants:**
  - Songs live in a flat `Music/` folder as `.mp3`/`.sm`/`.records`.
  - The judgement windows are 25/50/100/150/250 ms.
  - The energy values are as listed, and `jumpAllowDelay` is 80 ms.
  - The 109 ms delay is "experimentally obtained", and the pre-start delay is 3000 ms.
  - `maxPlayers` is 2, and there is a `.ini` TODO.
  - Song time comes from the wall clock (`Game.cpp:1818`, `:1831`).
- **Dance Clone chart loading:** the loader is line-positional and `dance-single` only. It has no STOPS (there's a TODO at `Song.h:3`). Holds use `2`/`3`, and there are no rolls, mines, lifts or fakes.
- **Dance Clone build facts:**
  - The toolchain versions in `COMPILING` match, and the target is `TARGET := boot`.
  - Nothing uses SDL_net. devkitPro packages SDL, SDL_image, SDL_mixer, SDL_ttf and SDL_gfx for Wii, but not SDL_net.
  - Boost and Metaphrasis aren't packaged.
  - MP3Player is still in libogc.
  - All 48 `Media/` paths that v0.68 loads exist.
- **Changelog quotes:** the "ripped from google images" background (v0.56) and the other changelog quotes are accurate.

## 5. Knock-on edits in other docs

| Doc:line | Now says | Change to |
|---|---|---|
| 01:15 | GRRLIB is "packaged" | It is built from source and isn't in devkitPro's pacman-packages. Doc 01:68 and doc 03 already say this |
| 01:29 | Pristine SM 3.9 is in `branches/original` | That's a modified 2009–10 port base. Pristine 3.9 is the upstream `v390release` tag |
| 01:31 | StepMania-Wii: "gone, only your copy", MIT, unfinished | Still on the Google Code Archive. GPL v2. A no-sound alpha was released 2012‑03‑30. Unfinished |
| 01:40 | "stopped before showing a working boot" | It booted and drew gameplay, but never had working audio |
| 02:27 | "never shown to link or boot… old Lua-for-Wii" | An alpha shipped without audio. Lua 5.1 is packaged; only Metaphrasis is missing |
| 02:55 | Patches include `dirData` and `SND_Init` | Drop both. Add the MP3 buffer fix and the Makefile path fixes |
| 02:72, 05:86 | Port from the SM-Wii trunk (MIT) | Port from `stepmania/stepmania@v390release`. The code is identical, and its MIT provenance is clean |
| 05:47 | Use the doc 07 §5.1 patch list | Use the corrected list in §1.5 above. Work on copies of simfiles |
| 05:49 | Success = "plays its demo song" | v0.68 can't read the `.dc` demo. Success = it plays a hand-made `.sm` chart as described in §1.4 |
| 05:102 | Re-implement `RageSoundDriver_Wii.cpp`'s structure "(MIT)" | The file has no licence and the project is GPL v2. Re-implement from the asndlib docs |
| 06:38 | `diff_usec()` for timestamps | Fine for short intervals. For time since start, use 64-bit tick maths, because `diff_usec` returns a `u32` that wraps at ~71.6 min |
| 06:117 | Lists three driver bugs | Also list the frame number of 0, position counted when queued, clipped endings, no IRQ/thread synchronisation and priority 0 |
| 06:133 | The MsdFile port "handles… escapes" | SM 3.9's MsdFile has no escapes. They are new code |
| sources.md:9 | "no mirror found" | Add the Google Code Archive project page, `project.json` and the downloads list. Note that the local zip is the archive's `source-archive.zip` |

## 6. Sources for this audit

- **Local:**
  - `reference/extracted/**`, as cited above
  - every `.svn/entries` file, parsed for revision, date and author
  - `branches/original/StepMania-3.9a-linux.tar.gz`, listed but not extracted
- **Google Code Archive:**
  - `project.json` and `downloads-page-1.json`, both under `storage.googleapis.com/google-code-archive/v2/code.google.com/stepmania-wii/`
  - HEAD requests only for `source-archive.zip` and `Stepmania-alpha-SD.zip`
- **GitHub** (read with `gh api`):
  - `devkitPro/libogc@master`: `gc/ogc/pad.h`, `libogc/pad.c`, `gc/asndlib.h`, `libmad/mp3player.c`, `gc/ogc/lwp.h`, `gc/ogc/lwp_watchdog.h`, `libdb/`
  - `devkitPro/newlib`: branches `devkitPro` and `devkitPPC_r47`
  - `devkitPro/pacman-packages@master`: `wii/`, `ppc/lua51`
  - `devkitPro/devkitppc-rules`: `wii_rules`
  - `dolphin-emu/dolphin@master`: `SI_DeviceDanceMat.cpp`
  - `stepmania/stepmania`: `NotesWriterSM.cpp` on `5_1-new`, plus tag `v390release` for the six files to port
  - `lefrac01/DanceClone`: commits and issues
