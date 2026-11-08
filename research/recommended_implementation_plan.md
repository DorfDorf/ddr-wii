# Recommended graphics and GUI architecture

*The final recommendation of the research brief on Wii GUI, graphics and animation for ddr-wii (the brief calls the game "Dance Dance Rebellion" and "Dance Dance Wiivolution"). It builds on the other four research documents:*

| Document | Question it answers |
|---|---|
| [gui_framework_comparison.md](gui_framework_comparison.md) | How existing Wii projects structure screens, widgets, navigation, focus, carousels, backgrounds, themes, input and transitions, and which implementations are best |
| [graphics_library_comparison.md](graphics_library_comparison.md) | GRRLIB vs libwiisprite vs direct GX (plus libgui's driver, HBC, SDL2, OpenGX) for sprites, blending, fonts, images, layering and 3D |
| [animation_system_research.md](animation_system_research.md) | How each project animates, what to reuse, and the animation core to build |
| [wii_graphics_architecture.md](wii_graphics_architecture.md) | Hardware limits, budgets, and how each rhythm-game element should be rendered |

*Markers: **[src]** read in source, **[doc]** documentation, **[mem]** from memory (verify), **[exp]** still to be tested, **[measured]** this project's own runs on the real Wii (doc 05). Nothing has been implemented. The plan follows CLAUDE.md's code rules (portable `core/`, thin `platform/wii/`, GRRLIB/GX drawing in `render/`, SPDX headers, notices kept on ported ITGmania code, no shipped third-party art without a CREDITS line).*

---

## 1. The recommendation

**Build a small in-house presentation layer on top of what ddr-wii already has, instead of adopting a GUI or sprite library.**

1. **Graphics: keep GRRLIB as the base for now, behind ddr-wii's own `render::` API, and add a direct-GX batched sprite path inside that API.** GRRLIB keeps video init, present and texture utilities. Notes, explosions, text and particles go through a `SpriteBatch` that draws atlased quads in long runs. libwiisprite, libgui's video driver, SDL2 and OpenGX are not adopted.
2. **UI: a small retained tree for menus and HUD** (label, image, panel, button, list/wheel, progress, spinner), with per-player focus, menu actions built on `wii::Input`, a theme struct, a scene manager with In/Out transitions, a shared animated background, and loading on a worker thread.
3. **Gameplay rendering stays immediate-mode and separate:** a playfield renderer that reads `Judge` state and song time each frame, sharing only the render core with the UI.
4. **Animation is time-based everywhere** (UI seconds, song seconds, song beats), through a portable `core/anim` library tested on the PC, with ITGmania's curves, effect clocks and wheel motion, and Simply Love's and the default noteskin's timings as the starting values.

---

## 2. Why: the evidence

| Decision | Evidence | Where |
|---|---|---|
| Don't switch graphics library now | GRRLIB holds 60 Hz with 1,600 sprites using 4.20 ms CPU, and the 4-player worst case uses 1.58 ms CPU + ≈1 ms GPU of a 16.7 ms frame, on the real Wii **[measured]** | doc 05 M2, M2b; [graphics §4](graphics_library_comparison.md#4-2d-textured-quads-what-each-library-does-per-sprite) |
| But add a batched path | Each GRRLIB `DrawImg` rebuilds the texture object, sets and resets TEV and vertex format, writes the copy filter and builds a matrix with trig **[src]**; ≈2.6 µs per sprite **[measured]**. A richer UI (every glyph, particle and panel a draw) would reach thousands of draws. devkitPro's `gxSprites` draws 1,024 atlased sprites with one texture load and no per-sprite matrix **[src]** | [graphics §4](graphics_library_comparison.md#4-2d-textured-quads-what-each-library-does-per-sprite) |
| Never use GRRLIB's TTF printer per frame; build a glyph atlas | `GRRLIB_PrintfTTF` re-rasterises each glyph per call and draws one `GX_POINTS` per pixel **[src]**; every other surveyed font system caches glyphs, but as one texture per glyph **[src]** | [graphics §7](graphics_library_comparison.md#7-font-rendering) |
| Not libwiisprite | RGBA8 only, sides must be multiples of 4, a matrix per sprite, a layer manager with a hard-coded −31 px scissor hack, no text layout or timing **[src]** | [graphics §1, §8](graphics_library_comparison.md#1-summary) |
| Not libgui as a library, but its design | It is the best Wii GUI code available (layout containers, AA shapes, themes, host tests) **[src]**, but it owns video, audio and input, builds against libogc2, reads pads with `PAD_ScanPads` (incompatible with `wii::Input`'s SI reader) and flushes the texture cache on every draw **[src]** | [GUI §2.1, §3.9](gui_framework_comparison.md#21-libgui-2026-rewrite) |
| Time-based animation only | Wii homebrew animates per frame and WiiFlow has to switch to bigger factors at 50 Hz **[src]**; ITGmania advances by seconds and beats **[src]**; ddr-wii must behave the same at 50 and 60 Hz (doc 06 §2) | [animation §2](animation_system_research.md#2-the-time-model) |
| Retained UI, immediate playfield | Every polished menu surveyed is a retained tree; even ITGmania draws the note field from game state each frame **[src]** | [GUI §4](gui_framework_comparison.md#4-cross-cutting-findings) |
| Scene manager with transitions and worker loading | ITGmania's screen stack loads the next screen after Out finishes **[src]**; Snes9x GX runs slow tasks on a worker while the GUI keeps drawing **[src]**; ddr-wii's `Gameplay` currently reads the song in its constructor **[src]** | [GUI §3.1, §3.10](gui_framework_comparison.md#31-screen-and-scene-management) |
| Memory budgets for caches | Snes9x GX keeps one preview image on the Wii (six on Wii U) **[src]**; ≈24–28 MB of MEM2 is left for graphics after IOS and the song buffer **[doc + derived]** | [architecture §2](wii_graphics_architecture.md#2-memory-budget) |

---

## 3. Target architecture

```
game/source/
├── screens/        scenes: PlayModeSelect, SongSelect, Gameplay (+ results), OffsetCalibration, tests …
│                   ── build their UI from ui/, draw gameplay through play/, never call GX/GRRLIB directly
├── ui/             retained UI for menus and HUD (Wii side, draws through render/)
│   ├── Node, Label, Image, Panel, Button, ListView/Wheel, ProgressBar, Spinner
│   ├── Focus (per player), MenuInput (actions from wii::Input), Theme
│   └── SceneManager, Transition, SharedBackground
├── play/           immediate-mode gameplay rendering (Wii side, draws through render/)
│   └── PlayfieldRenderer (receptors, notes, holds, mines, explosions), JudgementDisplay, ComboDisplay,
│       LifeBar, SongBackground
├── render/         the only module that talks to GX/GRRLIB
│   ├── SpriteBatch (direct GX), Texture/Atlas (TPL + generated), TextureCache (async, LRU, byte budget)
│   ├── FontAtlas (FreeType → I8/I4 pages), Mesh (AA panels, later), RenderTarget (EFB copy, blur)
│   └── Draw.* (existing GRRLIB wrappers + counters), ArrowArt, Font (debug), Banner
├── core/           portable C++17, PC-tested (existing rule)
│   ├── anim/       Easing, Tween, Track, Smooth/Spring, Periodic, BeatClock, Flipbook, ParticlePool
│   └── ui/         WheelMotion (from ITGmania WheelBase), Navigation, RepeatTimer, layout helpers
└── platform/wii/   WiiVideo (present + stats), WiiInput, WiiAudio — unchanged
```

**Dependency rules:** `screens → ui, play → render → platform`; `core/*` has no libogc includes (as today); only `render/` and `platform/` include GX or GRRLIB.

**Separation between UI and gameplay rendering:**

| | UI (`ui/`) | Gameplay (`play/`) |
|---|---|---|
| Model | Retained nodes, built when a scene opens | Immediate: drawn from `Judge` and chart state each frame |
| Clock | UI time (seconds since the scene opened) | Song time and beat (`SongClock`, `TimingData`) |
| Allocation | At scene build; none per frame | None after song load (doc 06 §2) |
| Typical cost | Tens to a few hundred quads | Hundreds of quads per player |
| Shared | `render::SpriteBatch`, atlases, `FontAtlas`, `core/anim` | same |

---

## 4. Module sketches

### 4.1 `render::SpriteBatch` and textures

```cpp
namespace render {

enum class Blend : u8 { Normal, Add };            // Normal = premultiplied (ONE, INVSRCALPHA); Add = (SRCALPHA, ONE)

struct Texture {                                   // owns or borrows GX texture data (32-byte aligned, MEM2 for big ones)
    GXTexObj obj; void* data; u16 w, h; u8 format; bool owned;
};
struct Cell { const Texture* tex; float u0, v0, u1, v1; float w, h; };   // an atlas region

struct Xform { float x, y, sx = 1, sy = 1, rot = 0, ox = 0.5f, oy = 0.5f; };  // ox/oy: pivot in the cell (LÖVE-style origin)

class SpriteBatch {
public:
    void begin();                                  // sets the batch's GX state once (vertex format, TEV, blend)
    void draw(const Cell& c, const Xform& t, u32 rgba, Blend b = Blend::Normal);   // queues a quad; flushes on texture/blend change
    void drawRect(float x, float y, float w, float h, u32 rgba);                    // untextured (white cell)
    void end();                                    // flush, then restore the GX state GRRLIB expects
    DrawCounts counts() const;                     // same counters as render::Draw (draws, switches, area)
};

}  // namespace render
```

- Corners are computed on the CPU; a fixed rotation (a lane's 0/90/180/270°) uses precomputed offsets, so notes need no trig.
- The last bound texture is cached (libwiisprite's [`BindTexture`](https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/image.cpp#L179-L195) idea); `GX_InvalidateTexAll` stays once per frame.
- Shipped art is TPL built by gxtexconv (`%.tpl : %.scf` is already in devkitPPC's `wii_rules`); code-generated placeholder art (CLAUDE.md) is rendered into an atlas texture at boot, as `ArrowArt` does today.

### 4.2 `render::FontAtlas`

```cpp
class FontAtlas {
public:
    bool load(const u8* ttf, size_t size, int pixelSize);     // FreeType, at boot or on the worker
    void prewarm(char32_t first, char32_t last);              // ASCII + Latin-1 at load
    float measure(std::string_view utf8) const;
    void draw(SpriteBatch& b, float x, float y, std::string_view utf8, u32 rgba, Align a = Align::Left) const;
};
```

One I8 (or I4) page per size, glyphs packed at load, missing codepoints rasterised off the frame path and used from the next frame (song titles fall back to their transliteration meanwhile). The existing 8×16 `render::Font` stays for debug overlays.

### 4.3 `core/anim`

As sketched in [animation_system_research.md §13](animation_system_research.md#13-proposed-animation-core): easing functions (ITGmania's five, ported, plus a few Penner curves), `Tween`, fixed-size `Track` (linear/Hermite/step keys), exact exponential `Smooth` and a critically damped `Spring`, ITGmania-style `Periodic` effects, a `BeatClock`, `Flipbook` and a structure-of-arrays `ParticlePool`. PC tests prove that 50 Hz and 60 Hz stepping give the same values.

### 4.4 `ui/`

```cpp
namespace ui {

struct Node {                                      // position relative to parent; alpha and tint multiply down the tree
    float x = 0, y = 0, sx = 1, sy = 1, rot = 0, alpha = 1; u32 tint = 0xFFFFFFFF; bool visible = true;
    Node* parent = nullptr; std::vector<Node*> children;      // built at scene open, not per frame
    virtual void update(double uiTime) {}
    virtual void draw(render::SpriteBatch& b, const Parent& p) const;
};

// Widgets: Label (FontAtlas), Image (Cell), Panel (9-slice cell now, AA mesh later), Button (style per state,
// time-based blend), ListView / Wheel (virtualised rows driven by core::ui::WheelMotion), ProgressBar, Spinner.

enum class Action : u8 { Left, Right, Up, Down, Start, Back, Select };
class MenuInput {                                  // from wii::Input down/held masks, per port, repeat in seconds
public: void update(const wii::Input& in, double now); bool pressed(int port, Action a) const;
};

struct Theme { /* Simply Love palette by colour index, font sizes, paddings, radii, focus/press timings … */ };

class Scene {                                      // the existing frame()-style screens, plus transitions
public:
    virtual void update(double uiTime) = 0;
    virtual void draw(render::SpriteBatch& b) = 0;
    virtual double transitionIn(double t);         // returns the time it needs; elements stagger their entrances
    virtual double transitionOut(double t);
    virtual bool ready() const { return true; }    // false while worker-thread loading is in flight
};

class SceneManager {                               // stack for overlays, In/Out between scenes, shared background
public: void replace(std::unique_ptr<Scene>); void push(std::unique_ptr<Scene>); void pop(); void frame();
};

}  // namespace ui
```

Focus belongs to a **player** (players join by port, doc 06 §11.2): song select keeps one shared wheel cursor and a difficulty cursor per player, as now. Navigation is list/wheel based with explicit neighbour links for small grids (Wii64's model); libgui's geometric `GuiNavigate` only if a free-form layout appears.

### 4.5 `play/`

`PlayfieldRenderer::draw(const PlayerView&, double beat, double songTime, SpriteBatch&)` reads the `Judge` state and the player's layout table (column x, rotation, atlas cell), so 1, 2 and 4 players and other column counts use the same code (doc 06 §11.3). Effects are evaluated from stored event times (last hit time and grade per lane, as `Gameplay::Player` already keeps), so nothing is allocated per hit. Element details: [wii_graphics_architecture.md §7](wii_graphics_architecture.md#7-the-rhythm-game-elements).

---

## 5. Reuse, adapt or rewrite

| Component | Source | Verdict | Notes |
|---|---|---|---|
| Video init / present | GRRLIB [`GRRLIB_Init`/`GRRLIB_Render`](https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_render.c#L407-L424) | **Reuse** (keep) | Optional later: own GX setup in `WiiVideo` (async draw-done, one XFB like HBC) |
| Batched sprites | devkitPro `gxSprites`; StepMania-Wii's quad-array backend shape | **Rewrite** | Small; the core of the plan |
| Texture loading | GRRLIB `CreateEmptyTextureFmt`, `LoadTextureTPL`; ddr-wii `render::Banner` | **Reuse** | Not GRRLIB's PNG loader for SD content |
| Async image cache | Snes9x GX [`GuiImageAsyncCache`](https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/widgets/GuiImageAsyncCache.h); WiiFlow [`_coverLoader`](https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2956-L3016) | **Adapt** | Extends ddr-wii's `SongMedia` worker with an LRU and prefetch |
| CMPR encoding | WiiFlow [`_convertToCMPR`](https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/texture.cpp#L89-L187) | **Adapt if needed** | RGB565 first (no encoder) |
| Glyph cache | libgui [`GuiTextRenderer`](https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiTextRenderer.cpp#L113-L172), HBC [`font.c`](https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/font.c#L217-L326) | **Rewrite** as an atlas | Their one-texture-per-glyph layout defeats batching |
| AA shapes | libgui [`GuiGeometry`](https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiGeometry.cpp#L495-L590) + display-list caching | **Adapt later** | Rounded panels and shadows with no art; host-testable |
| Widget tree, states, theme struct | libgui `GuiElement`/`GuiShape`/`GuiTheme`; HBC widget layers | **Adapt the design**, rewrite a small set | — |
| Navigation | Wii64 explicit links; libgui [`GuiNavigate`](https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiLayoutSolver.cpp#L221) | **Adapt** | Into `core/ui/Navigation`, PC-tested |
| Wheel motion | ITGmania [`WheelBase::Update`/`Move`](https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L149-L378) | **Port** | Into `core/ui/WheelMotion`, PC-tested |
| Screen stack and transitions | ITGmania [`ScreenManager`](https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L718-L811), [`Transition`](https://github.com/itgmania/itgmania/blob/v1.3.0/src/Transition.cpp#L20-L45) | **Adapt** (simplified) | Non-blocking; worker-thread loads |
| Worker task while drawing | Snes9x GX [`RunWithGuiUpdates`](https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L1031-L1071) | **Adapt** | For `Gameplay`'s song load |
| Tween curves, periodic effects, effect clocks | ITGmania [`Tween.cpp`](https://github.com/itgmania/itgmania/blob/v1.3.0/src/Tween.cpp), [`Actor::PreDraw`/`UpdateInternal`](https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L492-L660) | **Port** | Small, self-contained formulas |
| Keyframe evaluation | USB Loader GX [`HermiteKeyHandler::GetFrame`](https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/banner/Animator.cpp#L197-L241) | **Rewrite** (standard formula) | — |
| Judgement, receptor, explosion, combo timings | Simply Love, ITGmania default noteskin (values in [animation §10](animation_system_research.md#10-music-reactive-effects)) | **Use the numbers** | Our own tables; no theme files copied (CLAUDE.md) |
| Menu background | Simply Love UV-scroll layers, HBC gradients and bubble pool | **Rewrite** from the ideas | — |
| Blurred backdrop | WiiFlow [`_effectBlur`](https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L768-L901) | **Adapt later** | Once per selection change, never per frame |
| 3D cover flow | WiiFlow `CCoverFlow` | **Optional** | Only if a banner carousel is wanted |
| StepMania-Wii's GX backend | `trunk/RageDisplay_GX.cpp` | **Re-implement the idea only** (CLAUDE.md) | The array-of-quads backend shape |
| Pad input in any GUI library | libgui, WiiFlow, HBC, Wii64 | **Avoid** | Menus read actions derived from `wii::Input` |

---

## 6. Phased plan

Small PRs in the project's usual style: each one builds, passes the PC tests when it touches `core/`, is checked in Dolphin, and gets a short hardware checklist for the owner when timing or the look on the TV matters. These would become a milestone in doc 05 (for example "M5b — rendering and UI foundation") once the direction is approved.

| Step | Goal | Main changes | Check |
|---|---|---|---|
| **1. Sprite batch + playfield atlas** | Same picture, fewer draw calls | `render::SpriteBatch` (direct GX) and one atlas built from `ArrowArt`; Gameplay notes, receptors, holds and glows through it; a "batched" mode in the `RenderTest` ramp | Dolphin: identical look, counters show fewer texture switches. **Hardware:** run the ramp both ways and report CPU busy and max frame time at each step |
| **2. Animation core** | Time-based animation everywhere | `core/anim` + tests (50 vs 60 Hz equivalence); `core/ui/WheelMotion` ported from ITGmania; replace SongSelect's per-frame easing; receptor press, explosions and judgement pop as `Track`s with the Simply Love / noteskin timings | PC tests; Dolphin at PAL50 and PAL60 look the same |
| **3. Text** | Real fonts | `render::FontAtlas`; HUD and menus switch from the 8×16 debug font; a text stress mode | Dolphin; **hardware:** text stress numbers, and readability on the TV at 480i and 480p. **Owner decision:** which font file to ship (plus its `CREDITS.md` line) |
| **4. UI layer, scenes and transitions** | Modern menus | `ui::` nodes and widgets, `MenuInput`, `Theme`, `SceneManager` with In/Out and a shared background (UV-scroll layers + gradient); Gameplay loads on the worker behind a loading overlay; port PlayModeSelect first, then SongSelect, then the results | Dolphin: transitions at 50/60 Hz, no frozen frames when a song loads. **Hardware:** a session of menu use; report any stutter |
| **5. Song select polish** | Simply Love-style wheel with art | Banner LRU cache with prefetch (RGB565, byte budget), wheel rows as panels + text, optional blurred banner backdrop | Dolphin: fast scrolling loads nothing until rest; heap display stable. **Hardware:** scroll a large library, watch for hitches |
| **6. Gameplay polish** | Juice | Repeating hold bodies, beat flashes and note shimmer from `BeatClock`, particle bursts at milestones, generated judgement sheet, combo pulse, results animations and histogram | **Hardware:** full songs in 1P and 2P; report max frame time and how it feels |
| 7. *(optional)* Video path and extras | Only if needed | Own GX init/present in `WiiVideo` (async draw-done), 16:9 logical canvas, deflicker option, AA mesh panels, banner carousel | As needed |

Steps 1 and 2 are independent and can go in either order; 3 needs 1; 4 needs 2 and 3; 5 and 6 need 4.

---

## 7. Guardrails

- **Budgets per frame at 60 Hz** ([architecture §5](wii_graphics_architecture.md#5-throughput-budgets)): ≤ 1,000 GRRLIB-style draws or ≤ 5,000 batched quads **[exp]**, ≤ 4 Mpx drawn in gameplay **[exp]**, ≤ 50 texture switches, worst frame under ≈12 ms, zero heap allocations during gameplay.
- **No GX/GRRLIB outside `render/` and `platform/`.** Screens build UI and call renderers.
- **No per-frame steps:** every animation reads a clock (UI time, song time or beat).
- **Nothing blocking on the frame path:** SD reads, decodes and glyph rasterisation run on the worker; uploads are bounded per frame.
- **Caches have byte budgets** and LRU eviction; large buffers live in MEM2.
- **Measure on hardware** before and after each rendering change, with the existing counters (draws, switches, area, CPU, GPU wait).

---

## 8. Risks and open questions

| Risk / question | Plan |
|---|---|
| GRRLIB and the raw-GX batch sharing GX state (vertex descriptors, TEV, matrices) | The batch restores what GRRLIB expects in `end()`; verify with mixed scenes in step 1 **[exp]** |
| Batching gains on hardware | Measured in step 1 before anything else moves **[exp]** |
| `GX_REPEAT` needing power-of-two textures | Make tiling textures 64/128/256 px; confirm on hardware **[mem/exp]** |
| Premultiplied alpha with gxtexconv | gxtexconv converts formats; premultiplying may need a script step before it **[exp]** |
| Japanese song titles | A CJK-capable font is large; start with Latin fonts and fall back to `#TITLETRANSLIT` **[exp]** |
| Fill cost of full-screen layers | Fill test in step 4; keep ≤ 4 layers in gameplay until measured **[exp]** |
| 480i shimmer of thin UI lines | Lines ≥ 2 px; deflicker on in 480i, optional off in 480p **[exp]** |
| Dolphin's blank-texture episodes | Known Dolphin-only issue (doc 06 §5); judge rendering changes on hardware |
| No real art yet | The plan works with code-generated art (CLAUDE.md): AA panels, gradients, generated atlases and FreeType text look clean without any image files |

---

## 9. When implementing, update

- **doc 06** §5 (rendering: batch, atlases, fonts, budgets), §7 (screens: scene manager, transitions, song select), §9 (repository layout: `ui/`, `play/`, `core/anim`, `core/ui`).
- **CLAUDE.md key decisions**, e.g. "All animation is time-based (UI time, song time or beat) through `core/anim`" and "Only `render/` and `platform/` call GX or GRRLIB; sprites and text go through `render::SpriteBatch`".
- **doc 05** with the new milestone and its hardware results.
- **README** docs table: a row for these research documents.

**Decisions for the owner before step 1:** approve the direction (GRRLIB kept, batched GX path added, in-house UI); and later, for step 3, the UI font. Whether to support 16:9 and whether to build a banner carousel can wait until step 5.
