# Nintendo game engines in the Wii decompilations

*Part of a second research set on what Nintendo's own Wii games can teach ddr-wii. The set: this document (what is in each decompilation and how far to trust it), [nintendo_gui_architecture.md](nintendo_gui_architecture.md), [nintendo_animation_systems.md](nintendo_animation_systems.md), [rhythm_game_engine_comparison.md](rhythm_game_engine_comparison.md) and [reusable_implementation_patterns.md](reusable_implementation_patterns.md), which ends with the recommendation. It builds on the first set, which starts at [recommended_implementation_plan.md](recommended_implementation_plan.md).*

*Markers: **[src]** read in source, **[src, NM]** read in a source file that does **not** yet compile to the original bytes (logic plausible but unverified), **[doc]** documentation or licence files, **[mem]** from memory (verify), **[exp]** still to be tested, **[measured]** counted by a script over the clones. Nothing has been implemented, and no project code has changed.*

---

## 1. Summary

- **All six repositories are decompilations: C/C++ written by people so that it compiles back to the original game's machine code.** None is a recompilation (machine code mechanically translated to C). How much is verified varies a lot. Mario Strikers Charged is 100% matched and linked for all five releases, while only 123 of 743 Rhythm Heaven Fever units match **[doc, measured]**.
- **Nintendo's games share four layers.** The platform SDK (Dolphin/RVL SDK: GX, VI, OS, MEM, pads, AX audio). Nintendo middleware: **JSystem** (GameCube era, still used by Twilight Princess and Super Mario Galaxy), **NW4R** (the Wii middleware: 2D layouts, fonts, models, sound, effects) and **EGG** (EAD's Wii framework: scenes, faders, display, heaps). Then team libraries (EAD Tokyo's `c/f/m/s` libraries in NSMBW, TP's process framework) and finally game code **[src]**.
- **The 2D architecture is the same everywhere:** a retained tree of panes built from a binary layout file, animated by keyframe curves bound by pane name, drawn in tree order with full GPU state per pane, plus a fader drawn on top **[src]**. The [GUI document](nintendo_gui_architecture.md) covers it.
- **Memory is managed per scene:** a heap (or allocation group) per scene that is thrown away whole when the scene ends, after waiting for the GPU to finish **[src]**.
- **Every game here steps time in video frames.** Rhythm Heaven Fever derives musical time from the audio position each frame but still advances its scripts and sprites per frame **[src]**. The [rhythm comparison](rhythm_game_engine_comparison.md) covers this.
- **Licensing: no code from these repositories can go into ddr-wii.** Five carry CC0, which covers only the contributors' own rights. The code reproduces Nintendo's and Next Level Games' programs, and CC0 can't licence those. NSMBW-Decomp has no licence at all. The ideas can be re-implemented, as ddr-wii already does with StepMania-Wii (§10).

---

## 2. What was reviewed

| Repository (pinned commit) | Game (developer, year [mem]) | Licence file [doc] | How it builds [doc/src] | Match status at the pin [measured from `configure.py`] |
|---|---|---|---|---|
| [conhlee/rhf][rhf-repo] `02b140c9` | Rhythm Heaven Fever (Nintendo SPD with TNX, 2011) | CC0-1.0 | decomp-toolkit template; USA `SOME01` | 123 units Matching, 620 NonMatching. `GameUtil` (the engine): 19 Matching, 10 NonMatching |
| [doldecomp/ogws][ogws-repo] `e5a4d369` | Wii Sports (Nintendo EAD, 2006) | CC0-1.0 | decomp-toolkit template | 474 Matching, 317 NonMatching, 54 matching on every release except USA rev 0. EGG core 21 of 25 units match. NW4R `lyt` 12 of 15 |
| [zeldaret/tp][tp-repo] `c8fa8c9e` | Twilight Princess (Nintendo EAD, 2006, GameCube and Wii) | CC0-1.0 | decomp-toolkit template; GameCube, Wii and Shield versions ([`configure.py` L572-576][tp-vers]) | 147 Matching, 641 matching for listed releases, 246 NonMatching, 14 Equivalent. **JSystem matches the GameCube releases only** (`MatchingFor(ALL_GCN)`) |
| [SMGCommunity/Petari][smg-repo] `c8e636b8` | Super Mario Galaxy (Nintendo EAD Tokyo, 2007) | CC0-1.0 | decomp-toolkit template ([`configure.py` L673-677][smg-vers]) | 1,829 units match at least one release, 359 match none (`MatchingFor()` with no releases). `Game/Screen` 121 of 137, `RhythmLib` 13 of 15, NW4R `lyt` 15 of 15 |
| [NSMBW-Community/NSMBW-Decomp][nsmbw-repo] `58fe6985` | New Super Mario Bros. Wii (Nintendo EAD, 2009) | **none** | Own tools: compiles the sources and "slices" every part without a source from the player's own DOL/RELs. The rebuilt binaries must be byte-identical ([README L28-38][nsmbw-readme]) | 112 source files in `source/dol`. Only code that matches is used. README rejects AI-generated decompilation |
| [yannicksuter/mscharged-decomp][msc-repo] `b843f01b` | Mario Strikers Charged (Next Level Games, 2007) | CC0-1.0 plus third-party licences in `LICENSES/` | decomp-toolkit template | 356 Matching plus 983 matching for listed releases, none NonMatching. README: "100% code / 100% linked" on five releases, and a separate PC port compiles the decompiled source ([README L25-47][msc-readme]) |

Games and SDKs differ: Twilight Princess is a GameCube game ported to the Wii, so it uses the GameCube-era JSystem alongside Wii pieces. Super Mario Galaxy uses JSystem for 3D, audio and memory, but NW4R for its 2D layouts. Wii Sports and NSMBW use EGG and NW4R. Rhythm Heaven Fever uses NW4R with its own engine. Mario Strikers Charged uses Next Level Games' own engine (`NL`) on the RVL SDK **[src]**.

---

## 3. Reconstructed source vs generated code

### 3.1 How the builds decide what is verified

- **decomp-toolkit template** (rhf, ogws, tp, Petari, mscharged). `configure.py` lists every compilation unit with a status ([rhf L301-310][rhf-status]):
  - **Matching** units compile from the repository's C/C++ with the original CodeWarrior version and must give the same bytes. objdiff checks this.
  - **NonMatching** units are *not* linked. The build links the original code instead, which decomp-toolkit extracts from the player's own `main.dol` at build time, using `config/<version>/splits.txt` and `symbols.txt`.
  - **Equivalent** units are linked only with `--non-matching`.
  So **a NonMatching `.cpp` in the repository is a draft.** Often it is complete and differs only in register allocation (rhf's comments say "regswap"), but nothing has proved it.
- **NSMBW-Decomp** has no unverified sources: a function either matches and is compiled, or the original bytes are sliced in ([README][nsmbw-readme]).
- **What is generated, not written by hand:**
  - Extracted assembly and objects: in the build directory, never committed.
  - The symbol and split tables: tool output, then named by hand.
  - `objdiff.json`.
  - Build-time includes such as mscharged's disc-message image header.
  - Address-named units in rhf (`code_8008E430.cpp`, `data_802E57E0.cpp`): code whose purpose is still unknown.
- **Recompilation is a different thing.** A static recompiler translates PowerPC machine code into C mechanically, with no readable structure. None of the six repositories is one. mscharged's README says so explicitly, and notes that its PC port builds from the decompiled source, not a recompiler ([README L25][msc-readme], [L33][msc-readme]).

### 3.2 How readable the reconstructed code is [measured]

Placeholder names are what a decompiler leaves when nobody knows a function's or field's purpose yet. Counted in the clones (the second count also catches names like `_1C`, so it is an upper bound):

| Source tree | Lines | `fn_8xxxxxxx` functions | `unk…` / `field_0x…` / `_NN` members |
|---|---|---|---|
| rhf `Game` + `GameUtil` | 32,863 | 2,678 | 2,078 |
| ogws `Pack` (game) / `egg` | 24,289 / 13,478 | 42 / 0 | 295 / 97 |
| tp `src/d` (game) / JSystem | 842,126 / 68,166 | 0 / 0 | 69,617 / 3,083 |
| Petari `src/Game` | 389,263 | 0 | 20,865 |
| NSMBW `source/dol` | 42,371 | 42 | 56 |
| mscharged `Game` + `NL` | 258,586 | 6 | 17 |

So rhf's logic is readable, but much of it is still machine-named: `CTickFlowManager::fn_801E2698` is "ticks this frame". Mario Strikers Charged and NSMBW are almost fully named. TP and Galaxy name their functions but leave many fields unnamed.

### 3.3 Files cited in this set that are not verified

| File | Status | Used for |
|---|---|---|
| rhf `TickFlow.cpp`, `InputChecker.cpp`, `InputCheckManager.cpp`, `CellAnimManager.cpp`, `Layout.cpp`, `LayoutManager.cpp`, `CheckPointManager.cpp`, `TimeRatio.cpp`, minigame `MyFlow`/`MyChecker` files | NonMatching (except `AgbTap`, `Grading`, `Title`, which match) | Script interpreter, input judging, sprite drawing, buttons. Marked **[src, NM]** |
| tp `J2DPane.cpp` Wii-only branches (`#if !PLATFORM_GCN`) | JSystem matches GameCube builds only | Widescreen adaptation. Marked **[src, Wii unmatched]** |
| ogws `lyt_material.cpp` | NonMatching in Wii Sports | Cited from Galaxy instead, where it matches ([Petari][smg-mat]) |

Matching files used heavily: rhf `TickFlowManager.cpp`, `Sound.cpp`, `Controller.cpp`, `Scene.cpp`, `GameManager.cpp`, `CellAnim.cpp`, `LayoutAnimation.cpp`, `Mem.cpp`; ogws EGG core and NW4R `lyt_pane`/`lyt_layout`/`ut` fonts; TP JSystem (GameCube); Petari `Game/Screen`, `Game/System`, `RhythmLib`, NW4R `lyt`; all of NSMBW and mscharged.

---

## 4. The layers: Nintendo frameworks vs game-specific code

| Layer | What it is | Where it appears [src] |
|---|---|---|
| Platform SDK | Dolphin SDK (GameCube) / RVL SDK (Wii): GX graphics, VI video, OS threads and timers, MEM heaps, PAD/KPAD/WPAD input, AX/DSP audio, DVD/NAND | `revolution/` or `RVL_SDK/` in every repo, `dolphin/` in tp |
| JSystem | EAD's in-house framework from the GameCube era: J2D (2D layouts), J3D (models, materials, draw buffers), JUtility (textures, fonts, pads, video, XFB, fader, console), JKernel (heaps, archives, DVD, decompression), JFramework (display loop), JParticle, JAudio2, JMessage, JStudio | tp `libs/JSystem`; Petari `libs/JSystem` (no J2D UI) |
| NW4R | NintendoWare for Revolution, the Wii middleware: `lyt` (layouts), `ut` (fonts, text writer, lists), `g3d` (models), `snd` (sound archives, streams, sequences), `ef` (effects), `math` | ogws, rhf, Petari, NSMBW |
| EGG | EAD's Wii framework on top of NW4R: `core` (system, heaps, threads, scenes, faders, display, XFB, archives), `gfxe` (post effects, screens, lights), `audio`, `math` | ogws, NSMBW (`lib/egg`) |
| Team libraries | EAD Tokyo's `cLib` (counters, trees), `framework` (`fBase_c` process tree and profiles), `mLib` (2D/3D wrappers, faders, pads, video, heaps), `sLib` (state machines, phases) | NSMBW |
| | TP's process framework: `f_pc` (processes), `f_op` (actors, scenes, overlaps = transitions), `m_Do` (machine layer), `c` (core) | tp |
| | Next Level Games' `NL` engine (tasks, timers, allocators, fonts, text boxes, GX material programs) | mscharged |
| Game code | `d_*` in TP and NSMBW, `Game/*` in Petari, `Game/` + `GameUtil/` in rhf, `Pack/RP*` in ogws, `Game/*` in mscharged | — |

Rhythm Heaven Fever's `GameUtil` is the game's own engine on NW4R: TickFlow scripts, `CInputCheckManager`, `CCellAnimManager`, `CLayoutManager`, `CGameManager`, `CSceneManager`, `CSoundManager` **[src]**. It isn't a Nintendo-wide framework. Its timing ideas still descend from the earlier Rhythm Heaven games (the GBA original and Rhythm Heaven DS, which used the same tick-based scripts) **[mem]**.

---

## 5. JSystem in detail

All files under tp `libs/JSystem` (matching the GameCube builds) unless noted.

| Component | What it does | Key classes and files [src] | Lesson for ddr-wii |
|---|---|---|---|
| **J2D** | Retained 2D panes from `.blo` screen files: `J2DScreen` holds the tree; `J2DPane` has bounds, global bounds, a clip rectangle, local and global matrices, alpha with optional inheritance, an anchor (one of 9 base positions), a rotation pivot and SRT; subclasses `J2DPicture`, `J2DTextBox`, `J2DWindow` (9-slice). Animations of seven kinds bind to panes | [`J2DPane.h` L51-238][j2d-pane-h], [`J2DPane::draw` L246-344][j2d-pane-draw], `J2DAnimation.h`, `J2DOrthoGraph` | The pane model, anchors and per-pane clipping ([GUI doc §2](nintendo_gui_architecture.md#2-the-shared-model-a-retained-pane-tree-built-from-layout-data)) |
| **J3D** | Models with precompiled material display lists; draws are queued into `J3DDrawBuffer`s and sorted by material, by Z or not at all. In material mode, packets that share a texture merge into one bucket and are drawn with one material setup | [`entryMatSort` L54-95][j3d-matsort], [sort table L207-209][j3d-sorttable]; `J3DFrameCtrl` modes none / reset / loop / reverse / loop-reverse with a rate ([`J3DAnimation.h` L932-980][j3d-framectrl]) | Texture-keyed batching for the sprite batch; loop/reverse play modes |
| **JUtility** | `JUTTexture`/`JUTPalette` (textures from `ResTIMG`, the BTI header), `JUTResFont`/`JUTCacheFont` (bitmap fonts in glyph sheets), `JUTGamePad` (pads with key repeat and stick-to-direction hysteresis), `JUTVideo`/`JUTXfb` (video mode, 1–3 frame buffers), `JUTFader`, `JUTProcBar` (CPU/GPU time bars), `JUTConsole`, `JUTException` | [`CButton::update` L295-339][jut-pad] (repeat), [`JUTGamePad.cpp` L200-202][jut-stick] (press 0.5 / release 0.25) | Time-based key repeat; on-screen timing bars |
| **JKernel** | A tree of heaps; the "current heap" serves global `new`. `JKRExpHeap` is a general allocator. `JKRSolidHeap` is a bump allocator that can't free single blocks and can shrink to what it used. `JKRDisposer` objects register with the heap they live in and are destroyed with it. Archives (`JKRArchive`: memory, ARAM, DVD, compressed), DVD rippers, Yaz0/Yay0 decompression, threads | [`JKRSolidHeap.cpp` L58-80][jkr-adjust] (shrink), [L108-133][jkr-alloc] (bump), [L161-163][jkr-free] (no free); [`JKRHeap.cpp` L150-154][jkr-current]; [`JKRDisposer.cpp` L6-17][jkr-disposer] | Per-scene arenas; load-then-shrink |
| **JFramework** | `JFWDisplay`: frame begin/end, single/double/triple XFB exchange, the fader drawn at the end of GX work, waiting by retrace or OS time | [`JFWDisplay.cpp` L99-217][jfw-xfb] | Same frame skeleton as ddr-wii's `WiiVideo` |
| **JParticle** | Particle emitters and resources (`JPAEmitter`, `JPAResourceManager`) | `libs/JSystem/src/JParticle` | Pooled particles, already in the earlier plan |
| **JAudio2** | Sound system: sequences (`JASSeqParser`, `JASTrack`), streams (`JAIStream`), effects | used by Galaxy's `RhythmLib` ([rhythm doc §3](rhythm_game_engine_comparison.md#3-the-song-clock-where-musical-time-comes-from)) | — |
| **JMessage**, **JStudio**, **JGadget**, **JSupport**, **JMath** | Message text with control codes; cutscene timelines; containers; trees, lists, streams; math | — | — |

---

## 6. NW4R and EGG: the Wii-era middleware

- **NW4R `lyt`** is J2D's successor, with the same ideas and a cleaner split ([GUI doc §2](nintendo_gui_architecture.md#2-the-shared-model-a-retained-pane-tree-built-from-layout-data)):
  - `Layout::Build` reads a binary layout; `CreateAnimTransform` loads an animation; `BindAnimation`, `Animate`, `CalculateMtx` and `Draw` run each frame ([`lyt_layout.cpp` L61-300][lyt-layout]).
  - `Pane` does the transform and alpha work ([`CalculateMtx` L220-278][lyt-calcmtx]).
  - `Material::SetupGX` loads every texture and TEV setting for each pane ([Galaxy `lyt_material.cpp` L644-970][smg-mat]).
- **NW4R `ut`** provides bitmap fonts. A font is a set of texture sheets, each a grid of glyph cells with a 1-pixel gap ([`GetGlyphFromIndex` L182-208][ut-glyph]). `CharWriter` draws one quad per glyph and reloads the texture only when the sheet changes ([L134-189][ut-print]), with vertex-colour gradients ([L191-203][ut-grad]) and a two-colour TEV mapping ([L229-][ut-colmap]).
- **EGG core**:
  - `Scene` with per-scene MEM1 and MEM2 heaps ([`eggScene.h` L11-80][egg-scene]).
  - `SceneManager` with parent/child scenes and operations deferred until a fade-out completes ([`eggSceneManager.h` L12-130][egg-scenemgr-h]; create L126-204, destroy L286-343, after-fade dispatch L357-400 in [`eggSceneManager.cpp`][egg-scenemgr]).
  - The `Fader` interface and `ColorFader` ([`eggColorFader.cpp` L72-167][egg-colorfader]).
  - `Display` and `AsyncDisplay` frame pacing ([`eggDisplay.cpp` L24-46][egg-display], [`eggAsyncDisplay.cpp` L36-136][egg-async]).
  - Heaps and threads.
- **NW4R `snd`** (sound archives, streams and sequences) is what Rhythm Heaven Fever reads its playback position from: `StrmSoundHandle::GetPlaySamplePosition`, `SeqSoundHandle::GetTick` ([rhythm doc §3](rhythm_game_engine_comparison.md#3-the-song-clock-where-musical-time-comes-from)).

---

## 7. Frame pipeline and memory

- **Frame order** is the same everywhere: read input, run logic, draw scene, draw fader, then end the frame. Rhythm Heaven Fever ([`GameManager.cpp` L62-127][rhf-mainloop]) does it in this order:
  1. Read pads (only while a scene is active).
  2. Service DVD and archive loads.
  3. Advance TickFlow (musical time).
  4. Match inputs to checkers, then update checkpoints.
  5. If the scene is dead: delete it, free its memory groups and create the next scene.
  6. Update the scene, the fader and sound.
  7. Begin graphics, draw the scene, draw the fader, end graphics.
- **Frame pacing:**
  - EGG's `Display` waits a set number of retraces and measures each frame's real duration with `OSGetTick` ([L24-46, L84-92][egg-display]).
  - `AsyncDisplay` frees the CPU from the GPU. A post-retrace callback wakes the main thread. The EFB is copied to a frame buffer only if one is free, otherwise the frame is dropped. The buffer swap happens in the GX draw-done callback ([L36-44, L89-136][egg-async]).
  - Galaxy paces its loop by OS time at 59.94 Hz on PAL instead of waiting for the 50 Hz retrace ([`MainLoopFramework.cpp` L305-308, L398-401][smg-ticks]; [`GameSystemFrameControl.cpp` L8-17][smg-framectrl]).
- **Memory per scene:**
  - EGG gives each scene its own expanded heaps in MEM1 and MEM2, created under the parent scene's heaps. Destroying a scene calls `exit()`, destroys child scenes, **waits for the GPU (`GXFlush` + `GXDrawDone`)** and then destroys the heaps ([L286-343][egg-scenemgr]).
  - Rhythm Heaven Fever tags every allocation with a memory group (common assets, scene assets, scene object), using the RVL SDK's expanded-heap group IDs. When a scene dies it frees whole groups ([`Mem.hpp` L15-21][rhf-memgroups], [`Mem.cpp` L142-160][rhf-memfree]).
  - JSystem shrinks a solid heap to its used size after loading ([L58-80][jkr-adjust]).
- **Lesson for ddr-wii.** ddr-wii frees song memory with `malloc`/`free` today. An arena per scene (song select, gameplay), freed only after `GX_DrawDone`, removes both fragmentation and the risk of freeing a texture the GPU is still reading. This goes into the [patterns doc](reusable_implementation_patterns.md#2-the-pattern-catalogue).

---

## 8. Rendering and texture management

| Topic | What the games do [src] | Note for ddr-wii |
|---|---|---|
| Texture formats | BTI (`ResTIMG`) in JSystem, TPL in rhf, NW4R `TexMap`. Colour-index formats with TLUT palettes for recolouring (rhf `loadChrForDraw` CI path, [`CellAnimManager.cpp` L400-446][rhf-loadchr] **[src, NM]**; NW4R loads TLUTs per material) | TPL via gxtexconv is already the earlier plan. CI8 + TLUT is a cheap way to recolour notes per player **[exp]** |
| GPU state per draw | NW4R sets every material setting per pane ([Galaxy SetupGX][smg-mat]). rhf sets TEV once per pass but loads a texture and a matrix per sprite part ([`CCellAnim::draw` L233-319][rhf-celldraw]). `CharWriter` caches the font texture | State per element is fine for menus with tens of panes. Gameplay needs the batch (earlier [graphics doc §4](graphics_library_comparison.md#4-2d-textured-quads-what-each-library-does-per-sprite)) |
| Sorting | J3D merges draws that share a texture ([L54-95][j3d-matsort]). TP registers 2D and 3D draws into ordered lists ([GUI doc §7](nintendo_gui_architecture.md#7-draw-ordering)) | A sort key of layer, then texture, for the sprite batch |
| Shaders | Next Level Games wraps each TEV setup in a named "material program" with a registry (`NL/glx/GX*MaterialProgram*.cpp`, `GXMaterialProgramRegistry`) | A handful of named TEV presets in `render/` (normal, additive, tint/remap, font) instead of ad-hoc GX calls |
| Recolouring | rhf's sprite TEV computes `C0·(1−tex) + C1·tex`, so greyscale art becomes any two-colour ramp ([`drawSetupDefault` L175-194][rhf-celltev]). NW4R `CharWriter` does the same for text ([L229-][ut-colmap]) | One greyscale arrow texture, recoloured per note type (4th, 8th, …) or per player with no extra textures |
| Virtual canvas | rhf draws sprites in a 608×456 orthographic space (832×456 on 16:9) ([L157-159, L333-348][rhf-canvas] **[src, NM]**). TP's layouts are 608 units wide ([GUI doc §9](nintendo_gui_architecture.md#9-169-and-the-safe-area)) | ddr-wii can lay out in a safe 608×456 space (doc 06 §11.3 already keeps playfields inside the safe area) |

---

## 9. Portability: why the code doesn't move between projects

1. **Different SDKs.** Nintendo's code calls the RVL or Dolphin SDK (`GXSetTevColor`, `OSSleepThread`, `MEMAllocFromExpHeap`, `KPADRead`, NW4R `snd`). ddr-wii uses libogc (`GX_SetTevColor`, LWP threads, `malloc`, `PAD_Read` through `wii::Input`, ASND). Names, types and behaviour differ, and some RVL features (NW4R sound, expanded-heap groups) have no libogc equivalent.
2. **Different compilers.** The code is shaped to make CodeWarrior emit the original bytes: `#pragma` blocks, register order, odd temporaries, inline assembly in a few files **[measured: 0–6 files per source tree]**. GCC builds it differently, or not at all.
3. **Each game's framework is tied to its process model:** TP's `f_pc` processes, Galaxy's `NameObj` executors, NSMBW's `fBase_c` profiles, rhf's singletons. A class taken out drags its framework with it.
4. **The data formats are Nintendo's** (`.blo`, `.brlyt`, `.brlan`, `.brfnt`, `.brcad`, `.bti`). They are made by Nintendo's tools, and no repository contains assets: each one needs the user's own disc. ddr-wii has neither the tools nor the files, and doesn't need them.
5. **Versions differ even within Nintendo.** EGG in Wii Sports and in NSMBW are different versions, NW4R `lyt` matches different releases in different games, and TP's JSystem is verified for GameCube builds only.

So only **ideas** transfer: a data model, an update order, a formula, a state machine.

---

## 10. Licensing

| Source | Licence [doc] | What it permits ddr-wii |
|---|---|---|
| rhf, ogws, tp, Petari, mscharged | **CC0-1.0** (`LICENSE` / `LICENSE.md`) | CC0 waives the *contributors'* copyright in their own contributions. These are matching decompilations, so they reproduce Nintendo's (and Next Level Games') programs closely enough to compile back to them. A contributor can't waive rights they don't hold, so the dedication doesn't licence the underlying code. **No copying into ddr-wii.** |
| NSMBW-Decomp | **No licence file** | All rights reserved by default, on top of the same issue. **No copying.** |
| Third-party code inside mscharged | ODE (BSD), the GameSpy SDK (BSD-3-Clause), Broadcom's Bluetooth stack in `RVL_SDK/bte` (Apache-2.0); licence texts in `LICENSES/` | Genuinely open, but ddr-wii needs none of them (libogc already covers Bluetooth and zlib) |
| Assets | None are included in any repository; each needs a dump of the user's own disc (README of each) | Nothing to ship, and nothing ddr-wii should derive from |

This is the rule CLAUDE.md already applies to StepMania-Wii's Wii files: read the code, describe the behaviour in our own words, and write new code from that description. This research set does exactly that. It cites files and lines and paraphrases them; the formulas it states are textbook maths (Hermite curves, sine easing). This isn't legal advice: if the project ever wanted to use more than ideas, it would need a lawyer, not a licence file.

The open-source projects this set suggests adapting, with their licences, are in the [patterns doc §4.2](reusable_implementation_patterns.md#42-adapt-from-open-source-libraries).

---

## 11. What this means for ddr-wii

- Keep the earlier architecture (retained UI, separate immediate playfield, time-based animation, batched sprites). The Nintendo code shows the same split, and **adds** these:
  - one arena per scene, freed after a GPU wait;
  - scene states that separate loading from showing;
  - deferred scene changes that run when a fade-out completes;
  - keyframe clips with jump keys and play modes;
  - state machines with time-in-state tweens;
  - a shared cursor;
  - texture-keyed sorting;
  - glyph sheets with a texture cache;
  - a two-colour TEV mode;
  - 16:9 adaptation of a 608-wide design.
- Don't adopt the frame-stepped timing they all use. ddr-wii's audio-derived clock and timestamped SI input already go further than Rhythm Heaven Fever's frame-quantised input ([rhythm doc §6](rhythm_game_engine_comparison.md#6-input-sampling-and-timestamps)).

[rhf-repo]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/README.md
[rhf-status]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/configure.py#L301-L310
[rhf-mainloop]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/GameManager.cpp#L62-L127
[rhf-memgroups]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Mem.hpp#L15-L21
[rhf-memfree]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Mem.cpp#L142-L160
[rhf-loadchr]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L400-L446
[rhf-celldraw]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnim.cpp#L233-L319
[rhf-celltev]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L175-L194
[rhf-canvas]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L157-L159
[ogws-repo]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/README.md
[tp-repo]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/README.md
[tp-vers]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/configure.py#L572-L576
[smg-repo]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/README.md
[smg-vers]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/configure.py#L673-L677
[smg-mat]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/nw4r/lyt/lyt_material.cpp#L644-L970
[smg-ticks]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/System/MainLoopFramework.cpp#L305-L401
[smg-framectrl]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/System/GameSystemFrameControl.cpp#L8-L17
[nsmbw-repo]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/README.md
[nsmbw-readme]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/README.md#L28-L38
[msc-repo]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/README.md
[msc-readme]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/README.md#L25-L47
[j2d-pane-h]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/include/JSystem/J2DGraph/J2DPane.h#L51-L238
[j2d-pane-draw]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J2DGraph/J2DPane.cpp#L246-L344
[j3d-matsort]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J3DGraphBase/J3DDrawBuffer.cpp#L54-L95
[j3d-sorttable]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J3DGraphBase/J3DDrawBuffer.cpp#L207-L209
[j3d-framectrl]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/include/JSystem/J3DGraphAnimator/J3DAnimation.h#L932-L980
[jut-pad]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JUtility/JUTGamePad.cpp#L295-L339
[jut-stick]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JUtility/JUTGamePad.cpp#L200-L202
[jkr-adjust]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JKernel/JKRSolidHeap.cpp#L58-L80
[jkr-alloc]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JKernel/JKRSolidHeap.cpp#L108-L133
[jkr-free]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JKernel/JKRSolidHeap.cpp#L161-L163
[jkr-current]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JKernel/JKRHeap.cpp#L150-L154
[jkr-disposer]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JKernel/JKRDisposer.cpp#L6-L17
[jfw-xfb]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JFramework/JFWDisplay.cpp#L99-L217
[lyt-layout]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_layout.cpp#L61-L300
[lyt-calcmtx]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_pane.cpp#L220-L278
[ut-glyph]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_ResFontBase.cpp#L182-L208
[ut-print]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L134-L189
[ut-grad]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L191-L203
[ut-colmap]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L229-L265
[egg-scene]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/include/egg/core/eggScene.h#L11-L80
[egg-scenemgr-h]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/include/egg/core/eggSceneManager.h#L12-L130
[egg-scenemgr]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggSceneManager.cpp#L126-L400
[egg-colorfader]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggColorFader.cpp#L72-L167
[egg-display]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggDisplay.cpp#L24-L92
[egg-async]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggAsyncDisplay.cpp#L36-L136
