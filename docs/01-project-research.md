# 01 — Project Research: Ecosystem and Technical Findings

*Checked 2026-11-08. Each claim is marked with its evidence level:*
- **[src]** read in source code (in this workspace or on GitHub)
- **[doc]** read in official documentation
- **[mem]** from prior knowledge that I could not re-fetch today. Verify these before relying on them.
- **[exp]** must be confirmed by experiment

---

## 1. Summary of findings

1. **Both supplied projects are abandoned (2012 and 2013), and neither is a good foundation.** Each has valuable parts. The details are in [07-existing-codebase-review.md](07-existing-codebase-review.md).
2. **No modern StepMania-family engine can realistically be ported to the Wii.** All of them depend on desktop OpenGL (or Vulkan), FFmpeg, Lua and hundreds of MB of RAM. The Wii has 88 MB in total and a fixed-function GPU.
3. **The Wii homebrew toolchain is alive and well.** devkitPro's libogc **v3.2.1 was tagged on 2026‑10‑15**, and the installer v3.0.3 is current. GRRLIB (an MIT-licensed 2D library) and SDL/SDL2 for Wii are packaged.
4. **GameCube dance mats appear to libogc as standard GameCube controllers**, with the arrows on D-pad bits. libogc doesn't filter simultaneous opposite directions, so jumps work. **[src]** (libogc `pad.c`, Dolphin `SI_Device.h`/`SI_DeviceDanceMat.cpp`, Dance Clone `WiiInput.cpp`). This needs a final **[exp]** check with your specific pad.
5. **A small new engine is the practical route.** It would port StepMania's MIT-licensed timing and chart algorithms and use Dance Clone as a behavioural reference.

---

## 2. Existing rhythm-game engines

| Engine | Repo | Language / build | Graphics | Audio/video deps | Charts | Licence | Activity (as of 2026‑11‑08) | Wii-feasible? |
|---|---|---|---|---|---|---|---|---|
| **StepMania 5.x** | [stepmania/stepmania](https://github.com/stepmania/stepmania) | C++, CMake (+ autotools remnants), Lua themes | OpenGL + GLU + GLEW **[doc]** | libogg/libvorbis, libMAD, **FFmpeg**, ALSA/Pulse/JACK **[doc]** | .sm, .ssc, .dwi, others | MIT (MAD/FFmpeg GPL when built in) | **Active.** Commits on `5_1-new` up to 2026‑11‑07. Latest stable 5.0.12, 5.1 still beta | **No.** GL 2+/GLEW, FFmpeg, memory |
| **ITGmania** | [itgmania/itgmania](https://github.com/itgmania/itgmania) | C++, CMake ≥ 3.20 | OpenGL | FFmpeg built in, WASAPI/ALSA/Pulse | inherits SM 5.1 | **GPL‑3.0** | Active. v1.3.0 2026‑07‑01 | No |
| **Etterna** | [etternagame/etterna](https://github.com/etternagame/etterna) | C++, CMake ≥ 3.14 | OpenGL, experimental **Vulkan** | MAD, FFmpeg, OpenSSL, SQLite, Crashpad | .sm, plus others | MIT | Active. v0.75.1 2026‑09‑07 | No |
| **Project OutFox** | [TeamRizu/OutFox](https://github.com/TeamRizu/OutFox) (bug tracker only) | fork of SM 5.1 | GL (GLAD) | rtAudio/PortAudio, FFmpeg | many (.sm, .ssc, .dwi, .ksf, .bms, .osu, .tja, …) | **Engine source not verified as open.** The GitHub repo holds no engine code | Last public build OF5.0.0‑043 (2025‑09‑07) | No (and no source) |
| **StepMania 3.9** (2005) | inside your `stepmaniawii.zip` (`branches/original`) | C++03, autotools | OpenGL 1.x / D3D | MAD, Vorbis, SDL | .sm, .dwi, .ksf, .bms | MIT | Dead | Attempted by StepMania-Wii (2012) and never finished |
| **Dance Clone** | [lefrac01/DanceClone](https://github.com/lefrac01/DanceClone) | C++98, Make | SDL 1.2 | libogc MP3Player/ASND | .sm subset | LGPL‑2.1 file / GPLv2+ headers | Dead (2013‑01‑16) | **Yes**, it is a native Wii game |
| **StepMania-Wii** | Google Code `stepmania-wii` (gone, only your copy) | C++03 | GX (partial) | ASND, libmad, tremor | via SM 3.9 | MIT | Dead (2012‑04‑12) | Unfinished |

### Can a StepMania-based engine be ported? (Option A)

From the dependency lists above, plus the Wii's hardware:

- **Graphics:** StepMania 5 and its forks render through OpenGL with GLEW (GL 2+ era extensions and shaders). The Wii's GPU ("Flipper/Hollywood" GX) is **fixed-function with a TEV combiner, not shader-based**. devkitPro packages an `opengx` GL shim **[doc: pacman-packages/wii/opengx]**, but it emulates only a fixed-function GL subset. Getting StepMania 5's renderer and themes onto it would be a research project in its own right.
- **Video and audio:** FFmpeg is effectively required, for background videos and some audio. It isn't packaged for the Wii.
- **Memory:** 24 MB MEM1 + 64 MB MEM2 **[doc: WiiBrew]**. Modern StepMania themes, the song cache, textures and Lua comfortably exceed this.
- **Evidence:** the one real attempt (StepMania-Wii, which targeted the much smaller SM 3.9) stopped before showing a working boot.

**Conclusion: porting StepMania 5 / ITGmania / Etterna / OutFox is not realistic.** Their **file formats and timing algorithms** are what's worth reusing.

### Other Wii rhythm/DDR homebrew

- **Dance Clone** (WiiBrew page: "ideally a Game Cube dance mat") is the only maintained-at-some-point, source-available, Wii-native DDR game I found. **[doc: [WiiBrew Dance_Clone](https://wiibrew.org/wiki/Dance_Clone)]**
- Commercial Wii DDR titles (DDR Hottest Party, etc.) are games that run on a Wii, not source you can modify. They're out of scope, as you said.
- **Unknown:** I didn't find any other open Wii-native StepMania-format player. A wider search of WiiBrew/GameBrew categories ("Music games") would be a cheap follow-up, but it's unlikely to change the recommendation.

### Chart format references

- `.sm` spec: [StepMania wiki /sm](https://github.com/stepmania/stepmania/wiki/sm). Header tags `#OFFSET` (seconds), `#BPMS` (`beat=bpm,…`), `#STOPS` (`beat=seconds,…`). `#NOTES:` has 5 colon-separated fields (type, author, difficulty, meter, radar), then measures separated by `,` and ending with `;`. Rows per measure set the quantisation (4…192). Note characters are `0 1 2 3 4 M`, and later `L F K`. **[doc]**
- `.ssc` spec: [StepMania wiki /ssc](https://github.com/stepmania/stepmania/wiki/ssc) (marked "in progress"). Adds `#VERSION`, `#DELAYS`, `#WARPS`, `#TIMESIGNATURES`, `#TICKCOUNTS`, `#COMBOS`, `#SPEEDS`, `#SCROLLS`, `#FAKES`, `#LABELS`. A per-chart `#NOTEDATA:;` block carries `#STEPSTYPE`, `#DIFFICULTY`, `#METER`, `#NOTES` and optional **per-chart timing (split timing)**. **[doc]**
- **Test oracle:** [garcia/simfile](https://github.com/garcia/simfile) (Python, MIT) parses SM/SSC and converts beat ↔ time, including BPM changes, stops, delays, warps and split timing. It's ideal for generating expected values to unit-test our C++ parser on the PC. **[doc]**
- No standalone C/C++ `.sm`/`.ssc` parser library turned up (the GitHub `simfile` topic only lists Python/TS/Haxe projects and the ArrowVortex editor). So: **port StepMania 3.9's `MsdFile` + `NotesLoaderSM` + `TimingData` (MIT) and extend them for SSC.**

---

## 3. Wii homebrew ecosystem

| Component | Status | Notes |
|---|---|---|
| **devkitPro installer (Windows)** | v3.0.3 is "Latest" on [devkitPro/installer releases](https://github.com/devkitPro/installer/releases) **[doc]** | devkitPro asks people to link to [Getting Started](https://devkitpro.org/wiki/Getting_Started) rather than the release. That page blocked automated fetching today, so the steps in doc 03 are **[mem]** where marked |
| **devkitPPC** (GCC for PowerPC) | Installed by the `wii-dev` pacman group **[doc: WiiMC guide, devkitPro]** | |
| **libogc** | [devkitPro/libogc](https://github.com/devkitPro/libogc): **v3.2.1, 2026‑10‑15**, 2,099 commits, CI **[doc]** | Provides VIDEO, GX, PAD, WPAD (wiiuse), SI, ASND, AESND, MP3Player, LWP threads and timers |
| **libogc2** | [extremscorner/libogc2](https://github.com/extremscorner/libogc2) (also on Codeberg) | A fork with many fixes and extra GC accessory support (steering wheel, N64 devices, PAD extensions). It **installs from a separate package repository and can conflict with devkitPro portlibs.** Its PAD layer applies the same standard-controller type gate, so it gives no dance-mat advantage. **Use devkitPro libogc for this project** |
| **libfat-ogc** | packaged | SD/USB FAT access (`fatInitDefault`, `sd:/`, `usb:/`) |
| **GRRLIB** | [GRRLIB/GRRLIB](https://github.com/GRRLIB/GRRLIB) v4.6.1, MIT, built from source against `ppc-libpng`, `ppc-freetype`, `ppc-libjpeg-turbo`, `libfat-ogc` **[doc]** | A friendly wrapper over GX: PNG→texture, sprites, tiles, TTF text, blending. **Recommended renderer** |
| **SDL 1.2 / SDL2 for Wii** | `pacman-packages/wii/` has SDL, SDL2, SDL2_image/mixer/ttf/gfx, SDL_* **[doc]** | A viable alternative, but it adds a layer between us and audio timing |
| **opengx** | `pacman-packages/wii/opengx` **[doc]** | GL-on-GX shim. Not needed |
| Audio decoders | `ppc-libmad` (MP3), `ppc-libvorbisidec` (Tremor, integer Ogg Vorbis), `ppc-libvorbis`, `ppc-libogg`, `ppc-mpg123`, `ppc-libopus` **[doc]** | Tremor/libmad are integer decoders, a good fit for the Broadway CPU |
| Examples | [devkitPro/wii-examples](https://github.com/devkitPro/wii-examples): `audio/{modplay,mp3player,oggplayer}`, `graphics/gx/{gxSprites,neheGX,romfont,triangle}`, `devices/{network,usbgecko,usbkeyboard,wiimote}`, `filesystem/directory`, `threading`, `templates/{makefile,cmake/application}` **[doc]** | There is **no PAD example**, but the API is small (see doc 04) |
| Homebrew Channel | v1.1.3 (2016), GPLv2. Loads `/apps/<name>/boot.dol` (or `boot.elf`, which it prefers) + `meta.xml` + 128×48 `icon.png` **[doc: [WiiBrew HBC](https://wiibrew.org/wiki/Homebrew_Channel)]** | Includes **wiiload** network loading (`WIILOAD=tcp:<wii-ip>`) **[doc: [WiiBrew Wiiload](https://wiibrew.org/wiki/Wiiload)]** |
| Installing HBC | [wii.hacks.guide](https://wii.hacks.guide/get-started): **Wilbrand** (SD card), **str2hax** (no SD), or **ModMii** (PC-guided). LetterBomb (System Menu 4.3 only) is now "legacy". All of them run the HackMii installer, which installs HBC and BootMii. The guide then recommends a **NAND backup** and **Priiloader** **[doc]** | See doc 03 §6 |

### Wii hardware constraints (from WiiBrew)

| Resource | Value | Design impact |
|---|---|---|
| CPU | IBM Broadway PowerPC, 729 MHz, 256 KB L2 **[doc]** | Plenty for a 2D rhythm game, including MP3/Vorbis decoding (~5–10% of a core **[mem/exp]**). Avoid per-frame heap churn and parse charts at load time |
| RAM | **MEM1 24 MB + MEM2 64 MB** (GDDR3) **[doc]** | Don't decode whole songs to PCM by default (3 min of 48 kHz stereo 16-bit is ≈ 34.6 MB). Keep the compressed file in RAM and stream-decode. Downscale banners and backgrounds |
| GPU | Hollywood GX, 243 MHz, fixed-function TEV, textures up to 1024×1024 in GX tiled formats **[doc/src]** | Use texture atlases and RGBA8/RGB5A3 textures. GRRLIB handles the conversion |
| Video | 640×480 (EFB). NTSC 60 Hz, PAL 50/60 Hz. 480i/480p via component | Scroll positions must be computed from **time**, never from frame counts (PAL 50 Hz). Dance Clone forces PAL to 480 lines (`WiiVideo.cpp:48-56`) |
| Audio | DSP mixer at 48 kHz via ASND (16 voices) or AESND. "ARAM does not physically exist in Wii hardware" **[doc]** | Stream into an ASND voice. ASND can play 44.1 kHz voices by passing the rate as `pitch` **[src: asndlib.h]**. The clock comes from `ASND_GetTime`/`ASND_GetTimerVoice`/sample counters |
| Storage | SD/SDHC via the front slot (libfat). USB is also possible. FAT32 (exploits need ≤ 32 GB for the setup card) **[doc]** | Load the song list lazily and cache metadata |
| Input | 4 GameCube ports through SI polling. PAD state updates when `PAD_ScanPads()` runs **[src]** | Poll at least once per frame. Consider a sampling callback for sub-frame timestamps **[exp]** |

---

## 4. GameCube dance pad: key facts

The full detail and code are in [04-wii-controller-integration.md](04-wii-controller-integration.md).

- libogc's PAD driver accepts a device only if its SI type has `SI_TYPE_GC` (0x08000000) **and** `SI_GC_STANDARD` (0x01000000) set **[src: libogc `pad.c`, `si.h`]**.
- Dolphin models the GameCube dance mat with SI ID `SI_DANCEMAT = SI_TYPE_GC | SI_GC_STANDARD | 0x300` (= 0x09000300), so **it passes libogc's gate**. Its button word maps the arrows to `PAD_BUTTON_UP 0x8`, `DOWN 0x4`, `LEFT 0x1`, `RIGHT 0x2`, plus A, B, Z (the "+" button) and Start (the "−" button) **[src: Dolphin `SI_Device.h`, `SI_DeviceDanceMat.cpp`]**.
- Dance Clone reads the mat through the standard `PAD_ButtonsDown/Held` API and documents the same mapping **[src: `WiiInput.cpp:68-77`]**, and v0.5 was released for real hardware.
- libogc doesn't mask simultaneous Left+Right or Up+Down **[src: `pad.c`]**.
- **Unknown [exp]:** the exact SI type ID that **your** mat reports (a Konami/Nintendo first-party mat versus a third-party one). The first test program prints `SI_GetType()` per port, which answers this in 30 seconds.

---

## 5. Assumptions and unknowns

| # | Assumption / unknown | How we resolve it |
|---|---|---|
| U1 | Your mat reports a standard-controller SI type | Milestone 3 pad test (prints SI type and raw bits) |
| U2 | Dolphin's keyboard → GC D-pad mapping allows opposite directions at the same time | M3 test in Dolphin |
| U3 | Exact Dolphin menu names and paths for SD folder sync and logging (dolphin-emu.org refused automated fetches today) | Check in your installed Dolphin. Doc 03 marks these **[mem]** |
| U4 | Accuracy and smoothness of `ASND_GetTimerVoice`/`ASND_GetTime` as a song clock | M4 audio-clock experiment |
| U5 | SI polling rate (how often the pad is sampled per frame) under libogc defaults | M3 timestamp histogram |
| U6 | TV + audio output latency on your setup | Calibration screen (M5/M6) |
| U7 | Which audio formats your songs use (.ogg vs .mp3) | Inventory your simfile packs. Support both by M5 |
