# Wii graphics architecture for a rhythm game

*Part of the five research documents (start with [recommended_implementation_plan.md](recommended_implementation_plan.md)). It turns the hardware limits and the survey into concrete rendering rules for ddr-wii's screens. Markers: **[src]** read in source, **[doc]** documentation, **[mem]** from memory (verify), **[exp]** still to be tested, **[measured]** this project's own runs on the real Wii (doc 05, M2, M2b and M3).*

---

## 1. Hardware facts that shape the design

| Fact | Value | Source |
|---|---|---|
| CPU | IBM Broadway PowerPC, 729 MHz | **[doc: [WiiBrew Hardware][wb-hw]]** |
| GPU | Hollywood, 243 MHz, "identical to Flipper's albeit with minimal corrections", 1.5× Flipper's clock | **[doc: [WiiBrew][wb-hw], [Copetti, Wii][cop-wii]]** |
| GPU embedded memory | 3 MB. On Flipper that is 2 MB of framebuffer/Z and 1 MB of texture cache (TMEM) | **[doc: WiiBrew]**; split **[doc: Copetti GameCube, summary]** |
| EFB (render target) | 640 px stride, 528 lines in `RGB8_Z24` / `RGBA6_Z24`; `RGB565_Z16` turns on 3-sample multisampling and halves it to 640 × 264 | **[src: [gx.h:3476-3486][ogc-pixfmt]]** |
| Pixel pipelines / fill | 4 pixel pipelines; theoretical ≈ 4 × 243 MHz ≈ 970 Mpx/s | pipelines **[doc: [Copetti, GameCube][cop-gc]]**; rate **[mem]** (doc 06 §11.5) |
| TEV and texturing | 16 TEV stages, 8 texture maps, 8 texture coordinates per pass | **[src: gx.h `GX_MAX_TEVSTAGE` 16, `GX_MAX_TEXMAP` 8, `GX_TEXCOORD7`]** |
| Texture limits | Up to 1024 × 1024; image data 32-byte aligned; mipmapped textures must be powers of two | **[src: [`GX_InitTexObj` doc][ogc-inittex]]** |
| Texture formats | I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CI4, CI8, CI14, CMPR | **[src: [gx.h:291-301][ogc-fmts]]** |
| Main memory | MEM1 24 MB 1T-SRAM, MEM2 64 MB GDDR3; IOS keeps 12–16 MB of MEM2 | **[doc: WiiBrew; Copetti, Wii]** |
| malloc | Grows through MEM1, then continues into MEM2 | **[src: [libogc `sbrk.c`][ogc-sbrk]]** |
| CPU → GPU | Commands go through a FIFO in main RAM (256 KB in GRRLIB, libgui, WiiFlow and HBC); display lists are pre-built command buffers the GPU reads through a separate 4 KB prefetch FIFO | **[src: GRRLIB_core.c:35, gx.h `GX_CallDispList`]**, **[doc: Copetti GameCube]** |
| Video output | EFB copied to an XFB in main RAM (YUV, 2 bytes per pixel: ≈600 KB at 640 × 480); NTSC/PAL60 at 60 Hz, PAL50 at 50 Hz; 480i, 480p, 576i | **[doc: Copetti GameCube, summary]**, **[mem]** for the byte count; modes **[src: [WiiVideo.cpp](../game/source/platform/wii/WiiVideo.cpp)]** |

**What this means:** the GPU has plenty of fill rate and TEV power for a 2D game, there is no separate VRAM (textures live in MEM1/MEM2 and are read through a 1 MB cache), memory is the scarce resource, and the CPU's per-draw overhead is the first thing to run out (doc 06 §11.5 reached the same conclusion; M2b confirmed it on hardware).

---

## 2. Memory budget

| Region | Size | Already used by ddr-wii | Left for graphics |
|---|---|---|---|
| MEM1 | 24 MB | Code and static data, the MEM1 part of the heap, 2 XFBs (≈1.2 MB at 480 lines, ≈1.5 MB at 576), GX FIFO 256 KB | A few MB of heap **[exp: watch the heap display]** |
| MEM2 | 64 MB − IOS 12–16 MB ≈ 48–52 MB | The 24 MB song-file buffer reserved at the top of MEM2 (doc 05, M5 step 2); audio ring and decoder state | ≈ 24–28 MB for textures, caches and heap overflow **[derived]** |

**Texture sizes** (bytes per texel: RGBA8 4, RGB5A3/RGB565/IA8 2, I8/IA4/CI8 1, I4/CMPR 0.5) **[src: format definitions]**:

| Texture | RGBA8 | 16-bit (RGB5A3, RGB565, IA8) | 8-bit (I8, IA4) | 4-bit (I4, CMPR) |
|---|---|---|---|---|
| Playfield atlas 512 × 512 | 1 MB | 512 KB | 256 KB | 128 KB |
| Font page 512 × 512 | — | 512 KB (IA8) | 256 KB (I8) | 128 KB (I4) |
| Banner 256 × 100 (as now) | 100 KB | 50 KB | — | ≈13 KB (CMPR, 256 × 104) |
| Song background 640 × 480 | 1.2 MB | 600 KB | 300 KB | 150 KB |
| Full-screen render target | 1.2 MB | 600 KB | — | — |

So 64 cached banners cost 6.4 MB as RGBA8, 3.2 MB as RGB565 and 0.8 MB as CMPR. Banners are opaque, so **RGB565 halves the cache for free** (a format conversion, no encoder), and CMPR is the next step if the cache must grow (WiiFlow has a software encoder: [`_convertToCMPR`][wf-cmpr]) **[src]**.

**Rules:**
- Every texture buffer is 32-byte aligned (`memalign(32, …)`, as GRRLIB does in [`GRRLIB_CreateEmptyTextureFmt`][gr-empty]) **[src]**.
- Large, long-lived buffers go to MEM2 explicitly (WiiFlow allocates all texture data with `MEM2_alloc` **[src: [texture.cpp][wf-tex]]**); doc 06 §8 already says so.
- Caches have a byte budget and evict LRU (Snes9x GX keeps a single preview on the Wii for this reason **[src: [menu.cpp:1242-1246][s9-cachecfg]]**).
- Shipped art is prebuilt as TPL in the smallest format that looks right (§3), not decoded from PNG at boot.

---

## 3. Texture formats and the texture cache

| Format | bpp | Use in ddr-wii |
|---|---|---|
| **I4 / I8** | 4 / 8 | Glyph atlases, glows, masks: white-on-alpha art tinted by vertex colour |
| **IA4 / IA8** | 8 / 16 | Greyscale art with separate alpha (soft shapes, outlines) |
| **RGB5A3** | 16 | Colour UI art with alpha (3-bit alpha where translucent, 5-bit colour where opaque) |
| **RGB565** | 16 | Opaque photos: banners, backgrounds |
| **CMPR** | 4 | Large opaque images when memory is tight; built offline with gxtexconv or at runtime with an encoder |
| **RGBA8** | 32 | Only where 8-bit alpha *and* full colour are both needed (render targets, gradients that band in RGB5A3) |
| CI4/CI8 | 4/8 | Palette tricks (colour cycling); not needed now |

- **Data is tiled.** GX textures are stored in 4×4 (or 8×4 / 8×8) blocks, so a CPU-written texture has to be swizzled (libgui [`loadTextureData`][lg-loadtex], GRRLIB [`RawTo4x4RGBA`][gr-raw]) **[src]**. Sides that aren't multiples of the block size need padding: GRRLIB's PNG path blanks such textures ([texEdit.c:285-289][gr-png]) **[src]**, and ddr-wii already avoids it (doc 06 §7).
- **Cache coherence.** After the CPU writes texture memory: `DCFlushRange` on the data, then `GX_InvalidateTexAll()` (or `GX_InvalidateTexRegion`) before the GPU samples it **[src: GRRLIB `GRRLIB_FlushTex` + `GRRLIB_Render`; Snes9x GX [presentFrame][s9-video]]**. Once per frame is enough. **Never** per draw, as libgui's `drawTexture` does ([OgcVideoDriver.cpp:225-273][lg-drawtex]) **[src]**.
- **The 1 MB TMEM is a cache, not a limit.** Textures are read from MEM1/MEM2 through it; locality (atlases, few texture switches) matters more than total size **[mem]**. libogc exposes cache regions and preloading (`GX_InitTexCacheRegion`, `GX_PreloadEntireTexture`, [gx.h:4221-4520][ogc-tmem]) **[src]**, not needed at this scale.
- **Wrapping.** UV-scrolling backgrounds and tiling hold bodies need `GX_REPEAT`, which needs power-of-two sides **[mem; exp: confirm on hardware]**. Make those textures 64/128/256 px.
- **Mipmaps** only for art drawn much smaller than its size (a 3D banner carousel); WiiFlow uses trilinear filtering with LOD bias and anisotropy for its covers ([coverflow.cpp:1344][wf-lod]) **[src]**.
- **Premultiplied alpha** for UI atlases avoids dark fringes under bilinear scaling (graphics doc §6) **[mem/exp]**.

---

## 4. Frame timing at 60 and 50 Hz

**The present path today** ([`WiiVideo::endFrame`](../game/source/platform/wii/WiiVideo.cpp#L44-L82) → [`GRRLIB_Render`][gr-render]): CPU builds the frame → `GX_DrawDone` (wait for the GPU) → copy EFB to XFB → `VIDEO_WaitVSync` **[src]**. CPU and GPU never overlap across frames, which is simple and gives steady pacing as long as CPU + GPU stays under one refresh.

| Mode | Frame | Measured scene cost (4P worst case) | Headroom |
|---|---|---|---|
| NTSC / PAL60 | 16.68 ms | 1.58 ms CPU + ≈1 ms GPU wait **[measured, M2b]** | ≈5× |
| PAL50 | 20.0 ms | not measured on hardware | more |

**Rules:**

1. **Everything moves by time, never by frame count** (doc 06 §2). The animation doc shows the exact per-frame-to-time conversion for existing per-frame code such as the wheel easing ([SongSelect.cpp:316](../game/source/screens/SongSelect.cpp#L316)).
2. **A frame that overruns costs a whole extra refresh.** With a blocking vsync wait, 17 ms of work shows as a 33 ms frame. Scroll smoothness depends on the *worst* frame, so budgets are about maxima: keep the worst frame under about 12 ms of CPU + GPU at 60 Hz (the M2b criterion) **[exp]**.
3. **Nothing on the frame path may block or allocate:** no SD reads, no image decodes, no FreeType rasterisation, no `malloc` during gameplay (doc 06 §2). Loading runs on the worker thread (priority below the main thread, as `SongMedia` does at 40, [SongMedia.cpp](../game/source/screens/SongMedia.cpp)); uploads to textures happen on the main thread in bounded steps (one banner per frame, as Snes9x GX's cache does, [GuiImageAsyncCache.h][s9-cache]) **[src]**.
4. **Video latency.** Input is read at the start of a frame and shown at the next vsync, plus the TV's processing. The judge uses timestamps (doc 06 §3.4), so this only shifts what the player *sees*: that is what `visualOffset` is for (doc 06 §3.5). Rendering must not add queued frames.
5. **Interlaced output.** At 480i, 1-px horizontal lines flicker; the copy filter's vertical taps (deflicker) smooth them by blurring ([gx.h:3669][ogc-copyfilter]) **[src]**. Design UI with lines ≥ 2 px and offer "sharp" (filter off) for 480p **[exp]**.
6. **CPU/GPU overlap is available if ever needed:** `GX_SetDrawDone` + `GX_SetDrawDoneCallback` and `GX_SetDrawSync` tokens ([gx.h:1566-1679][ogc-drawdone]) **[src]** let the CPU build frame N+1 while the GPU draws frame N. Not needed now.

---

## 5. Throughput budgets

| Resource | Evidence | Proposed budget per frame (60 Hz) |
|---|---|---|
| GRRLIB-style draws (CPU) | ≈2.6 µs each, 1,600 draws = 4.20 ms avg **[measured, M2]** | ≤ 1,000 (≈2.6 ms) |
| Batched quads (CPU) | Not measured; per-quad work is a fraction of GRRLIB's (graphics doc §4) | ≤ 5,000 **[exp]** |
| Drawn area (GPU) | 2P scene: 1.21 Mpx with 0.99 ms CPU and 1.28 ms GPU wait → total GPU time 1.3–2.3 ms, i.e. ≈0.5–0.95 Mpx per ms **[measured M2b, derived]** | ≤ 4 Mpx in gameplay (≈4–8 ms of GPU) until richer art is measured **[exp]** |
| Texture switches | 4P: 82 switches within 1.58 ms CPU **[measured]** | ≤ 50 with atlases |
| Full-screen layers | each 0.31 Mpx at 640 × 480 | ≤ 4 in gameplay (background + dim + flashes), ≤ 6 in menus |
| Heap allocations | doc 06 §2 | 0 during gameplay |

The existing counters (draws, texture switches, drawn area, CPU, GPU wait; [Draw.h](../game/source/render/Draw.h), [WiiVideo.h](../game/source/platform/wii/WiiVideo.h)) measure all of these, in Dolphin as well as on hardware, except that GPU time only means something on hardware (doc 06 §11.5).

---

## 6. Rendering pipeline

```
 frame start ─ input → scene update (UI time / song time) ─────────────────────────────┐
                                                                                        ▼
 ┌─ layer 0  background ── shared menu background, or the song background + dim
 ├─ layer 1  playfields ── per player: lane board → receptors → holds → taps → mines → explosions (additive)
 ├─ layer 2  player HUD ── judgement, combo, error, life bar (per player, inside the title-safe area)
 ├─ layer 3  global HUD ── score, song info, progress
 ├─ layer 4  UI ───────── menus, panels, prompts (retained UI tree)
 ├─ layer 5  transition ─ fades, wipes between scenes
 └─ layer 6  debug ────── counters overlay
      every layer submits quads to one SpriteBatch in order; the batch flushes when texture, blend or TEV mode changes
 ─► GX_DrawDone → copy EFB→XFB → vsync (GRRLIB_Render for now)
```

- **Order is the only depth** (Z off), as in GRRLIB and ITGmania's note field (graphics doc §9).
- **Two blend modes cover nearly everything:** normal (premultiplied) and additive (`SRCALPHA, ONE`) for glows, explosions, flashes and particles.
- **Atlases per layer** keep batches long: one playfield atlas (arrows, receptors, hold parts, mine, glow, explosion), one font page per size, one UI atlas (panels, icons), plus the big single images (banners, backgrounds).
- **Render-to-texture** only for things that change rarely: a blurred copy of the selected song's banner (WiiFlow's TEV blur, [coverflow.cpp:768-901][wf-blur]) or a pre-composed song-info card, rebuilt on selection change **[src]**. No full-screen copies every frame.

---

## 7. The rhythm-game elements

### 7.1 Four- and five-panel receptors

- **Layout as data.** A `Playfield` (doc 06 §11.3: x, lane width, scale) plus a per-column table: x offset, rotation, atlas cell. Dance-single is four columns at −90°, 180°, 0°, 90° (L, D, U, R; [Gameplay.cpp:33](../game/source/screens/Gameplay.cpp#L33)) **[src]**. A five-panel layout (pump style: DL, UL, centre, UR, DR) is ±45°/±135° rotations of the same arrow plus a centre cell, so only the table changes. ITGmania does the same with per-column renderers and per-button noteskin elements ([`NoteField::InitColumnRenderers`][itg-colrend]) **[src]**.
- **The controller sets the panel count.** A GameCube dance mat reports four arrows as D-pad bits (doc 05, M3) **[measured]**, so five-panel play would need different hardware; keep the layout data-driven, but don't build 5-panel input now.
- **Per receptor per frame:** base sprite (beat flash through vertex colour, ITG's `diffuseramp` on the beat clock, [metrics.ini:70-72][ns-metrics]), a press overlay (additive, alpha 0.8 → 0.4 over 0.2 s decelerate; lift fades out and grows to 1.2, [metrics.ini:74-77][ns-metrics]) and the step "squash" (zoom 0.85 → 1 over 0.11 s, [metrics.ini:72][ns-metrics]) **[src]**. That's 2–3 quads per column.

### 7.2 Scrolling step charts

- **Positions are functions of the song beat** (doc 06 §3.2; [Gameplay.cpp:319-321](../game/source/screens/Gameplay.cpp#L319-L321)) **[src]**, culled to the visible range by a moving `first_visible` index ([Gameplay.cpp:357-367](../game/source/screens/Gameplay.cpp#L357-L367)) **[src]**.
- **One batch per player** for taps: all notes come from the playfield atlas with the same blend, so a player's taps can be one `GX_Begin` run **[exp]**. Today each note is a GRRLIB draw.
- **Quantisation colour** is a vertex colour on a white arrow (as now, `quantColour`), or a baked cell per quantisation if the art wants gradients; both are one quad.
- **Note shimmer on the beat**: ITGmania's default noteskin layers four extra quads per note with beat-driven `diffuseramp` ([Down Tap Note.lua:11-29][ns-tap]) **[src]**. A vertex-colour pulse driven by the beat clock gives a similar effect for no extra quads.
- **Speed mods**: x-mods exist; C-mods and M-mods follow ITGmania's [`ArrowEffects::GetYOffset`][itg-yoff] **[src]**. Reverse scroll mirrors y around the field centre.

### 7.3 Holds, rolls and mines

- **Hold bodies** are currently one stretched quad ([Gameplay.cpp:382-388](../game/source/screens/Gameplay.cpp#L382-L388)) **[src]**, so the texture smears with length. One quad with a repeating V coordinate on a power-of-two body texture keeps the pattern at any length, still one draw (§3 wrapping) **[exp]**. ITGmania's segmented strips ([NoteDisplay.cpp:756-1097][itg-hold]) are only needed for bending mods **[src]**.
- **Caps** at head and tail from the atlas; active/inactive/dead colours as now; rolls in amber; keep the sub-pixel guard (doc 06 §5).
- **Mines**: one atlas cell (red body, white core) with a slow spin; a hit plays the noteskin's additive rotate-and-fade (0 → 90 → 180° over 0.6 s, [metrics.ini:42][ns-metrics]) **[src]**.

### 7.4 Arrow hit animations

- **Explosion** = the glow cell, additive, tinted per grade, alpha held for 0.1 s then `decelerate` to 0 over 0.5 s, as the default noteskin's W1–W5 commands ([metrics.ini:44-48][ns-metrics]) **[src]**. A held hold ends with a flash that shrinks from 1 to 0.9 over 0.2 s then fades over 0.1 s ([metrics.ini:50][ns-metrics]) **[src]**.
- **Stateless evaluation:** each lane keeps the time and grade of its last hit (the `Player` struct already does: `lane_flash_us`, `lane_flash`, [Gameplay.h](../game/source/screens/Gameplay.h)) and the frame evaluates `alpha = track(now − hitTime)`. Nothing is allocated per hit.
- **Cost:** one or two large additive quads per lane; at 128 × 128 px, 4 lanes × 2 players ≈ 0.13 Mpx. Cheap.

### 7.5 Judgement graphics

- **A sheet of pre-rendered words** (Marvelous … Miss, with early/late variants, as Simply Love's 12- or 14-frame sheets, [Player judgment.lua:104-112][sl-judg]) **[src]**, drawn as one quad. Until real art exists, render the words into an atlas at startup (CLAUDE.md's "placeholders generated in code").
- **Motion:** Simply Love's `zoom 0.8 → decelerate 0.1 s → 0.75 → hold 0.6 s → accelerate 0.2 s → 0`, with an optional tilt proportional to the timing error (capped at 50 ms) ([Player judgment.lua:114-123][sl-judg]) **[src]**. That is one `Track`.
- **The ms-error readout** (already shown) uses the glyph atlas.

### 7.6 Combo counters

- **Digits from the glyph atlas**, one quad each, laid out from fixed-width digit metrics.
- **Motion:** a short scale pulse on each increment (design choice, about 1.08 → 1.0 over 0.08 s); colour by full-combo status with Simply Love's 0.8 s `diffuseshift` ([Player combo.lua:58-60][sl-combo]); a burst at 100 and 1000 (Simply Love has separate milestone actors, [Player combo.lua:38-40][sl-combo]) **[src]**.

### 7.7 Song selection carousel

- **Wheel model:** ITGmania's `WheelBase` offset (time-based spin after a 0.25 s hold, settle at `0.2 + |offset| / SwitchSeconds`, [WheelBase.cpp:149-253][itg-wheel]) **[src]**, with Simply Love's 17 items and `y = offset × screen_h / 15` ([metrics.ini:1529-1543][sl-wheel]) **[src]**. Only visible rows are drawn; each is a panel + title text + a small colour bar.
- **Banners:** an LRU cache with a byte budget (e.g. 32 × 50 KB RGB565 = 1.6 MB), prefetching 2–3 rows each side of the cursor (Snes9x GX's design, [GuiImageAsyncCache.h][s9-cache]) **[src]**. Decode on the worker (as `SongMedia` does), upload at most one per frame, show the title placeholder until it lands (already the case).
- **Optional banner carousel / cover flow:** five to seven banners on an arc (USB Loader GX, [gui_gamecarousel.cpp][ul-carousel]) or in perspective (WiiFlow's 45° projection, [coverflow.cpp:245-248][wf-cfinit]) **[src]**, driven by the same offset. A reflection is the same quad flipped with an alpha gradient in its vertex colours; WiiFlow's Z-plane trick ([`_drawMirrorZ`][wf-mirror]) is only needed in real 3D.

### 7.8 Animated song backgrounds

- **Gameplay:** the song's `#BACKGROUND`, decoded on the worker during the intro, at 640 × 480 RGB565 (600 KB) or 320 × 240 (150 KB, upscaled by bilinear filtering; soft, but it sits behind a dimmer) **[exp: judge the look on the TV]**. Dim by vertex colour (free). Add a slow pan/zoom over the song and an optional brightness pulse on the beat.
- **Menus:** the shared background: 3–4 UV-scrolling, palette-tinted layers (Simply Love's [Normal.lua][sl-bg] uses 10) **[src]**, a gradient, and a sparse sprite pool (HBC's [bubbles.c][hbc-bubbles]) **[src]**.
- **Not now:** background videos and `#BGCHANGES` (doc 06 §1). WiiFlow's THP player (`source/gui/gcvid.cpp`, `WiiMovie.cpp`) is the reference if that changes **[src]**.

### 7.9 Results screen

- **Static content, built once** on entry: grade (large pre-rendered text), judgement counts, held/let-go, max combo, mean and spread of the error (all already computed), a timing histogram (bars), and a life graph (line strip from life samples).
- **Animation:** panels slide in staggered, the percentage counts up over about 1 s with a decelerate curve, the grade lands with a scale-down and a flash.
- **Cost:** a few hundred quads; histogram and graph geometry can be recorded once into a display list (libgui's mesh-caching approach, [OgcVideoDriver.cpp:318-435][lg-dlist]) **[src]**.

### 7.10 Gameplay visual effects

| Effect | Mechanism | Cost |
|---|---|---|
| Receptor beat flash | Vertex colour from the beat clock (ITG `diffuseramp`) | 0 extra quads |
| Lane press flash | Exists (150 ms glow) | 1 quad per lane |
| Column flash on miss | Simply Love has one (`BGAnimations/ScreenGameplay underlay/PerPlayer/NoteField/ColumnFlashOnMiss.lua`) **[src: file present]**: a red lane-tall quad fading out | 1 quad |
| Combo break | Short shake of the combo text (offset tween) | 0 |
| Milestone / full combo | Particle burst (pool, additive) + a brief full-screen additive flash | ≤ 200 small quads + 1 full-screen quad for a few frames |
| Danger | Life bar pulses when low (Periodic on UI time) | 0 |
| Failed | Greyed playfield (exists) | 1 quad |

---

## 8. Multiplayer and the safe area

- Doc 06 §11.3 already fixes the layouts: 64 px lanes for 1–2 players, 32 px (scale 0.5) for 3–4, playfields inside the action-safe area (x 32–608, y 24–456), HUD inside title-safe (x 64–576, y 48–432).
- Per-player costs scale linearly: in 4P the note and explosion quads quadruple while the shared layers don't. M2b's 4P scene stayed under 3 ms total on hardware **[measured]**, so the budget in §5 is per frame, not per player.
- Each player's HUD (judgement, combo, error) is drawn inside its own column; the batch keeps all players' quads of one kind together when the order allows.

---

## 9. Aspect ratio and video modes

- **16:9.** The Wii reports the user's choice through `CONF_GetAspectRatio()`. libgui widens `viWidth` to 678 px in 16:9 ([OgcVideoDriver.cpp:41-58][lg-vinit]) **[src]**; HBC lays out on an 852-px-wide virtual canvas (`WIDESCREEN_RATIO`, [gfx.h:8][hbc-gfxh]) and snaps A8 text to the pixel grid in that mode ([gfx.c:365-372][hbc-tex]) **[src]**. Doc 06 §11.3 already lists an 854-px logical canvas as the option. Recommendation: lay out in logical units (640 × 480, or 854 × 480 in 16:9) and map them to the 640-wide EFB with the projection matrix, so circles stay round on a 16:9 TV.
- **PAL50.** The EFB can be up to 528 lines (doc 05 M2 ran "PAL50 528i" in Dolphin) **[src: gx.h, doc 05]**. The logical 480-line canvas should be scaled to the mode's EFB height by the projection, not by each screen.
- **480p vs 480i**: see §4 rule 5.

---

## 10. Measurement plan

Before and after each rendering change, on Dolphin (logic, counters) and then the real Wii (timing):

1. **Batched vs GRRLIB ramp:** a mode in `RenderTest` that draws the same 0–4,000 atlased quads through GRRLIB and through the batch; record CPU busy and max frame time per step.
2. **Text stress:** 0–2,000 glyphs per frame from the atlas.
3. **Fill:** 0–10 full-screen blended layers; record GPU wait vs drawn area, to replace the 0.5–0.95 Mpx/ms estimate in §5 with a number.
4. **Worst-case gameplay:** the M2b 4P scene with the new art, explosions, particles and the song background; record *max* frame time over a full song.

Each hardware run is a short checklist for the owner, like the existing ones in doc 05: boot the test build over wiiload, read the overlay at each step, report the numbers (or a photo).

---

## References

[wb-hw]: https://wiibrew.org/wiki/Hardware
[cop-wii]: https://www.copetti.org/writings/consoles/wii/
[cop-gc]: https://www.copetti.org/writings/consoles/gamecube/

[ogc-pixfmt]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3475-L3490
[ogc-inittex]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L4250-L4272
[ogc-fmts]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L291-L301
[ogc-sbrk]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/libogc/sbrk.c
[ogc-tmem]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L4221-L4520
[ogc-copyfilter]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3669
[ogc-drawdone]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L1566-L1679

[gr-render]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_render.c#L407-L424
[gr-empty]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L172-L208
[gr-raw]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L136-L170
[gr-png]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L263-L298

[lg-loadtex]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L183-L217
[lg-drawtex]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L225-L273
[lg-dlist]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L318-L435
[lg-vinit]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L29-L95

[wf-cmpr]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp#L89-L187
[wf-tex]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp
[wf-lod]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L1340-L1350
[wf-blur]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L768-L901
[wf-cfinit]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L230-L249
[wf-mirror]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L704-L726

[s9-cache]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/widgets/GuiImageAsyncCache.h
[s9-cachecfg]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L1242-L1246
[s9-video]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/drivers/ogc/OgcEmulatorVideo.cpp#L763-L929

[hbc-gfxh]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.h#L8
[hbc-tex]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L360-L434
[hbc-bubbles]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/bubbles.c#L36-L224
[ul-carousel]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_gamecarousel.cpp

[itg-colrend]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteField.cpp#L337-L369
[itg-yoff]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ArrowEffects.cpp#L540-L727
[itg-hold]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteDisplay.cpp#L756-L1097
[itg-wheel]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L149-L253
[ns-metrics]: https://github.com/itgmania/itgmania/blob/v1.3.0/NoteSkins/dance/default/metrics.ini#L39-L77
[ns-tap]: https://github.com/itgmania/itgmania/blob/v1.3.0/NoteSkins/dance/default/Down%20Tap%20Note.lua#L11-L29

[sl-judg]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/Graphics/Player%20judgment.lua#L104-L123
[sl-combo]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/Graphics/Player%20combo.lua#L38-L60
[sl-wheel]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/metrics.ini#L1529-L1548
[sl-bg]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/BGAnimations/_shared%20background/Normal.lua#L1-L55
