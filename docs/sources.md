# Sources

*All links checked **2026-11-08** unless noted. Status: 🟢 maintained (activity in 2025–26) · 🟡 stable/slow · 🔴 abandoned · ⚠️ couldn't fetch today (site refused automated access or the page is missing, so verify by hand).*

## Supplied projects

| Source | Status | Notes |
|---|---|---|
| `reference/stepmaniawii.zip` → `reference/extracted/stepmaniawii/stepmania-wii/` | 🔴 last commit 2012‑04‑12 (SVN r11) | Upstream `http://stepmania-wii.googlecode.com/svn` (Google Code closed in 2016, no mirror found) |
| `reference/danceclonewii.7z` → `reference/extracted/danceclonewii/` | 🔴 last commit 2013‑01‑16 | Source = GitHub commit `5137688950…` |
| https://github.com/lefrac01/DanceClone | 🔴 | 41 commits, LGPL‑2.1 file (GPLv2+ headers), 1 open issue |
| https://wiibrew.org/wiki/Dance_Clone | 🔴 (page edited 2014) | v0.1–0.5 (2009–10) by ThatOtherPerson, unofficial v0.68 (2013) |
| https://gamebrew.org/wiki/Dance_Clone_Wii | 🔴 | Mirror/info page |

## Wii homebrew toolchain and libraries

| Source | Status | Used for |
|---|---|---|
| https://devkitpro.org/wiki/Getting_Started | ⚠️ 403 to automated fetch | Official install guide (link this, per devkitPro) |
| https://devkitpro.org/wiki/devkitPro_pacman | ⚠️ 403 | pacman usage |
| https://github.com/devkitPro/installer/releases | 🟢 v3.0.3 "Latest" | Windows installer |
| https://github.com/devkitPro/libogc | 🟢 tag **v3.2.1, 2026‑10‑15** | libogc source |
| https://raw.githubusercontent.com/devkitPro/libogc/master/gc/ogc/pad.h | 🟢 | PAD API, button bits |
| https://raw.githubusercontent.com/devkitPro/libogc/master/libogc/pad.c | 🟢 | ScanPads semantics, SI type gate |
| https://raw.githubusercontent.com/devkitPro/libogc/master/gc/ogc/si.h | 🟢 | SI device types |
| https://raw.githubusercontent.com/devkitPro/libogc/master/gc/asndlib.h | 🟢 | ASND API incl. `ASND_GetTime`, `ASND_GetTimerVoice`, `ASND_GetSampleCounter` |
| https://raw.githubusercontent.com/devkitPro/libogc/master/gc/aesndlib.h | 🟢 | AESND API (no position queries) |
| https://raw.githubusercontent.com/devkitPro/libogc/master/gc/mp3player.h | 🟢 | MP3Player API (no position queries) |
| https://github.com/devkitPro/wii-examples | 🟢 | Templates, GX, audio, device examples |
| https://github.com/devkitPro/pacman-packages (`wii/`, `ppc/`) | 🟢 | Confirms SDL/SDL2/opengx, ppc-libmad, ppc-libvorbisidec, etc. |
| https://github.com/extremscorner/libogc2 (also https://codeberg.org/extremscorner/libogc2) | 🟢 | Fork with extra GC accessory support. Not recommended here |
| https://github.com/GRRLIB/GRRLIB | 🟢 v4.6.1 (MIT) | 2D rendering |
| https://wiibrew.org/wiki/Homebrew_Channel | 🟡 HBC 1.1.3 (2016) | SD layout, icon size |
| https://wiibrew.org/wiki/Wiiload | 🟡 | `WIILOAD=tcp:<ip>` |
| https://wiibrew.org/wiki/Hardware/Broadway | 🟡 | 729 MHz CPU |
| https://wiibrew.org/wiki/Starlet/Main_Memory | 🟡 | MEM1 24 MB / MEM2 64 MB |
| https://wiibrew.org/wiki/Hardware/DSP | 🟡 | DSP notes ("ARAM does not physically exist") |
| https://www.wiimc.org/?p=51 | 🔴 (old third-party build guide) | Confirms `pacman -S wii-dev` on Windows (no sudo) |

## Softmodding (real Wii)

| Source | Status | Notes |
|---|---|---|
| https://wii.hacks.guide/ | 🟢 | Recommended guide |
| https://wii.hacks.guide/get-started | 🟢 | Exploit selection: Wilbrand / str2hax / ModMii. Bluebomb for Wii mini |
| https://wii.hacks.guide/letterbomb | 🟢 (marked legacy) | 4.3 only, FAT32 ≤ 32 GB SD |

## Dolphin

| Source | Status | Notes |
|---|---|---|
| https://dolphin-emu.org/download/ | ⚠️ 403 | Version not confirmed today |
| https://dolphin-emu.org/docs/guides/how-use-official-gc-controller-adapter-wii-u/ | ⚠️ 403 | Wii U GC adapter + Zadig guide |
| https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/SI/SI_Device.h | 🟢 | `SI_DANCEMAT = SI_TYPE_GC | SI_GC_STANDARD | 0x300` |
| https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/SI/SI_DeviceDanceMat.cpp | 🟢 | Mat button mapping |
| https://wiki.dolphin-emu.org (DDR Mario Mix page) | ⚠️ (search summary only) | "must be configured manually" with an adapter |
| https://forums.dolphin-emu.org/Thread-dance-mat-question | 🟡 (forum) | Advice: use Standard Controller for DDR, Dance Mat is for Family Trainer |
| https://bugs.dolphin-emu.org/issues/7356 , /11058 , /13048 | 🟡 | Historical dance-mat/D-pad issues (11058 marked fixed) |

## Rhythm engines and chart formats

| Source | Status | Notes |
|---|---|---|
| https://github.com/stepmania/stepmania | 🟢 commits to 2026‑11‑07 (5_1-new). Latest stable 5.0.12 | MIT. OpenGL/GLEW, FFmpeg, MAD |
| https://github.com/stepmania/stepmania/wiki/Linux-Dependencies | 🟡 (2023) | Dependency list |
| https://github.com/stepmania/stepmania/wiki/sm | 🟡 (2023) | .sm spec |
| https://github.com/stepmania/stepmania/wiki/ssc | 🟡 (2022, "in progress") | .ssc spec |
| https://github.com/itgmania/itgmania | 🟢 v1.3.0 (2026‑07‑01) | GPL‑3.0 SM 5.1 fork |
| https://github.com/etternagame/etterna | 🟢 v0.75.1 (2026‑09‑07) | MIT. OpenGL + experimental Vulkan |
| https://github.com/TeamRizu/OutFox | 🟡 last public build 2025‑09‑07 | Bug tracker only. Engine source not verified open |
| https://projectoutfox.com/ | 🟡 | Supported formats list |
| https://github.com/garcia/simfile | 🟡 sporadic (last 2026‑06‑27) | MIT Python SM/SSC parser + timing. **Test oracle** |
| https://github.com/garcia/msdparser | 🟡 | MSD tokenizer reference |
| https://github.com/topics/simfile | — | No C/C++ parser library found |

## In-workspace source references used most

| File | Topic |
|---|---|
| `reference/extracted/stepmaniawii/stepmania-wii/trunk/TimingData.cpp` | Beat↔time algorithm, offset sign convention |
| `…/trunk/MsdFile.cpp`, `…/trunk/NotesLoaderSM.cpp` | .sm parsing |
| `…/trunk/PrefsManager.cpp:87-92` | StepMania judge windows (22.5/45/90/135/180 ms) |
| `…/trunk/arch/Sound/RageSoundDriver_Wii.cpp` | ASND streaming pattern |
| `…/trunk/arch/InputHandler/InputHandler_Wii.cpp` | PAD/WPAD input |
| `…/trunk/RageDisplay_GX.cpp` | GX init/draw |
| `reference/extracted/danceclonewii/source/DanceClone-master/Source/Platform/Wii/WiiInput.cpp` | Dance-mat input |
| `…/Source/Game/Sound.cpp`, `…/Source/Game/Constants.cpp` | Audio + sync constants |
| `…/Source/Game/Song.cpp`, `…/Source/Game/Game.cpp` | .sm subset parser, judging |
