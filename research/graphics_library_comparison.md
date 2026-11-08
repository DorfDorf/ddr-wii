# Graphics library comparison: GRRLIB, libwiisprite and direct GX

*Part of the five research documents (start with [recommended_implementation_plan.md](recommended_implementation_plan.md)). Versions and pinned commits are listed in [gui_framework_comparison.md §1](gui_framework_comparison.md#1-what-was-reviewed). Markers: **[src]** read in source, **[doc]** documentation, **[mem]** from memory (verify), **[exp]** still to be tested, **[measured]** this project's own runs on the real Wii (doc 05, M2 and M2b).*

---

## 1. Summary

| | GRRLIB 4.6.1 | libwiisprite 0.3.0f | Direct GX (libogc 3.2.1) |
|---|---|---|---|
| What it is | C helper library: video setup, textured quads, primitives, fonts, 3D helpers, render-to-texture | C++ sprite/layer library (J2ME style) | The SDK itself: every GX register write is yours |
| Per-sprite CPU cost | Highest of the three: texture object rebuilt, TEV/vertex format set and reset, copy filter written, matrix built with trig, all per call (§4) | Lower: texture binding cached, but a matrix per sprite | Lowest: one texture load per batch, positions written straight into one `GX_Begin` (devkitPro's `gxSprites` example) |
| Text | Tile-set fonts and a per-pixel TTF printer that is unusable per frame (§7) | FreeType helper in this fork | Whatever you build (a glyph atlas) |
| Texture formats | Any format via `GRRLIB_CreateEmptyTextureFmt` and TPL; the PNG/JPEG loaders produce RGBA8 only | RGBA8 only, sides must be multiples of 4 | All 11 formats, mipmaps, palettes, TMEM control |
| Advanced GX | Blend presets, render-to-texture, basic 3D | Additive flag, mirror flag | TEV (16 stages), 8 texture maps, indirect textures, display lists, fog, async draw-done |
| Maintenance | Active (last commit 2026-10-12) | Fork last touched 2024-05 | Maintained with devkitPro |
| Fit with ddr-wii | Already linked and working, measured on hardware | Nothing to gain over GRRLIB | Full control; more code to own |

**Verdict:** don't switch libraries now and don't adopt libwiisprite. Keep GRRLIB for what it does well (video init and present, image utilities, debug drawing) behind ddr-wii's own `render::` API, and add a small **direct-GX batched sprite path** inside that API for the high-count draws (glyphs, notes, particles). The evidence and the reasoning are in §12–§13.

---

## 2. Frame lifecycle and video setup

| Library | Init | Present | Notes |
|---|---|---|---|
| GRRLIB | [`GRRLIB_Init`][gr-init]: 256 KB FIFO, two XFBs, `GX_PF_RGB8_Z24` (or `RGB565_Z16` when the video mode is antialiased), copy filter from the mode, gamma 1.0, one vertex format (pos f32 xyz, colour RGBA8, tex f32), ortho projection, alpha blending, no culling **[src]** | [`GRRLIB_Render`][gr-render]: `GX_DrawDone()` → `GX_InvalidateTexAll()` → flip → `GX_CopyDisp` → `VIDEO_SetNextFramebuffer` → `VIDEO_Flush` → `VIDEO_WaitVSync` (a second wait only in 240p `VI_NON_INTERLACE` modes) **[src]** | The CPU waits for the GPU, then for vsync: no CPU/GPU overlap across frames. ddr-wii's [`WiiVideo::endFrame`](../game/source/platform/wii/WiiVideo.cpp#L44-L82) adds its own `GX_DrawDone` first to time the GPU |
| libwiisprite | [`GameWindow::InitVideo`][ws-gw] | [`GameWindow::Flush`][ws-flush]: `GX_CopyDisp` → `GX_DrawDone` → next framebuffer → vsync → flip → `GX_InvalidateTexAll` **[src]** | Same serial shape |
| libgui (Ogc driver) | [`OgcVideoDriver::init`][lg-vinit]: preferred mode with `viWidth` 672 (678 for 16:9) and re-centred origin, two XFBs, 256 KB FIFO **[src]** | [`render`][lg-render]: `GX_CopyDisp` → `GX_DrawDone` → free retired display lists → `VIDEO_Flush` → `VIDEO_WaitForFlush` **[src]** | Builds against libogc2's API |
| HBC | [`gfx_init`][hbc-init]: `RGB8_Z24`, viewport +0.05 px "to align textures on the pixel grid", ortho with z 100..1000 so z can order layers **[src]** | [`gfx_frame_end`][hbc-end]: `GX_DrawDone` → `VIDEO_WaitVSync` → `GX_CopyDisp(xfb)` → `GX_Flush` with **one** XFB **[src]** | Copying right after vsync saves an XFB (≈600 KB at 640×480) |
| StepMania-Wii | `RageDisplay_GX::EndFrame` (`trunk/RageDisplay_GX.cpp:221-239`) **[src]** | Copy → `GX_DrawDone` → vsync | Same serial shape |

**What libogc offers beyond this** **[src: [gx.h][ogc-drawdone]]**: `GX_SetDrawDone()` plus `GX_SetDrawDoneCallback()` signal GPU completion by interrupt instead of stalling, and `GX_SetDrawSync(token)` marks when the GPU has finished with a buffer. With those, the CPU can build frame N+1 while the GPU draws frame N. **ddr-wii doesn't need this yet:** the worst 4-player scene used 1.58 ms CPU + about 1 ms GPU wait of a 16.7 ms frame on hardware (doc 05, M2b). It is the lever to pull if a future scene gets heavy.

---

## 3. EFB formats and antialiasing

- `GX_PF_RGB8_Z24` and `GX_PF_RGBA6_Z24` give a 640 × 528 EFB; `GX_PF_RGB565_Z16` turns on 3-sample multisampling and halves the EFB to 640 × 264 lines **[src: [gx.h:3476-3486][ogc-pixfmt]]**. So hardware AA at 480 lines means drawing the frame in two halves. None of the surveyed UIs does that.
- **WiiFlow supersamples instead:** it renders the cover-flow scene 2–8 times with sub-pixel viewport jitter, copies each pass to an RGBA8 texture in MEM2 (1.2 MB each at 640×480) and blends them in one TEV pass with constant-colour weights ([`prepareAAPass`][wf-aapass], [`renderAAPass`][wf-aarender], [`drawAAScene`][wf-aascene]) **[src]**. That costs N full scene renders: acceptable for a slow menu, not for gameplay.
- **libgui avoids AA passes altogether** by drawing shapes as meshes with a feathered alpha fringe ([GuiGeometry.cpp][lg-geo]) **[src]**. For text and sprites, bilinear filtering of art with soft edges does the same job.
- **Render-to-texture with alpha** needs `GX_PF_RGBA6_Z24` (6 bits per channel, so gradients band slightly): GRRLIB switches to it in [`GRRLIB_CompoStart`][gr-compo] and back in `CompoEnd` **[src]**.
- **Deflicker.** The copy filter's vertical taps (`GX_SetCopyFilter`, [gx.h:3669][ogc-copyfilter]) soften 480i flicker by blurring vertically **[src]**. GRRLIB sets it from the video mode. WiiLÖVE exposes it as a toggle (`setDeflicker`, [graphics.cpp:269][wl-gfx]) **[src]**. In 480p it only blurs, so a "sharp text" option should turn it off there **[exp]**.

---

## 4. 2D textured quads: what each library does per sprite

| Per-sprite work | GRRLIB [`DrawImg`][gr-drawimg] | libwiisprite [`Sprite::Draw`][ws-draw] | libgui [`drawTexture`][lg-drawtex] | HBC [`plot_texture`][hbc-tex] | SDL2 Wii [`OGC_RenderGeometry`][sdl-geom] | devkitPro `gxSprites` |
|---|---|---|---|---|---|---|
| `GX_InitTexObj` | Every call (+ `GX_InitTexObjLOD` when filtering is off) | Only when the image changes ([`Image::BindTexture`][ws-bind] caches the last one) | Every call | Never (each entity keeps a built `GXTexObj`) | Per texture | Once |
| `GX_LoadTexObj` | Every call | On change | Every call | Every call | Per batch | Once |
| `GX_InvalidateTexAll` | Once per frame | Once per frame | **Every call** | Once per frame | When a texture is updated | Once per frame |
| TEV / vertex-format changes | Set to MODULATE + TEX0 before, reset to PASSCLR + no TEX0 after ([render.c:103-104][gr-drawimg]) | None | Same set/reset | TEV per entity | Per batch | None |
| Copy filter | **`GX_SetCopyFilter` on every call** ([render.c:52-59][gr-drawimg]) | — | — | — | — | — |
| Matrix | `guMtxIdentity`, `ScaleApply`, `RotAxisDeg`, `Concat`, `TransApply` with 4 sin/cos for the handle, `Concat`, `GX_LoadPosMtxImm`, then restore | 3 matrix ops + load | 4 ops + load + restore | 3–5 ops + load | None (positions computed on the CPU by SDL's queue) | None (screen-space positions, one model-view for all) |
| Vertices | 4 × (pos f32×3, colour u32, tex f32×2) | 4 × (pos f32×2, colour, tex) | 4 | 4 | 6 per quad (two triangles), whole batch in one `GX_Begin` | 4 × (pos f32×2, tex f32×2) |
| Off-screen culling | No | No | No | **Yes** ([gfx.c:373-386][hbc-tex]) | No | No |

GRRLIB's untextured `GRRLIB_Rectangle` is cheap by comparison: a bare `GX_QUADS` with no matrix or texture setup ([GRRLIB_fbSimple.h:82-114][gr-rect]) **[src]**.

**Measured cost in this project.** On the real Wii, the M2 render test drew 1,600 GRRLIB sprites with 4.20 ms of CPU per frame on average (4.21 ms max), and the M2b 4-player stress scene drew 483 sprites with 82 texture switches in 1.58 ms (doc 05) **[measured]**. That is about **2.6 µs per GRRLIB sprite**, scene logic included. Today's gameplay draws a few hundred, so GRRLIB is not a bottleneck **for the current look**.

**Where it would become one.** A richer UI multiplies draws: every glyph of text is a draw (combo, score, judgement offset, song titles, wheel rows), particles add hundreds, panels add several each. At 2.6 µs, 3,000 draws would take about 8 ms of the 16.7 ms frame **[exp: extrapolated]**. A batched path writes only the vertices for each quad and changes texture state once per batch; devkitPro's `gxSprites` example draws 1,024 sprites this way (one TPL texture loaded once, one model-view matrix, four `GX_Position2f32`/`GX_TexCoord2f32` pairs per sprite) **[src: `C:\devkitPro\examples\wii\graphics\gx\gxSprites\source\gxsprites.c`]**. Its per-quad cost should be several times lower than GRRLIB's **[exp: measure with the M2 ramp]**.

**Atlases come first.** Batching only helps if consecutive quads share a texture. ddr-wii's arrows, receptors, hold parts and glows are four separate textures today ([ArrowArt.h](../game/source/render/ArrowArt.h)), so one atlas for the playfield art (doc 06 §5 already plans one) and one per font page are the prerequisite.

---

## 5. Scaling, rotation and pivots

- **GRRLIB** rotates and scales around a *handle* (`GRRLIB_SetHandle` / `GRRLIB_SetMidHandle`, [GRRLIB_handle.h][gr-handle]) and recomputes the handle offset with sin/cos every call **[src]**. ddr-wii's arrow textures use the mid handle (doc 06 §11.3 correction note).
- **libwiisprite** has a reference pixel (`SetRefPixelPosition`, `SetRefPixelPositioning`) plus zoom and stretch ([sprite.cpp:90-153][ws-sprite]) **[src]**.
- **WiiLÖVE** gives LÖVE's `draw(x, y, r, sx, sy, ox, oy)` with an origin, on a push/pop transform stack ([graphics.cpp:53-108][wl-gfx]) **[src]**.
- **Direct GX batch:** compute the four corners on the CPU. A note lane has one of four fixed rotations (−90°, 180°, 0°, 90°, [Gameplay.cpp:33](../game/source/screens/Gameplay.cpp#L33)), so the corner offsets can be precomputed per lane and need no trig per note. Free rotation costs one sin/cos pair per sprite.
- **Hierarchies:** HBC's origin stack ([`gfx_frame_push`][hbc-push]) and Wii64's transparency/depth stacks ([GraphicsGX.cpp:479-577][w64-gfx]) show the two things a UI tree needs to pass down: an offset (or full 2×3 transform) and a multiplied alpha **[src]**.

---

## 6. Alpha blending and transparency

| Mode | GRRLIB [`GRRLIB_SetBlend`][gr-blend] | libwiisprite | SDL2 Wii [`set_blend_mode_real`][sdl-blend] | StepMania-Wii (`RageDisplay_GX.cpp:257-271`) |
|---|---|---|---|---|
| Normal | `SRCALPHA, INVSRCALPHA` | same | same | same |
| Additive | `SRCALPHA, DSTALPHA` | `SRCALPHA, ONE` ([sprite.cpp:384-386][ws-draw]) | `SRCALPHA, ONE` | `SRCALPHA, ONE` |
| Others | SCREEN (`SRCCLR, DSTALPHA`), **MULTI = `GX_BM_SUBTRACT`**, INV | — | MOD, MUL | — |

- GRRLIB's ADD reads the destination alpha. Its EFB format `RGB8_Z24` stores no alpha, so this should behave as `ONE` **[mem]**, but `SRCALPHA, ONE` (what everyone else uses) says what it means **[exp: check both on hardware]**. GRRLIB's "MULTI" is a subtractive blend, not a multiply **[src]**.
- **Premultiplied alpha** (blend `ONE, INVSRCALPHA`) removes the dark fringes that bilinear filtering produces around straight-alpha art when it is scaled, and lets one atlas hold both normal and additive art. None of the surveyed libraries uses it **[src]**; it is worth adopting for the UI atlases **[mem/exp]**.
- **Alpha compare** (`GX_SetAlphaCompare`, [gx.h:3106][ogc-alphacmp]) discards fully transparent texels **[src]**. HBC combines it implicitly with z-ordering; ddr-wii's 2D passes should keep Z off and rely on draw order.
- **Tinting.** `GX_MODULATE` multiplies texture by vertex colour (white art tinted per quantisation, as ddr-wii does now). Wii64 tints a greyscale background with a TEV register colour ([`Gui::drawBackground`][w64-bg]) **[src]**, and HBC tints A8 glyph textures with the raster colour through a custom TEV setup ([gfx.c:409-416][hbc-tex]) **[src]**. A "flash to white" (receptor hit) needs a TEV stage that lerps the texture colour toward a constant; modulate alone can only darken **[src: TEV colour inputs in gx.h]**.

---

## 7. Font rendering

| Implementation | How it works | Per-frame cost | Verdict |
|---|---|---|---|
| GRRLIB [`GRRLIB_PrintfTTF`][gr-ttf] | Re-rasterises every glyph with FreeType on **every call**, then [`DrawBitmap`][gr-ttfbmp] emits **one `GX_POINTS` primitive per pixel** **[src]** | A 20-character 24 px string is several thousand point primitives (one per pixel of every glyph's bitmap) plus 20 FreeType renders | **Never per frame** (doc 06 §11.5 already says so) |
| GRRLIB [`GRRLIB_Printf`][gr-printf] / BMF / [`GRRLIB_PrintfSystemFont`][gr-sysfont] | Tile-set font (one `DrawTile` per character), ByteMap fonts, or the console's IPL ROM font | One GRRLIB draw per glyph | Fine for debug text (what ddr-wii does) |
| FreeTypeGX (WiiFlow [FreeTypeGX.cpp][wf-ftgx], USB Loader GX, WiiLÖVE) | Glyphs cached per pixel size, **each glyph its own `GX_TF_I4` texture**; `drawText` does `GX_InitTexObj` + load per glyph **[src]** | One texture switch per glyph | Adapt the caching, not the one-texture-per-glyph layout |
| libgui [`GuiTextRenderer::cacheGlyphData`][lg-glyph] + [`OgcGlyphRenderer`][lg-glyphr] | Same model, cached per size in a map with a 256-entry fast table, I4 textures tiled 8×8, `GX_Position2s16` quads **[src]** | One switch per glyph | Same |
| HBC [`font_get_char`][hbc-font] | Glyph cache hashed by codepoint (grows on demand), A8 textures tinted by TEV, kerning, wrapping **[src]** | One switch per glyph | Same |
| ddr-wii [`render::Font`](../game/source/render/Font.cpp) | libogc's 8×16 console font in a 128×256 RGBA8 tile set | One GRRLIB `DrawTile` per glyph | Keep for debug overlays |

**Recommendation:** a **glyph atlas**. Rasterise each font size with FreeType (already linked through GRRLIB's `pkg-config freetype2`) into one `GX_TF_I8` or `I4` page, prewarm ASCII and Latin-1 at load, add other codepoints on demand (song titles can be Japanese; the transliterated title is the fallback), and draw every glyph of a frame through the sprite batch. A 512 × 512 page is 256 KB in I8 or 128 KB in I4 **[src: format sizes]**. Soft shadows are the same quads drawn once more, offset and dark. Glyph rasterisation must stay off the frame path (prewarm, or rasterise on the worker thread).

---

## 8. Image loading and caching

| Implementation | Formats out | Constraints and behaviour | Verdict |
|---|---|---|---|
| GRRLIB [`GRRLIB_LoadTexturePNG`][gr-png] (PNGU) | RGBA8 | If PNGU had to round a side to a multiple of 4, GRRLIB **blanks the texture** ([texEdit.c:285-289][gr-png]) **[src]** (doc 06 §7 found the same and works around it) | Don't use for SD-card art |
| GRRLIB [`GRRLIB_LoadTextureTPL`][gr-tpl] | Any GX format | Zero-copy: points into the TPL buffer; no mipmap or palette handling **[src]** | **Use** for shipped art |
| GRRLIB [`GRRLIB_CreateEmptyTextureFmt`][gr-empty] | Any GX format, 32-byte aligned | — **[src]** | Use |
| libwiisprite [`Image::_InitializeImage`][ws-image] | RGBA8 | Refuses sides that aren't multiples of 4 **[src]** | — |
| WiiFlow [`STexture::fromPNG`][wf-png], [`fromJPG`][wf-jpg], `fromTHP` | RGBA8, RGB565 or **CMPR**, with **mipmaps** | Software CMPR encoder ([`_convertToCMPR`][wf-cmpr]), box-filter resize, all buffers from `MEM2_alloc`; sides truncated to multiples of 4 (8 for CMPR) **[src]** | **Adapt** if CMPR is wanted at runtime |
| libgui [`GuiImageData::decodeToRgba`][lg-imgdata] | RGBA8 | CPU-only decode into an app-sized scratch buffer, upload separately; `loadClipped()` masks pictures to rounded corners **[src]** | Adapt the split (decode on a worker, upload on the main thread) |
| Async caches | Snes9x GX [`GuiImageAsyncCache`][s9-cache] (LRU, prefetch, pin), USB Loader GX [`GuiImageAsync`][ul-async] (one loader thread, placeholder until loaded), WiiFlow [`_coverLoader`][wf-loader] (window around the centre) **[src]** | — | **Adapt** (see the GUI doc §3.6) |
| ddr-wii [`render::Banner`](../game/source/render/Banner.h) | RGBA8 256×100 | libpng/libjpeg, box filter, refuses images over 4096 px a side or 2 Mpx **[src]** | Keep; consider RGB565 (§ wii_graphics_architecture) |

**Build-time conversion is already in the toolchain.** devkitPPC's `wii_rules` has a `%.tpl : %.scf` rule that runs **gxtexconv**, which converts PNGs to any GX format (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CI4, CI8, CMPR) with optional mipmaps **[src: `C:\devkitPro\devkitPPC\wii_rules:23-25`, `gxtexconv` v1.0.6 usage text]**. ddr-wii's Makefile would need a rule to embed the resulting `.tpl` (CLAUDE.md's Assets section). That gives shipped art in the right format at zero load-time cost.

---

## 9. Layering and draw order

- **Submission order** decides what is on top in GRRLIB, libgui and the direct path (painter's algorithm, Z off) **[src]**.
- **libwiisprite's `LayerManager`** draws from the last index to the first, so index 0 is on top, inside a scissor box with a hard-coded −31 px offset ([layermanager.cpp:94-109][ws-lm]) **[src]**.
- **HBC** puts a z value on every entity and keeps the Z test on, so overlapping widgets order by z as well as by queue order ([gfx.c:112-174][hbc-init], [gfx.c:532-536][hbc-end]) **[src]**. With alpha-blended art this needs care (transparent texels still write Z unless alpha compare discards them).
- **ITGmania's note field** fixes the order per pass: board, receptors, beat bars, holds then taps per column, explosions last ([`NoteField::DrawPrimitives`][itg-nf]) **[src]**.
- **Scissor** clips lists and wheels: HBC `GFXQ_SCISSOR` ([gfx.h][hbc-gfxh]), libwiisprite's view window, GRRLIB's `GRRLIB_ClipDrawing` **[src]**.
- **Recommendation:** fixed layers per frame (background, playfield, HUD, UI, overlay, transition), submission order within a layer, Z off, scissor for scrolling regions. A batcher may merge only *consecutive* quads with the same texture and blend mode, so order is never changed.

---

## 10. 3D rendering

- **GRRLIB** has a 3D mode with a camera, object-view helpers, immediate primitives (cube, sphere, torus …) and four light types ([GRRLIB_3D.c][gr-3d]) **[src]**.
- **WiiFlow's cover flow** is real 3D: `guPerspective` at 45° ([coverflow.cpp:245-248][wf-cfinit]), covers as quads or box meshes, a Z-only floor plane so mirrored covers are clipped ([`_drawMirrorZ`][wf-mirror]), and mipmapped textures with LOD bias and anisotropy ([coverflow.cpp:1344][wf-lod]) **[src]**.
- **Wii64** draws a rotating 3D logo and box inside its 2D menu (`Logo.cpp`, `Box3D.cpp`) **[src]**.
- **Simply Love** has 3D arrow, ring and fragment models for its backgrounds (`BGAnimations/_shared background/arrow_model.txt`, `ring_model.txt`) **[src]**.
- **Use in ddr-wii:** optional. A tilted banner carousel or a few slowly spinning arrow models in a menu background cost little (tens of vertices, perspective and lighting are fixed-function). Gameplay stays 2D. 3D textures that shrink on screen need mipmaps (WiiFlow) to avoid shimmering.

---

## 11. GX features the libraries hide

| Feature | Where it's used | What ddr-wii could do with it |
|---|---|---|
| Multi-stage TEV with constant colours | WiiFlow blur (up to 7 stages, weights in KONST colours, [coverflow.cpp:768-901][wf-blur]); WiiFlow AA resolve ([video.cpp:389-460][wf-aascene]); Snes9x GX scanlines (2 stages, [setupScanlineFilterTEV][s9-tev]) **[src]** | Flash-to-white on hit, colour-tinted greyscale art, blurred banner backdrops, glows without extra textures |
| Indirect textures | `GX_SetTevIndirect`, `GX_SetIndTexOrder` ([gx.h:3217-3251][ogc-ind]) **[src]**; none of the surveyed UIs uses them | Heat-haze or ripple effects on a background, once the basics are done |
| Display lists | libgui records cached meshes with `GX_BeginDispList` and replays them with `GX_CallDispList` under a position matrix, freeing them only after `GX_DrawDone` ([OgcVideoDriver.cpp:318-435][lg-dlist]) **[src]**. Rules: 32-byte aligned, multiple of 32, `DCInvalidateRange` first, ≥63 bytes slack ([gx.h:2040-2110][ogc-dlist]) **[src]** | Static panels, results-screen graphs |
| Render to texture | GRRLIB `GRRLIB_Screen2Texture` ([snapshot.c:32-45][gr-compo]); WiiFlow [`renderToTexture`][wf-rtt] **[src]** | Bake a complex panel once (song info card), blur a banner once |
| Streaming textures | Snes9x GX uploads the emulator frame every frame as RGB5A3: convert, `DCStoreRange`, `GX_InvalidateTexAll` once ([OgcEmulatorVideo.cpp:763-929][s9-video]) **[src]** | Procedural or video backgrounds later |
| TMEM preloading | `GX_InitTexPreloadRegion`, `GX_PreloadEntireTexture`, `GX_LoadTexObjPreloaded` ([gx.h:4246-4520][ogc-tmem]) **[src]** | Not needed at this scale |
| Async GPU completion | `GX_SetDrawDone` + callback, `GX_SetDrawSync` (§2) **[src]** | CPU/GPU overlap if a scene gets heavy |

---

## 12. Comparison matrix (with the extra candidates)

| Criterion | GRRLIB | libwiisprite | Direct GX | libgui driver | SDL2 for Wii | OpenGX |
|---|---|---|---|---|---|---|
| Level | Helper functions | Sprite objects | Registers | GUI backend | Portable 2D API | OpenGL 1.x subset |
| Per-quad CPU | High (§4); measured 2.6 µs | Medium | Low (batched) | High (+ texture-cache flush per draw) | Medium (queue copy, batched triangles) | Medium–high (GL state emulation) |
| Texture formats | All (via TPL / empty textures) | RGBA8 | All + mipmaps + palettes | RGBA8 (images), I4 (glyphs) | RGBA8, RGB565, CI8 ([SDL_render_ogc.c:182, 252][sdl-tex]) | Auto-compression, mipmaps ([README][ogx]) |
| Fonts | Debug only for per-frame use | FreeType helper | Build it | Glyph cache | SDL_ttf (surfaces → textures) | — |
| TEV / indirect / display lists | No / no / no | No | Yes | Display lists only | No | Partial (fixed-function GL) |
| Render to texture | Yes | No | Yes | No | Yes ([`OGC_SetRenderTarget`][sdl-rt]) | Yes |
| Input/audio coupling | None | None | None | Owns input and audio | Owns events, audio, threads | None |
| Toolchain | devkitPro libogc (installed) | devkitPro | devkitPro | libogc2 | devkitPro | devkitPro |
| Last change | 2026-10 | 2024-05 | 2026-11 | 2026-11 | 2026-04 | 2026-04 |

**Why not the extras.** libgui's driver is coupled to its own platform layer and libogc2, and flushes the texture cache per draw. SDL2 would give PC builds of the UI, but hides TEV, display lists and the compact texture formats, and brings its own event and audio loop next to `wii::Input` and ASND. OpenGX exists to port OpenGL code; ddr-wii has none.

---

## 13. Recommendation, with the evidence

1. **GRRLIB stays for now.** It is linked, its init/present path holds 60 Hz with 75% of the frame to spare on hardware at 1,600 sprites (doc 05 M2), and its `CreateEmptyTextureFmt`, `LoadTextureTPL` and `Screen2Texture` cover most texture plumbing **[src + measured]**.
2. **All drawing goes through `render::`**, which ddr-wii already has ([Draw.h](../game/source/render/Draw.h)). That seam makes the backend swappable without touching screens.
3. **Add a direct-GX `SpriteBatch` inside `render::`** for atlased quads: per-batch texture and blend state, CPU-computed corners, one `GX_Begin` per run of compatible quads, and a cache of the last bound texture (libwiisprite's [`BindTexture`][ws-bind] idea). Text, notes, explosions and particles go through it. Leave GRRLIB's GX state as it found it (vertex descriptors, TEV, matrix) so both paths can coexist during the switch **[exp]**.
4. **Never use** `GRRLIB_PrintfTTF` per frame, GRRLIB's PNG loader for SD content, or `GX_InvalidateTexAll` per draw **[src]**.
5. **Ship art as TPL** through gxtexconv (I4/IA4/IA8 for glyphs and greyscale effects, RGB5A3 for UI art with alpha, CMPR for large opaque images), and decode SD-card banners and backgrounds on a worker thread **[src: toolchain]**.
6. **Measure before replacing more.** The M2/M2b counters (draws, texture switches, area, CPU and GPU time) already exist ([Draw.h](../game/source/render/Draw.h), [WiiVideo.h](../game/source/platform/wii/WiiVideo.h)); add a "batched" mode to the M2 ramp and compare on hardware before moving anything else off GRRLIB **[exp]**. Replacing `GRRLIB_Init`/`GRRLIB_Render` with ddr-wii's own `WiiVideo` code is a later, optional step (it would allow async draw-done and a single XFB like HBC).

---

## References

[gr-init]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_core.c#L54-L225
[gr-render]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_render.c#L407-L424
[gr-drawimg]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_render.c#L42-L105
[gr-rect]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/grrlib/GRRLIB_fbSimple.h#L82-L114
[gr-handle]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/grrlib/GRRLIB_handle.h#L36-L66
[gr-blend]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/grrlib/GRRLIB_settings.h
[gr-ttf]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_ttf.c#L98-L170
[gr-ttfbmp]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_ttf.c#L177-L198
[gr-printf]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_print.c#L39-L96
[gr-sysfont]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_systemFont.c#L141-L156
[gr-png]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L263-L298
[gr-tpl]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L210-L244
[gr-empty]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_texEdit.c#L172-L208
[gr-compo]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_snapshot.c#L32-L72
[gr-3d]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_3D.c

[ws-gw]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/gamewindow.cpp#L31-L147
[ws-flush]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/gamewindow.cpp#L172-L188
[ws-draw]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/sprite.cpp#L379-L445
[ws-sprite]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/sprite.cpp#L90-L153
[ws-bind]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/image.cpp#L179-L195
[ws-image]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/image.cpp#L197-L205
[ws-lm]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/layermanager.cpp#L94-L109

[lg-vinit]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L29-L95
[lg-render]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L107-L119
[lg-drawtex]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L225-L273
[lg-dlist]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L318-L435
[lg-geo]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiGeometry.cpp#L495-L590
[lg-glyph]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiTextRenderer.cpp#L113-L172
[lg-glyphr]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcGlyphRenderer.cpp#L26-L155
[lg-imgdata]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiImageData.h#L64-L92

[hbc-init]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L112-L174
[hbc-end]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L506-L538
[hbc-tex]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L360-L434
[hbc-push]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L456-L504
[hbc-gfxh]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.h
[hbc-font]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/font.c#L217-L326

[wf-aapass]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/video.cpp#L324-L362
[wf-aarender]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/video.cpp#L364-L387
[wf-aascene]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/video.cpp#L389-L460
[wf-rtt]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/video.cpp#L304-L322
[wf-ftgx]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/FreeTypeGX.cpp#L104-L317
[wf-png]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp#L383-L548
[wf-jpg]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp#L266-L381
[wf-cmpr]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp#L89-L187
[wf-cfinit]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L230-L249
[wf-mirror]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L704-L726
[wf-lod]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L1340-L1350
[wf-blur]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L768-L901
[wf-loader]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2956-L3016

[s9-cache]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/widgets/GuiImageAsyncCache.h
[s9-tev]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/drivers/ogc/OgcEmulatorVideo.cpp#L290-L349
[s9-video]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/drivers/ogc/OgcEmulatorVideo.cpp#L763-L929
[ul-async]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_image_async.h
[w64-gfx]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/GraphicsGX.cpp#L479-L577
[w64-bg]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/Gui.cpp#L159-L175
[wl-gfx]: https://github.com/HTV04/wiilove/blob/6b1d7e73959ef11fa8fc0df2e878ff8576acb0f3/src/wiilove/modules/graphics.cpp#L53-L286
[itg-nf]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteField.cpp#L836-L1146

[sdl-geom]: https://github.com/devkitPro/SDL/blob/7ebfb9926476d213c4aca26e78a37fd69c3f80f3/src/render/ogc/SDL_render_ogc.c#L493-L560
[sdl-blend]: https://github.com/devkitPro/SDL/blob/7ebfb9926476d213c4aca26e78a37fd69c3f80f3/src/render/ogc/SDL_render_ogc.c#L67-L101
[sdl-tex]: https://github.com/devkitPro/SDL/blob/7ebfb9926476d213c4aca26e78a37fd69c3f80f3/src/render/ogc/SDL_render_ogc.c#L171-L260
[sdl-rt]: https://github.com/devkitPro/SDL/blob/7ebfb9926476d213c4aca26e78a37fd69c3f80f3/src/render/ogc/SDL_render_ogc.c#L272-L316
[ogx]: https://github.com/devkitPro/opengx/blob/23c12e6845a5529bbaeb555a55f62f3b9f30f777/README.md

[ogc-drawdone]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L1566-L1679
[ogc-pixfmt]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3475-L3490
[ogc-copyfilter]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3669
[ogc-alphacmp]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3106
[ogc-ind]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L3217-L3251
[ogc-dlist]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L2040-L2115
[ogc-tmem]: https://github.com/devkitPro/libogc/blob/ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e/gc/ogc/gx.h#L4221-L4520
