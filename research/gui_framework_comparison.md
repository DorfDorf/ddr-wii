# GUI framework comparison

*Research for ddr-wii's menus and on-screen UI. It is one of five research documents; the final recommendation is in [recommended_implementation_plan.md](recommended_implementation_plan.md), and the companions are [graphics_library_comparison.md](graphics_library_comparison.md), [animation_system_research.md](animation_system_research.md) and [wii_graphics_architecture.md](wii_graphics_architecture.md). Every source link points at a pinned commit (§1). Claim markers follow CLAUDE.md: **[src]** read in source code, **[doc]** documentation, **[mem]** from memory (verify it), **[exp]** still to be tested. "Reuse / adapt / rewrite" verdicts assume the code rules in CLAUDE.md still apply. Nothing here has been implemented.*

---

## 1. What was reviewed

| Project | Read at | What it is | Why it matters here |
|---|---|---|---|
| **libgui** (dborth/libgui, formerly libwiigui) | `a22287e9`, 2026-11-07 (v3.00 + unreleased) | C++ retained widget library for GameCube, Wii and Wii U. Reworked in 2026: v2.00 (September) put it on a driver layer, v3.00 (October) added layout containers, anti-aliased shape meshes, themes and host-side tests | The most complete Wii GUI framework that exists today |
| **Snes9x GX** | `3c1aa182`, 2026-11-07 | Emulator whose menus are built on libgui | A large real app on libgui: screen flow, worker-thread loading, an async preview cache |
| **FCE Ultra GX** | `fbeab303`, 2026-11-07 | Same family | Its `source/libgui` and `source/widgets` are byte-identical to Snes9x GX's **[src: `diff -rq`]**, so it is covered by the Snes9x GX notes |
| **WiiFlow Lite** | `69c0d4d6`, 2026-08-19 | Game loader with a 3D cover flow | Best cover-flow, theme-INI and animated-background reference |
| **GRRLIB** | `a426e617`, 2026-10-12 (4.6.1 is installed) | 2D/3D drawing library over GX | What ddr-wii draws with today. Compared in [graphics_library_comparison.md](graphics_library_comparison.md) |
| **libwiisprite** | nebiun fork `dd4173c9`, 2024-05-02 (0.3.0f) | Sprite/layer library in the style of J2ME's game API | In the brief. It has no GUI layer |
| **WiiLÖVE** | `6b1d7e73`, 2022-08-26 (alpha) | The LÖVE API in Lua on a modified GRRLIB | In the brief. API-design reference only |
| Homebrew Channel *(extra)* | `13305cfb`, 2025-05-09 | The HBC's own UI: plain C on raw GX | Small (≈5.5k lines for the UI), polished and efficient: a draw queue, state layers on widgets, animated background, theme zips |
| USB Loader GX *(extra)* | `e25c4f35`, 2025-07-04 | Loader on a heavily extended libwiigui | Carousel and grid browsers, extra effects, a keyframe animator, a compact theme API |
| Wii64 menu framework *(extra)* | `a3fb91bd`, 2026-10-16 | C++ component tree, also called "libgui" (unrelated to dborth's) | Explicit focus graph, transparency stack |
| ITGmania v1.3.0 + Simply Love *(extra)* | tag `v1.3.0` (`8a3bf888`); Simply Love `dd06138b` (the submodule commit of that tag) | The engine and theme whose look and rules ddr-wii follows | Screen stack and transitions, actor tweens, beat-driven effects, wheel motion, the judgement and receptor timings |
| libogc *(extra)* | `ef718e2d` (3.2.1 is installed) | GX and the rest of the SDK | Ground truth for GX behaviour |
| SDL2 for Wii, OpenGX *(extra)* | `ogc-sdl-2.28` `7ebfb992`; opengx `23c12e68` | Portable 2D renderer / OpenGL 1.x wrapper on GX | Alternatives to compare |
| Funkin' Wii + FinEngine, Wiithym, LiightsOut *(extra)* | `60bd9144` + `12478c38`; `6de64bb0`; `b32d0fd2` | Other Wii game projects found by searching for Wii rhythm games | Little to reuse (§2.9) |
| StepMania-Wii, Dance Clone | local copies in `reference/` | The SM 3.9 Wii port and the SDL 1.2 DDR clone | Reviewed broadly in doc 07; their UI and drawing parts are covered here |

The local Simply Love files on this machine (`C:\Games\ITGmania`) were checked against upstream by git blob hash before being cited: `Graphics/Player judgment.lua`, `Graphics/Player combo.lua`, `metrics.ini` and `BGAnimations/_shared background/Normal.lua` are identical to Simply Love `dd06138b`, and `NoteSkins/dance/default/metrics.ini` and `Down Tap Note.lua` are identical to ITGmania `v1.3.0` **[src]**.

**No open-source Wii rhythm game with a polished renderer was found.** Wiithym's repository contains only a README **[src]**. Funkin' Wii is a 380-line state-machine stub on GRRLIB that parses a Sparrow XML atlas **[src: [teststate.cpp][fw-test]]**, and its FinEngine is an early skeleton with a GX and a desktop-GL backend behind one `Graphics` interface **[src]**. osu!wii is listed on WiiBrew as GRRLIB-based, but no source repository was found **[doc: web search]**.

---

## 2. How each codebase structures its GUI

### 2.1 libgui (2026 rewrite)

- **Layers.** `source/libgui` never includes a platform header. Video, audio, input, storage and threads sit behind abstract drivers owned by one `Platform` object, with GC/Wii (`drivers/ogc`) and Wii U (`drivers/wut`) implementations **[src: [README][lg-readme]]**. It builds against **libogc2**, not devkitPro's libogc (`Makefile.wii:10` includes `$(DEVKITPRO)/libogc2/wii_rules`) **[src]**.
- **Retained tree.** `GuiElement` is the base (position relative to a parent, alignment, alpha, scale, state, effects) **[src: [GuiElement.h][lg-elem-h]]**. `GuiWindow` is the container: it draws children in order and moves the selection **[src: [GuiWindow.cpp][lg-win-draw]]**. Widgets: `GuiButton`, `GuiImage`, `GuiText`, `GuiShape`, `GuiCard`, layout containers `GuiRow`/`GuiColumn`/`GuiGrid`/`GuiOverlay`, `GuiPopover`; app-owned `GuiOptionBrowser`, `GuiFileBrowser`, `GuiKeyboard` **[src: [CHANGELOG][lg-changelog]]**.
- **Modern look without art.** `GuiMesh` tessellates rectangles, rounded rectangles, ellipses, polygons and shadows into triangles with a feathered alpha fringe, so edges are anti-aliased without multisampling **[src: [GuiGeometry.cpp `fillShape`][lg-geo-fill], [`addRoundRect`][lg-geo-rrect]]**. `GuiPaint` (fill/gradient/stroke/shadow, chainable), `GuiShapeStyle` (a paint per state) and `GuiTheme` (palette and metrics, filled by an app-defined `GuiThemeInit()`) give a code-drawn style system **[src: [CHANGELOG][lg-changelog], [GuiTheme.cpp][lg-theme]]**.
- **Builders.** `UiScope` creates and owns a page's elements and supports bindings (`label.bind(fn)`, `visibleWhen`, `enabledWhen`) evaluated once per frame **[src: [GuiUi.h][lg-ui]]**.
- **Tests on the PC.** The layout solver, geometry, image decode and whole-screen D-pad traversal run on the host under ASan/UBSan (`test/`) **[src: [CHANGELOG][lg-changelog]]**. That matches ddr-wii's own "portable core, tested on the PC" rule.
- **Frame loop.** The demo's `UpdateGui()` reads input, calls `mainWindow->update(controller[i])` for four channels, draws the tree and renders **[src: [menu.cpp `UpdateGui`][lg-updategui]]**. Each screen is a function that builds its widgets, loops on `UpdateGui()` until the next menu is chosen, and returns its id to `MainMenu()`'s switch **[src: [menu.cpp `MainMenu`][lg-mainmenu]]**.

### 2.2 Snes9x GX and FCE Ultra GX

- The same libgui plus a code-drawn menu style (`source/menu/menustyle.h`, `menuart.h`, `menupalette.h`, `theme.cpp`). Screens are functions such as [`MenuGameSelection`][s9-gamesel] and [`MenuSettings`][s9-settings] in a 5,107-line `menu.cpp` **[src]**.
- [`RunWithGuiUpdates`][s9-rungui] runs slow work (opening a folder, loading a game) on a worker thread **while the GUI keeps drawing**, with the main window set to `STATE::DISABLED` so it is washed out and ignores input **[src]**. A progress overlay with a dot spinner (`MenuSpinner`, [menuart.h][s9-spinner]) shows meanwhile.
- [`GuiImageAsyncCache`][s9-asynccache] loads preview images: a background thread reads and decodes PNGs to RGBA (CPU only), and `update()`, called once per frame on the main thread, uploads at most a bounded amount. It keeps an LRU of slots, prefetches neighbours of the selection, pins the image on screen and has a `tryShutdown(timeout)` for SD/USB reads that hang **[src]**. On Wii and GameCube it is built with **capacity 1 and no prefetch**, against 6 slots and radius 2 on Wii U ([menu.cpp:1242-1246][s9-cachecfg]) **[src]**: the authors keep full-screen RGBA previews to one on the Wii.

### 2.3 WiiFlow Lite

- One large `CMenu` class with a `_showX/_hideX/_textX` set per screen and a shared frame function [`_mainLoopCommon`][wf-mainloop] that ticks the cover flow, the buttons and the fanart, then draws the background, cover flow, effects and buttons **[src]**.
- Widgets live in [`CButtonsMgr`][wf-btnmgr]: a flat list of buttons, labels and progress bars addressed by `s16` ids. Each element has a current and a target position, alpha and scale, and [`SElement::tick`][wf-elemtick] moves it toward the target every frame **[src]**. Buttons are 3-slice textures (left, centre, right, plus selected variants) **[src]**.
- The 3D cover flow and the theme system are the strongest parts (§3.6, §3.8).

### 2.4 USB Loader GX

- A heavily extended libwiigui: the original slide/fade/scale effects plus `EFFECT_PULSE`, `EFFECT_ROCK_VERTICLE` and `EFFECT_GOROUND` ([gui.h:114-125][ul-effects]) **[src]**.
- Several game browsers on the same data: list, grid ([gui_gamegrid.cpp][ul-grid]), carousel ([gui_gamecarousel.cpp][ul-carousel]), banner grid, plus a 3D box cover **[src]**.
- A Wii channel-banner player (`source/banner/`: layouts, panes, materials and a keyframe [`Animator`][ul-anim]) **[src]**, covered in [animation_system_research.md](animation_system_research.md).

### 2.5 Homebrew Channel

- Plain C on raw GX. Drawing goes through a **retained draw queue**: `gfx_queue_entry` items are origin push/pop, scissor and entity draws (gradient or texture, with x/y/z, scale, rotation and colour) **[src: [gfx.h][hbc-gfxh]]**. [`gfx_frame_push`][hbc-push] walks the entries with an origin stack, so a widget's offset moves all its children **[src]**.
- A **view** is a set of widgets plus a `sub_view` it is drawn over, which is how dialogs sit above the screen that opened them ([view.h][hbc-viewh]) **[src]**.
- Each widget has **layers**: lists of draw entries that are drawn only when the widget's flags (`WF_FOCUSED`, `WF_CURSOR`, `WF_ENABLED` …) match the layer's ([`view_push_view`, view.c:110-151][hbc-layers], [widgets.h][hbc-widgetsh]) **[src]**. Focus, hover and disabled looks are therefore data, not code.

### 2.6 Wii64 menu framework

- `Component` → `Frame`, `Button`, `TextBox`, `Image`, `LoadingBar`, `MessageBox`, plus 3D `Box3D`/`Logo` **[src]**.
- Focus is an **explicit graph**: each component stores its neighbour per direction ([`setNextFocus`/`getNextFocus`][w64-nextfocus]) and a `Focus` singleton turns new D-pad presses into directions ([`Focus::updateFocus`][w64-focus]) **[src]**.
- `Graphics` keeps a **transparency stack** (`pushTransparency`/`popTransparency`: a child's alpha is multiplied by its parent's) and a depth stack ([GraphicsGX.cpp][w64-gfx]) **[src]**.
- [`Gui::draw`][w64-guidraw] calls `updateTime(0.0f)` with a `TODO: Pass deltaTime` comment: time-based updates were planned and never wired up **[src]**.

### 2.7 libwiisprite and WiiLÖVE

- **libwiisprite** has sprites, tiled layers and a `LayerManager`, but no widgets, text layout, focus or animation timing. `LayerManager::Draw` draws from the last layer to the first inside a scissor box with a hard-coded `-31` offset described as "this fixes everything" ([layermanager.cpp:94-109][ws-lm]) **[src]**. It is a sprite toolkit, not a GUI framework.
- **WiiLÖVE** exposes LÖVE's immediate-mode API to Lua: `love.graphics.push/pop/translate/rotate/scale` over a matrix stack ([graphics.cpp:53-108][wl-gfx]), `draw(texture, x, y, r, sx, sy, ox, oy)` with an origin, quads, and `love.update(dt)` timing from `gettime()` ([timer.cpp][wl-timer]) **[src]**. It depends on modified libogc/GRRLIB/PNGU packages from its author's own pacman repository (`Makefile:82-85`) and has been inactive since 2022 **[src]**. The API shape (transform stack, origins, dt-based updates) is worth copying; the runtime is not.

### 2.8 ITGmania and Simply Love

- **Screens** are `Actor` trees managed by `ScreenManager`: a stack ([`AddNewScreenToTop`][itg-sm-add], [`PopTopScreen`][itg-sm-pop]), [`PrepareScreen`][itg-sm-prep] to preload the next one, [`LoadDelayedScreen`][itg-sm-delay] to build the next screen only after the current one's Out transition has finished, and a shared background **[src]**.
- `ScreenWithMenuElements` owns **In, Out and Cancel transitions** drawn above everything ([ScreenWithMenuElements.cpp:108-124][itg-swme]), each a `Transition` that goes waiting → transitioning → finished when its tweens run out ([Transition.cpp][itg-trans]) **[src]**.
- **Animation is time-based** (seconds and song beats), through tween queues and effect clocks; see [animation_system_research.md](animation_system_research.md).
- **Simply Love's music wheel** is a vertical list: 17 items, `y = offsetFromCenter × screen_h / 15`, `SwitchSeconds=0.05` ([metrics.ini:1529-1543][sl-wheel]) **[src]**.

### 2.9 StepMania-Wii, Dance Clone and the other games

- **StepMania-Wii** keeps SM 3.9's architecture: `Actor` trees and screens over a `RageDisplay` backend. Its `RageDisplay_GX` receives vertex arrays (`DrawQuadsInternal` sends a whole array in one `GX_Begin`, `trunk/RageDisplay_GX.cpp:452-463`), converts every texture to RGBA8 (`:285-298`), and serialises each frame with copy → `GX_DrawDone` → vsync (`:221-239`) **[src]**. The *idea* worth keeping is the split between a scene graph and a backend that takes arrays of quads.
- **Dance Clone** has a small pointer-only widget set ("Gooey": `Element` with `Clicked(x, y, channel)` and `CursorAt`, no D-pad focus) and SDL 1.2 blits **[src: `Source/GUI/Element.h:80-81`]**. Its `Decal` class times judgement and OK/NG flipbooks against **song time** with Static/Once/Loop modes, and drifts rating decals down linearly over their duration (`Source/Game/Decal.h:35-40`, `Game.cpp:2004-2060`) **[src]**. It pre-rotates arrow sheets at load because SDL 1.2 can't rotate at draw time (`Graphics.cpp:282-363`) **[src]**; GX rotates for free.
- **Funkin' Wii / FinEngine**: a `FinState` (`init`/`update`/`render`/`cleanup`) switched by `FinGame` ([FinGame.cpp][fw-game]) **[src]**. Nothing beyond what ddr-wii already has.

### 2.10 ddr-wii today

- Each screen is a class with `frame()`; `main.cpp`'s [`runScene`](../game/source/main.cpp#L29-L40) constructs it, runs it until it returns false, and destroys it. There are no transitions, no shared background and no retained UI tree **[src]**.
- Drawing is immediate-mode through `render::img/tile/rect`, thin GRRLIB wrappers that count draws, texture switches and area ([Draw.h](../game/source/render/Draw.h)) **[src]**. Text is the 8×16 libogc console font in a tile set ([Font.cpp](../game/source/render/Font.cpp)) **[src]**.
- `Gameplay` loads the song file in its constructor ([Gameplay.cpp:83-88](../game/source/screens/Gameplay.cpp#L83-L88)), so the screen stops drawing while the SD card is read **[src]**.
- The song wheel eases with `scroll_ += (cursor_ - scroll_) * 0.35f` once per frame ([SongSelect.cpp:316](../game/source/screens/SongSelect.cpp#L316)) **[src]**, which runs 20% slower at PAL 50 Hz than at 60 Hz. The gameplay effects, by contrast, are computed from timestamps (e.g. the 150 ms receptor glow, [Gameplay.cpp:334-338](../game/source/screens/Gameplay.cpp#L334-L338)) **[src]**.

---

## 3. Topic-by-topic comparison

Verdicts: **Reuse** = take the code as it is; **Adapt** = take the code or algorithm with changes; **Rewrite** = write our own following the idea; **Avoid** = don't follow it.

### 3.1 Screen and scene management

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| Blocking menu functions | libgui [`MainMenu`][lg-mainmenu], Snes9x GX [`MenuSettings`][s9-settings] | Each screen builds widgets on the stack and loops until it returns the next menu id | **Avoid** for ddr-wii. A transition between two screens needs both alive at once, and ddr-wii's scenes already return to one frame loop | None; it's a structure problem |
| Worker task + live GUI | Snes9x GX [`RunWithGuiUpdates`][s9-rungui] | Slow task on a worker thread, GUI keeps drawing, window disabled meanwhile | **Adapt** for song loading (the audio file read and chart parse in `Gameplay`'s constructor) | The worker must run below the main thread's priority, as `SongMedia` already does (priority 40) |
| One class for all screens | WiiFlow `CMenu` + [`_mainLoopCommon`][wf-mainloop] | Every screen is a show/hide/text method set inside one class | **Avoid**. It doesn't scale and mixes screens' state | — |
| Views with sub-views | HBC [view.h][hbc-viewh] | A dialog view draws over its parent | **Adapt** as overlay scenes (pause menu, prompts) | — |
| Screen stack + delayed load + shared background | ITGmania [`ScreenManager`][itg-sm-add], [`LoadDelayedScreen`][itg-sm-delay] | Stack for overlays; the next screen is built after the current Out transition finishes; one background persists | **Adapt**, simplified (no Lua, no preparing two screens at once) | Building the next scene must not stall the frame; heavy loads go to a worker |
| State machine | FinEngine [`FinGame`][fw-game]; ddr-wii [`runScene`](../game/source/main.cpp#L29-L40) | Current scene + next scene | Already in place; extend it | — |

**Best:** ITGmania's model, cut down: a `SceneManager` with a scene stack, In/Out transitions, a persistent shared background, and loading on a worker during the transition (Snes9x GX's pattern).

### 3.2 Reusable UI components

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| Element tree + widgets + builders | libgui [GuiElement.h][lg-elem-h], [GuiUi.h][lg-ui], containers in [GuiLayout.cpp][lg-layout] | Parent-relative placement, states, effects; layout containers that reflow; builder scopes with bindings | **Adapt the design**, rewrite a small subset. Taking the library would bring its own video/input/audio drivers (§3.9) | Its `drawTexture` calls `GX_InvalidateTexAll()` on every draw ([OgcVideoDriver.cpp:225-273][lg-drawtex]), which empties the texture cache per quad |
| Anti-aliased shapes | libgui [GuiGeometry.cpp][lg-geo-fill], `GuiMeshCache`, `GuiShape` | Rounded panels, capsules, shadows, gradients as feathered triangle meshes; meshes cached and replayed as GX display lists ([`createMesh`/`drawMeshInstance`][lg-dlist]) | **Adapt** (the tessellator is platform-agnostic and host-tested) | Vertex count: a 40×40 key went from 2,664 to 1,332 vertices after their optimisation **[src: CHANGELOG]**; cache meshes, don't rebuild per frame |
| Flat id-indexed widget list | WiiFlow [`CButtonsMgr`][wf-btnmgr] | Buttons/labels/progress bars with targets, 3-slice textures | **Avoid** the structure, keep the 3-slice idea | 3 draws per button |
| State layers | HBC [`view_push_view`, view.c:110-151][hbc-layers] | Per-widget draw lists shown by flag match | **Adapt** the idea: a widget's look per state is data | Cheap: the entries are prebuilt |
| Component + modal singletons | Wii64 [`Gui::draw`][w64-guidraw] | MessageBox and LoadingBar singletons drawn over everything | **Avoid** singletons; use overlay scenes | — |
| Actor/ActorFrame/Sprite/BitmapText | ITGmania (Actor.cpp, ActorFrame.cpp) | Everything on screen is an actor with tweens and effects | **Adapt the ideas** (time-based tweens, effect clocks), not the Lua-driven actor system | ITG's actor tree costs too much per node for hundreds of notes on a 729 MHz CPU (doc 02); keep notes out of the tree |

**Best:** libgui's newer pieces (containers, shapes, paints, theme struct, builders) as the design model, HBC's state layers as the cheap mechanism, and a deliberately small widget set of our own: label, image, panel (shape or 9-slice), button, list/wheel, progress bar, spinner.

### 3.3 Menu navigation

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| Geometric "nearest in direction" | libgui [`GuiNavigate`][lg-nav] (host-tested), used by [`GuiWindow::moveSelectionIn`][lg-movesel] | Picks the nearest selectable item in the pressed direction; a row of mixed heights counts as one row; disabled and hidden items are skipped | **Adapt** if a grid screen appears (options) | None |
| Explicit neighbour links | Wii64 [`setNextFocus`][w64-nextfocus] | The screen code wires each component's up/down/left/right neighbour | **Adapt** for small fixed screens (it's the most predictable on a mat) | None |
| Cycle in creation order | WiiFlow [`CButtonsMgr::up`][wf-btnup], HBC `view_set_focus_prev/next` ([view.c:304-340][hbc-focus]) | Up/down walks the list | Fine for single lists only | — |
| Key repeat | libgui [InputController.h:81-82][lg-repeat] (first repeat 0.3 s, then every 0.05 s); ITGmania [`WheelBase::Move`][itg-wheel-move] (0.25 s, then a continuous spin); ddr-wii song select (0.4 s) | Held direction repeats on a timer | **Rewrite** in ddr-wii's time base (it already does this; make it shared) | Must be time-based, not frame-counted |

On a dance mat the inputs are four arrows plus Start and a back button, so almost every screen is a list or a wheel. **Best:** lists and wheels with timed repeat for most screens, explicit neighbour links for the few small grids, and libgui's geometric search only if a free-form layout is ever built.

### 3.4 Focus and selection states

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| State enum + style per state, time-based blend | libgui `STATE` ([GuiElement.h:37-44][lg-state]); [`GuiShape::setTransition`][lg-shape-trans] blends between state paints with `blend += deltaTime / transition` and a smoothstep, quantised to 4 cached steps ([GuiShape.cpp:115-126][lg-shape-blend]) | DEFAULT, SELECTED, CLICKED, HELD, DISABLED; looks per state; smooth change | **Adapt** | The 4-step quantisation exists so in-between meshes can be cached; with textured panels, a continuous blend costs nothing |
| Flags select draw layers | HBC [`view_push_view`, view.c:110-151][hbc-layers] | Focus glow is a separate layer that appears when `WF_FOCUSED` is set | **Adapt** | — |
| Per-controller selection | libgui `setState(STATE::SELECTED, chan)` in [`GuiButton::update`][lg-btn-update] | Each controller can select a different button | **Rewrite** for ddr-wii's players: song select already has one shared wheel cursor plus a difficulty cursor per player | Per player, not per controller channel: players join by port (doc 06 §11.2) |
| Scale + sound on focus | WiiFlow `up()` sets target scale 1.1 and plays the hover sound ([gui.cpp:573-603][wf-btnup]) | — | **Adapt** (time-based) | — |

**Best:** libgui's state enum with a style per state and a time-based blend, drawn either as HBC-style layers or as tweened properties. Focus belongs to a player, not to a controller channel.

### 3.5 Animated buttons

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| Grow on hover | libgui `setEffectGrow()` = scale effect +4% per frame to 110% ([GuiElement.cpp:520-531][lg-fx]) | Linear per-frame steps | **Avoid** the per-frame step | 50 Hz runs 20% slower |
| Click "pop" and asymmetric smoothing | WiiFlow [`SButton::tick`][wf-btntick] (`click` decays ×0.8 per frame, drawn at scale `(1-click)×1.6`), [`SElement::tick`][wf-elemtick] (scale grows at 0.3, shrinks at 0.1 of the gap per frame) | Pleasant feel | **Adapt** as time-based tweens | Same frame-rate issue |
| Pulse / rock / orbit effects | USB Loader GX [`EFFECT_PULSE`][ul-pulse], [`EFFECT_GOROUND`][ul-goround] | Breathing scale, vertical bob, orbit | **Adapt** as periodic effects with a time or beat clock | Frame-stepped in the source |
| Focus tween + periodic effects | ITGmania `BeginTweening` + effects such as `pulse`, `glowshift` ([Actor.cpp:492-660][itg-predraw]) | Any property tweened in seconds; effects can run on the song's beat | **Adapt** | — |

**Best:** buttons animate through time-based tweens (focus: scale up with a decelerate curve over about 0.1–0.15 s; press: a short dip and return) and optional periodic effects on the beat clock. The values to start from are in [animation_system_research.md](animation_system_research.md).

### 3.6 Cover-flow and carousel interfaces

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| 3D cover flow | WiiFlow `CCoverFlow`: layout as data ([`SLayout`][wf-slayout]), per-slot targets ([`_updateTarget`][wf-target]), per-frame smoothing ([`_coverTick`][wf-covertick]), multi-step jumps ([`_jump`][wf-jump]), 45° perspective ([`init`][wf-cfinit]), reflections ([`_drawMirrorZ`][wf-mirror]), blurred shadow/reflection texture ([`makeEffectTexture`][wf-effect], [`_effectBlur`][wf-blur]) | The reference cover flow | **Adapt** the slot-layout + target model; build the 3D/blur parts only if wanted | Its smoothing factors are per frame, so it raises them in 50 Hz mode (`m_normal_speed = 0.12`, [init:236-242][wf-cfinit]). Blur needs EFB copies; supersampling multiplies scene cost (§3.7) |
| Windowed cover loading | WiiFlow [`_coverLoader`][wf-loader] | A thread keeps covers loaded in a window around the centre, alternating left/right outward, evicts outside it, loads a high-quality cover only for the centre, and shrinks the window on out-of-memory | **Adapt** | — |
| Pre-converted cover cache | WiiFlow `.wfc` files ([`SWFCHeader`][wf-wfc], [`cacheCoverFile`][wf-cache]) | Covers stored on SD already in GX format (CMPR, mipmaps, optionally zipped) | **Adapt** for banners if decode time shows up | CMPR is 8× smaller than RGBA8 (§ graphics doc) |
| Arc carousel | USB Loader GX [`GuiGameCarousel`][ul-carousel]: covers on a 780-px-radius arc 7° apart, held direction accelerates by 4/frame up to 250 ([gui_gamecarousel.cpp:28-34][ul-car-consts]); async cover images ([`GuiImageAsync`][ul-async]) | Cheaper 2.5D carousel | **Adapt** the motion idea, in time units | — |
| Offset-based wheel | ITGmania [`WheelBase::SetPositions`][itg-wheel-pos] and [`Update`][itg-wheel-update]: items placed by `offsetFromSelection`, spin at a set items/second after a 0.25 s hold, settle at `0.2 + |offset|/SwitchSeconds` per second, locked-wheel bounce | Time-based, exact under frame drops | **Adapt** (pure logic; belongs in `core/` and can be PC-tested) | — |
| Async LRU preview cache | Snes9x GX [`GuiImageAsyncCache`][s9-asynccache] | Decode off-thread, bounded upload per frame, prefetch, pin | **Adapt** for banners | Budget memory: Snes9x GX uses 1 slot on Wii for 640×480 RGBA |
| ddr-wii | [SongSelect.cpp](../game/source/screens/SongSelect.cpp), [SongMedia.cpp](../game/source/screens/SongMedia.cpp) | Simply Love-style vertical wheel; one banner loaded once the cursor rests 0.2 s | Keep, upgrade | Banners are 256×100 RGBA8 (100 KB each) |

**Best:** ITGmania's offset-based wheel motion (time-based) with WiiFlow's "slot layout as data", fed by a Snes9x GX-style async LRU banner cache with prefetch. That supports Simply Love's vertical list now and a horizontal banner carousel or a 3D cover flow later with the same model.

### 3.7 Background rendering

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| UV-scrolling tiled layers | Simply Love [`_shared background/Normal.lua`][sl-bg] | 10 full-screen sprites of one tiled texture, each with its own scroll velocity (`texcoordvelocity`), low alpha (0.05–0.2), tinted by the theme colour | **Adapt** with 3–4 layers | Each layer is a full-screen blended quad: 10 layers ≈ 3 Mpx per frame. Needs a power-of-two texture for `GX_REPEAT` (§ wii_graphics_architecture) |
| Particle background | HBC [`bubbles.c`][hbc-bubbles] | Static pool of bubbles rising with a sine wobble, popped by the pointer into sub-bubbles; count follows the time of day | **Adapt** (pool, no allocation) | Per-frame steps |
| Gradients | HBC [`plot_gradient`][hbc-grad] | Quads with four corner colours, no texture | **Reuse the technique** | Costs a quad |
| Animated fanart | WiiFlow [`CFanartElement::tick`][wf-fanart] | Theme-defined layers that move/scale/rotate/fade over frames | Idea only | Per-frame |
| Blurred background | WiiFlow [`_effectBlur`][wf-blur] | Separable Gaussian in one pass of up to 7 TEV stages, weights in TEV constant colours | **Adapt** for a blurred song banner behind panels, computed once per song change | Needs EFB copies; fine once per change, not per frame |
| Tinted background | Wii64 [`Gui::drawBackground`][w64-bg] | One texture recoloured in TEV with a register colour | **Reuse the technique** (palette-tinted greyscale art) | — |
| Persistent background | ITGmania shared background | Survives screen changes | **Adapt** | — |

**Best:** a shared background owned by the scene manager: 3–4 UV-scrolling, palette-tinted layers (Simply Love), gradient quads (HBC), and optionally a blurred copy of the selected song's banner made once per selection (WiiFlow's TEV blur).

### 3.8 Theme and skin management

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| App-defined theme struct | libgui [`GuiTheme::current`][lg-theme] + `GuiThemeInit()` in `source/theme.cpp`, styles `panelStyle()`, `buttonStyle()` … | Palette and metrics in code; style recipes built from them | **Adapt** now | — |
| Theme INI with code defaults | WiiFlow [`CMenu::_addButton`][wf-addbtn] (`m_theme.getInt(domain, "x", default)` for position, size, colour, textures, font, sounds), [`_setHideAnim`][wf-hideanim] (`effect_x`, `effect_y`, `effect_scale_x/y`) | Each element looks itself up by domain; the code supplies defaults | **Adapt later** (ddr-wii already reads INI) | Parsing at screen build, not per frame |
| Default inside the key | USB Loader GX `thInt("0 - hdd info pos x")` ([GameBrowseMenu.cpp:128][ul-thint]) | One string holds the default and the name | Idea only | — |
| Zip of override files | HBC [`theme_get_index`][hbc-theme] | A PNG named like a built-in asset replaces it; `_wide.png` variants for 16:9; fonts too | **Adapt later** for SD-card skins | — |
| metrics.ini + Lua + graphics | ITGmania / Simply Love | Full theming engine | **Avoid** (doc 06 lists Lua and themes as non-goals) | Lua and the actor tree are too heavy (doc 02) |

**Best:** start with a libgui-style `Theme` struct in code (Simply Love's palette, fonts, metrics), so every widget asks the theme rather than hard-coding colours. File-based skins can later override it WiiFlow-style (INI keys with code defaults) and HBC-style (same-named PNGs on the SD card), without changing widget code.

### 3.9 Controller input

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| libgui input driver | [OgcInputDriver.cpp:82-93][lg-pad] calls `PAD_ScanPads()`, `PAD_ButtonsDown/Held`; Wii adds `WPAD_ScanPads` | Per-channel snapshot + `InputController` repeat/pointer smoothing | **Avoid** | Conflicts with `wii::Input`: SI hands each new sample to one reader, so the game reads pads only through `wii::Input` (CLAUDE.md, doc 04 §5) |
| WiiFlow, HBC, Wii64 | `menu_input.cpp:64-65` (`WPAD_ScanPads`, `PAD_ScanPads`), [controls.c:107][hbc-controls], [FocusManager.cpp][w64-focus] (`PAD_ButtonsHeld`) | Same pattern | **Avoid** | Same conflict |
| Trigger objects | libgui `GuiTrigger` (button masks mapped to Accept/Cancel across layouts) | Declarative bindings per widget | Idea only | — |
| Timestamped events | ddr-wii [`wii::Input`](../game/source/platform/wii/WiiInput.h) | SI polling interrupt, debounced, timestamped lane events + held/down masks per port | **Keep**; build menu actions on it | — |

**Best:** every GUI library surveyed polls the pads itself, so none of their input layers can be used. The UI should consume menu actions (Left, Right, Up, Down, Start, Back, per port) derived from `wii::Input`'s down/held masks, with repeat timing in seconds.

### 3.10 Screen transitions

| Implementation | Where | What it does | Verdict | Wii concern |
|---|---|---|---|---|
| Per-frame slide/fade effects, blocking waits | libgui `SLIDE_IN/OUT`, `FADE` ([GuiElement.cpp:426-532][lg-fx]); `WindowPrompt` spins `while(promptWindow.getEffect() > 0)` ([menu.cpp:165-182][lg-prompt]); exit fade adds 15 alpha per frame ([menu.cpp:70-80][lg-updategui]) | Simple, but locks the loop and depends on frame rate | **Avoid** the blocking and the per-frame steps | 50 Hz plays slower |
| Sine slide + mask fade, blocking | HBC [`dialog_fade`][hbc-dfade] (dialog rises along `sin` over `TRANS_STEPS` frames while the mask darkens) | Nice ease-out | **Adapt** the curve, not the loop | — |
| Hide/show to theme offsets | WiiFlow [`hide/show`][wf-hide] + `_setHideAnim` | Every element glides to or from its theme-defined "hidden" offset and scale | **Adapt** (element-level in/out) | Per-frame smoothing |
| In/Out/Cancel transitions | ITGmania [ScreenWithMenuElements.cpp:108-124][itg-swme], [Transition.cpp][itg-trans], [`LoadDelayedScreen`][itg-sm-delay] | Transition actors on top; the new screen loads after Out ends | **Adapt** | Load must not stall the frame (worker) |

**Best:** ITGmania's In/Out model on a non-blocking frame loop: the old scene plays its Out timeline while the next one is built (heavy parts on a worker), then the new scene plays In; the shared background stays put. Element-level entrance animations (WiiFlow's hide/show) make it look polished at little cost.

---

## 4. Cross-cutting findings

1. **Wii homebrew GUIs animate in frame steps; ITGmania animates in time.** libgui's effects, WiiFlow's smoothing, USB Loader GX's effects and HBC's bubbles and dialogs all add a fixed amount per frame **[src]**. WiiFlow even switches to larger factors in 50 Hz mode **[src: [init][wf-cfinit]]**. ITGmania advances everything by seconds or beats ([`UpdateTweening`][itg-tween-update], [`UpdateInternal`][itg-effclock]) **[src]**. ddr-wii must behave the same at 50 and 60 Hz (doc 06 §2), so time-based is the only option. libgui's newest code agrees: `GuiShape` blends by `getDeltaTime()` ([GuiShape.cpp:115][lg-shape-blend]) **[src]**.
2. **None of the libraries can be adopted whole.** libgui is the best GUI code available, but it brings its own `Platform`, video, audio and input drivers, builds against libogc2, and reads pads with `PAD_ScanPads` **[src]**. ddr-wii's audio clock and input design (CLAUDE.md key decisions) depend on owning those pieces. The value is in its algorithms and design: layout solver, geometry tessellator, navigation, theme struct, async image cache.
3. **Blocking UI loops are common** (libgui prompts, HBC dialogs and fades, Snes9x GX menus) **[src]**. A rhythm game needs one frame loop that never blocks: audio previews, banner loads and transitions all run alongside.
4. **Memory caution is real.** Snes9x GX keeps one preview image on the Wii but six on Wii U **[src]**; WiiFlow shrinks its cover window when allocation fails **[src]**. Banner caches need an explicit budget.
5. **Retained UI, immediate gameplay.** Every polished menu here is a retained tree (libgui, HBC, WiiFlow, ITG), while the note field is drawn from game state every frame even in ITGmania ([`NoteField::DrawPrimitives`][itg-nf-draw]) **[src]**. ddr-wii should keep that split: a small retained tree for menus and HUD, and a playfield renderer that reads `Judge` state directly.

---

## 5. Verdict for ddr-wii

- **Don't adopt a GUI library.** Build a small in-house UI layer, using libgui (2026) as the design model, HBC as the efficiency model, and ITGmania/Simply Love as the behaviour and timing model.
- **Adapt** (algorithm or code with changes): ITGmania's wheel motion and screen/transition flow; libgui's `GuiNavigate`, `GuiMesh` tessellation, theme struct and state styles; Snes9x GX's async LRU image cache and worker-task pattern; WiiFlow's slot-layout cover model, windowed loader and TEV blur; HBC's state layers, gradients and particle pool.
- **Rewrite**: the widget set (label, image, panel, button, list/wheel, progress, spinner), the scene manager, menu input on top of `wii::Input`, and all animation in a time base (see [animation_system_research.md](animation_system_research.md)).
- **Avoid**: blocking menu loops, per-frame animation steps, per-draw `GX_InvalidateTexAll`, singletons for modals, and any input layer that polls the pads.

The architecture, module layout and phased plan are in [recommended_implementation_plan.md](recommended_implementation_plan.md).

---

## References

[fw-test]: https://github.com/NicktasticDEV/Funkin-Wii/blob/60bd9144466ca3a2447ba9117da7717b73e49cb6/funkin/source/states/teststate.cpp
[fw-game]: https://github.com/NicktasticDEV/Funkin-Wii/blob/60bd9144466ca3a2447ba9117da7717b73e49cb6/engine/source/engine/FinGame.cpp

[lg-readme]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/README.md#architecture
[lg-changelog]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/CHANGELOG.md
[lg-elem-h]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiElement.h#L47-L59
[lg-state]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiElement.h#L37-L44
[lg-fx]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiElement.cpp#L426-L532
[lg-win-draw]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiWindow.cpp#L109-L138
[lg-movesel]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiWindow.cpp#L298-L445
[lg-nav]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiLayoutSolver.cpp#L221
[lg-layout]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiLayout.cpp
[lg-geo-fill]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiGeometry.cpp#L495-L544
[lg-geo-rrect]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiGeometry.cpp#L556-L571
[lg-theme]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiTheme.cpp#L28-L40
[lg-ui]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiUi.h#L203-L260
[lg-shape-trans]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiShape.cpp#L63-L66
[lg-shape-blend]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiShape.cpp#L90-L130
[lg-btn-update]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiButton.cpp#L191-L260
[lg-updategui]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/menu.cpp#L43-L97
[lg-prompt]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/menu.cpp#L99-L190
[lg-mainmenu]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/menu.cpp#L1020-L1095
[lg-repeat]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/InputController.h#L81-L82
[lg-pad]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcInputDriver.cpp#L82-L93
[lg-drawtex]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L225-L273
[lg-dlist]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/ogc/OgcVideoDriver.cpp#L318-L435

[s9-gamesel]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L1164
[s9-settings]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L4003
[s9-rungui]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L1031-L1071
[s9-spinner]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menuart.h#L242-L272
[s9-asynccache]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/widgets/GuiImageAsyncCache.h
[s9-cachecfg]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/menu/menu.cpp#L1242-L1246

[wf-mainloop]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/menu/menu.cpp#L1693-L1790
[wf-btnmgr]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.hpp
[wf-elemtick]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L677-L684
[wf-btntick]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L663-L669
[wf-btnup]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L573-L603
[wf-hide]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L374-L420
[wf-slayout]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.hpp#L151-L201
[wf-target]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L1686-L1855
[wf-covertick]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2558-L2587
[wf-jump]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2605-L2641
[wf-cfinit]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L230-L249
[wf-mirror]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L704-L726
[wf-effect]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L911-L990
[wf-blur]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L768-L901
[wf-loader]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2956-L3016
[wf-wfc]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2643-L2679
[wf-cache]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2681-L2732
[wf-fanart]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/fanart.cpp#L226-L296
[wf-addbtn]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/menu/menu.cpp#L1580-L1600
[wf-hideanim]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/menu/menu.cpp#L1661-L1668

[ul-effects]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui.h#L114-L125
[ul-goround]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_element.cpp#L536-L559
[ul-pulse]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_element.cpp#L610-L630
[ul-grid]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_gamegrid.cpp
[ul-carousel]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_gamecarousel.cpp#L171-L203
[ul-car-consts]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_gamecarousel.cpp#L28-L34
[ul-async]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_image_async.h
[ul-anim]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/banner/Animator.cpp#L197-L241
[ul-thint]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/menu/GameBrowseMenu.cpp#L128

[hbc-gfxh]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.h
[hbc-push]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L456-L504
[hbc-grad]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L316-L352
[hbc-viewh]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/view.h#L8-L24
[hbc-layers]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/view.c#L110-L151
[hbc-focus]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/view.c#L304-L340
[hbc-widgetsh]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/widgets.h#L11-L71
[hbc-bubbles]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/bubbles.c#L36-L224
[hbc-dfade]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/dialogs.c#L138-L170
[hbc-theme]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/theme.c#L137-L166
[hbc-controls]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/controls.c#L102-L170

[w64-nextfocus]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/Component.cpp#L119-L127
[w64-focus]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/FocusManager.cpp#L52-L110
[w64-gfx]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/GraphicsGX.cpp#L479-L577
[w64-guidraw]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/Gui.cpp#L84-L130
[w64-bg]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/Gui.cpp#L159-L175

[ws-lm]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/layermanager.cpp#L94-L109
[wl-gfx]: https://github.com/HTV04/wiilove/blob/6b1d7e73959ef11fa8fc0df2e878ff8576acb0f3/src/wiilove/modules/graphics.cpp#L53-L108
[wl-timer]: https://github.com/HTV04/wiilove/blob/6b1d7e73959ef11fa8fc0df2e878ff8576acb0f3/src/wiilove/modules/timer.cpp#L44-L70

[itg-sm-add]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L780-L811
[itg-sm-pop]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L802-L810
[itg-sm-prep]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L596-L649
[itg-sm-delay]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L718-L779
[itg-swme]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenWithMenuElements.cpp#L108-L137
[itg-trans]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Transition.cpp#L20-L45
[itg-predraw]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L492-L660
[itg-tween-update]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L803-L858
[itg-effclock]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L896-L950
[itg-wheel-pos]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L134-L147
[itg-wheel-update]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L149-L253
[itg-wheel-move]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L352-L378
[itg-nf-draw]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteField.cpp#L836-L1146

[sl-wheel]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/metrics.ini#L1529-L1548
[sl-bg]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/BGAnimations/_shared%20background/Normal.lua#L1-L55
