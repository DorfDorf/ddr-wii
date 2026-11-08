# Nintendo GUI architecture

*Part of the Nintendo research set (start with [nintendo_game_engine_analysis.md](nintendo_game_engine_analysis.md) for what each repository is, its pinned commit and how far its code is verified; the recommendation is in [reusable_implementation_patterns.md](reusable_implementation_patterns.md)). Markers as there: **[src]**, **[src, NM]** (non-matching source), **[src, Wii unmatched]**, **[doc]**, **[mem]**, **[exp]**. Everything here is described in our own words from the code; nothing is to be copied (licensing: [engine analysis §10](nintendo_game_engine_analysis.md#10-licensing)).*

---

## 1. Summary

- **Nintendo's menus and HUDs are retained pane trees made by a layout tool and loaded from binary files** (J2D `.blo` in Twilight Princess, NW4R `.brlyt` in Wii Sports, Galaxy, NSMBW and Rhythm Heaven Fever) **[src]**:
  - Each pane has a local transform, an anchor, alpha that children can inherit, and an optional clip rectangle.
  - Animations are separate keyframe files bound to panes by name.
  - Code rarely builds UI by hand. It finds panes by name, plays named animations, sets texts and reads state.
- **Each game wraps the layout library in a thin game layer** **[src]**:
  - TP's `CPaneMgr` adds per-pane tweens.
  - NSMBW's `LytBase_c` turns name tables into pointers once, and plays animations by index.
  - Galaxy's `LayoutActor` adds state-machine controllers such as `ButtonPaneController`.
  - Rhythm Heaven Fever's `CLayout`/`CExBtnPane` adds buttons with focus animations and neighbour tables.
  - Mario Strikers Charged has its own timeline-based front end.
- **The reusable components are few and small:** a button as a state machine with one animation per state, a shared selection cursor that moves to the focused pane, counters, yes/no prompts, and named faders and wipes **[src]**.
- **Scene management separates loading from showing:**
  - EGG defers "change scene" until the fade-out has finished.
  - Rhythm Heaven Fever's scenes go through Loading → Ready → Active → ScheduleDown → Down.
  - NSMBW loads in phases that can wait.
  - Mario Strikers Charged's transitions have a "cut time" and can capture the old screen **[src]**.
- **Performance:** NW4R sets every material setting for every pane it draws. That is affordable for menus with tens of panes but not for a note field with hundreds of sprites **[src]**. ddr-wii's earlier split (retained UI, batched immediate playfield) matches what these games do.
- **What ddr-wii should change in its UI plan** is in §11: name-resolved handles, clips per state, a shared cursor, deferred scene changes with one extra covered frame, phased loading, a 608×456 design space with a 16:9 rule, and draw-order buckets.

---

## 2. The shared model: a retained pane tree built from layout data

| Feature | J2D (TP, GameCube era) | NW4R `lyt` (Wii) | Notes |
|---|---|---|---|
| Tree and lookup | `J2DPane` in a `JSUTree`; `search(u64 tag)` finds a pane by an 8-character tag ([`J2DPane.h` L51-238][j2d-pane-h]) | `Pane` with child list; `FindPaneByName(name, recursive)` ([`lyt_pane.cpp` L182-198][lyt-find]) | Names are resolved by game code once at build time in NSMBW (§3) |
| Pane types | `J2DPicture`, `J2DTextBox`, `J2DWindow` (9-slice frame) and their `Ex` variants with materials | `Picture`, `TextBox`, `Window`, `Bounding` (invisible hit area), `Group` (named sets of panes for animation) | Same set: image, text, framed panel, hit box, null pane for grouping |
| Transform | SRT with a rotation pivot (`mRotateOffsetX/Y`) and one of 9 anchors (`mBasePosition`) | Scale → rotate X/Y/Z → translate; global = parent × local ([`CalculateMtx` L220-278][lyt-calcmtx]) | Anchors make "bottom-right corner" layouts survive size changes |
| Alpha | `mColorAlpha = mAlpha × parent / 255` when the pane is set to inherit ([`J2DPane::draw` L296-299][j2d-alpha]) | A pane flagged *influenced alpha* passes its alpha down through `DrawInfo` while its children compute ([L255-277][lyt-calcmtx]) | Fading a whole panel = animating one alpha |
| Clipping | Each pane's clip rectangle is its bounds ∩ the parent's clip, applied as a GX scissor ([L290-294, L325-335][j2d-pane-draw]) | Scissor masks are a game-side addition (NSMBW `SetScissorMask`, [`d_lytbase.hpp` L123][nsmbw-lytbase]) | Scrolling lists clip to their panel |
| Update | `calcMtx`, then `draw` walks the tree, self then children | `Animate` (apply bound curves) → `CalculateMtx` → `Draw`, skipping invisible subtrees on request ([L284-321][lyt-draw]) | Three passes per frame |
| Materials | `J2DMaterial` with TEV blocks | `Material` with textures, TEV stages, colours, blend; `SetupGX` per pane ([Galaxy L644-970][smg-mat]) | §10 |

**Lesson.** ddr-wii's planned `ui::Node` (x, y, scale, rotation, alpha, tint, visible, parent, children) is the same model. It should add three things from here:
- **an anchor** (9 positions), so layouts stay correct when panels resize;
- **an optional clip rectangle** that intersects with the parent's;
- **an "inherit alpha" flag.**

---

## 3. Game-side UI layers

| Game | Layer | What it adds [src] |
|---|---|---|
| Twilight Princess | `CPaneMgr` / `CPaneMgrAlpha` around `J2DPane` ([`d_pane_class.h` L13-116][tp-panemgr]) | Per-pane helpers: translate, scale, global vertex positions. **Frame-counted tweens**: `alphaAnime(frames, from, to, curve)` and `scaleAnime` advance a counter each call and return true when done; `rateCalc` offers linear, t² and (1−t)² ([`d_pane_class_alpha.cpp` L71-120][tp-ratecalc]) |
| NSMBW | `LytBase_c` on NW4R ([`d_lytbase.hpp` L9-145][nsmbw-lytbase]); `m2d` draw list ([`m_2d.cpp`][nsmbw-m2d-draw]) | **Name tables resolved once**: `NPaneRegister`, `PPaneRegister`, `TPaneRegister`, `WPaneRegister` turn arrays of pane names into typed pointers at build time (L50-75). Animations and groups are registered by name, then played by index: `AnimeStartSetup(idx, startAtEnd)`, loop, reverse, `isAnime(idx)` (L80-121). Text boxes are registered with message IDs (L75) |
| Super Mario Galaxy | `LayoutActor` with pane controllers and nerves; `ButtonPaneController` ([`ButtonPaneController.hpp`][smg-btn-h]) | A **state machine per UI element**, each state playing a named animation (§4). Wipes are named layouts in a holder (§6) |
| Rhythm Heaven Fever | `CLayout`, `CLayoutAnimation`, `CExBtnPane`, `CExBtnPaneManager` ([`Layout.hpp`][rhf-layout-h]) | Buttons with default, focus and action animations, select/action sounds, rumble on hover, a controller channel ([L33-124][rhf-btn]), and a manager with **neighbour index tables** ([L126-132][rhf-neighbours]). `CLayoutAnimation` plays forward or in reverse from any frame ([`LayoutAnimation.hpp` L8-75][rhf-layanim]) |
| Mario Strikers Charged | Next Level Games' `FE` front end: `feScene`, `feSceneManager`, `fePresentation`, components (`feButtonComponent`, `feSlideMenu`, `feScrollBar`, `fePopupMenu`, `fePointer*`), and a **timeline** (`TLInstance`, `TLComponent`, `TLSlide`) | Slides, layers, images, texts, components and groups are placed on a timeline with a start time and duration **in seconds**. Code can override an instance's position, colour and UVs without changing the authored asset ([`tlInstance.h` L7-74][msc-tlinstance]); a component switches its active slide, optionally keeping the current time ([`tlComponent.cpp` L12-55][msc-tlcomponent]) |

**Lesson.** The game layer is where UI code lives, and it is thin. Two habits are worth copying:
- **Look panes up once.** NSMBW keeps typed pointers instead of searching names each frame.
- **Play animations by state.** Galaxy and RHF write "enter the Selected state", not "fade alpha from 128 to 255 over 6 frames".

Mario Strikers Charged's seconds-based timeline is the closest to ddr-wii's time-based rule.

---

## 4. Reusable components

| Component | Where | Behaviour [src] | ddr-wii use |
|---|---|---|---|
| **Button as a state machine** | Galaxy `ButtonPaneController` (nerves L13-21; [`calcPointingAnimStartFrame` L228-247][smg-btn-mirror]; [`exePointing`/`exeNotPointing` L273-295][smg-btn-states]) | States Hidden → Appear → Wait ⇄ Pointing ⇄ NotPointing → Decided → DecidedWait → DecidedToDisappear → Disappear, with one animation name per state. When the pointer leaves mid-animation, the "not pointing" animation starts at the **mirrored frame** (length − current), so the highlight reverses smoothly | `ui::Button` (and wheel rows): states Idle / Focused / Pressed / Disabled with a clip per state; mirrored continuation |
| **Button with reversible focus** | RHF `CExBtnPane` ([update L92-160, set state L162-215][rhf-btn-cpp] **[src, NM]**) | Losing focus plays the focus animation **in reverse from where it is**, instead of jumping to the end | Same as above |
| **Shared selection cursor** | NSMBW `dSelectCursor_c` ([`d_SelectCursor.hpp` L12-113][nsmbw-cursor]) | One cursor layout for all menus. `SetPane(pane, layoutId)` moves and sizes it to the focused pane, `SetAlpha` follows that pane's alpha, `Cancel` hides it, and it can fade out | `ui::Cursor`: one highlight that slides between focus targets. It suits Simply Love's moving highlight bar |
| **Neighbour table** | RHF `CExBtnPaneManager_Sub` (initial button and four neighbour index lists, [L126-132][rhf-neighbours]) | D-pad focus moves by lookup, not by geometry | Matches the earlier plan's explicit links (Wii64) |
| **Named wipes and faders** | Galaxy `WipeHolderBase` ([L7-33][smg-wipeholder]): `setCurrent(name)`, `wipe(name, frames)`, `isOpen`/`isClose`/`isWipeIn`/`isWipeOut`; NSMBW `dFader_c` types FADE, CIRCLE_MIDDLE, BOWSER, DRIP_DOWN, MARIO, CIRCLE_TARGET ([`d_fader.hpp` L8-15][nsmbw-dfader]) | One interface, several looks, chosen by name or type | `ui::Transition` kinds: fade, slide, iris; selected per scene change |
| **Counters, prompts, message windows** | Galaxy `CounterLayoutController`, `CountUpPaneRumbler`, `YesNoController`; TP `d_msg_*`, `d_meter2*` | Counting up with a shake and popping on change are layout animations triggered by code | Score/combo counters and the results screen |

---

## 5. Menu input and focus

- **Key repeat, counted in frames.** TP's `JUTGamePad::CButton::update` sets "repeat" bits after a delay, then every *rate* frames, per button mask ([L295-339][jut-pad]). RHF's controller does the same with a held timer ([`Controller.cpp` L117-129][rhf-repeat]).
- **Stick to D-pad with hysteresis.** A direction turns on past 0.5 and off below 0.25 ([`JUTGamePad.cpp` L200-202][jut-stick]). Not needed for a mat, but useful if a GameCube pad's stick is ever used in menus.
- **RHF cool-down and chords.** After A or B is released, that button is ignored for 6 frames ([`Controller.cpp` L19-46, L80-115][rhf-cooldown]). A and B pressed within 4 frames of each other count as one combined input ([L131-195][rhf-chord]).
- **Pointer focus.** Galaxy and RHF hit-test the Wii Remote pointer against pane bounds ([`CExBtnPane` L92-160][rhf-btn-cpp]). ddr-wii's menus are driven by the mat, so this is out of scope.

**Lesson.** ddr-wii's `MenuInput` (earlier plan §4.4) already gives repeat **in seconds** from `wii::Input`. Two things to take from here:
- **a hysteresis threshold**, if analogue sticks are ever supported;
- **an input cool-down after a menu action** (say 100 ms), so a bouncing panel can't trigger "back" twice.

The 20 ms pad debounce already covers bounce in gameplay (doc 06 §3.4).

---

## 6. Scenes, loading and transitions

| Mechanism | Where | How it works [src] |
|---|---|---|
| Scene stack with deferred changes | EGG `SceneManager` ([`eggSceneManager.h` L12-130][egg-scenemgr-h]) | Parent/child scenes (a child scene runs while its parent waits). `changeSceneAfterFadeOut(id)` starts the fade-out and stores the request; `calcCurrentFader` performs the change, sibling change, child creation, reinit or "destroy back to scene X" **when the fader reports it has finished** ([L357-400][egg-scenemgr-calc]) |
| Heaps per scene | EGG `createScene` / `destroyScene` ([L126-204, L286-343][egg-scenemgr-create]) | Each scene gets fresh heaps under its parent's. Destruction waits for the GPU before freeing them |
| Scene life states | RHF `CScene` ([`Scene.hpp` L18-62][rhf-scene-h], [`Scene.cpp` L10-49][rhf-scene-cpp]) | Initial → **Loading** (start loading assets) → **Ready** (assets in, `init` done, drawn but waiting) → **Active** once the fader is idle ([`GameManager.hpp` L27-29][rhf-gm-h]) → **ScheduleDown** (fading out; still drawn) → **Down** (unload) → Dead (deleted; memory groups freed). "Assets ready" is "archive loader idle" ([L61-63][rhf-scene-cpp]) |
| Phased loading | NSMBW `sPhase_c` ([`s_Phase.cpp` L9-28][nsmbw-phase]) | A list of steps called each frame, in order, until one returns WAIT; DONE when the list ends. Loading never blocks the frame |
| Fader state machine | EGG `ColorFader` ([L72-105][egg-fader-calc]); NSMBW `mFaderBase_c` ([`m_fader_base.cpp` L54-72][nsmbw-fader]); TP `JUTFader` | Four states (opaque, clear, fading in, fading out), duration in frames (EGG's default is 20). **The fade-out reports "finished" one frame after it reaches full cover**, so at least one fully covered frame is on screen before the scene underneath changes (EGG L92-96; NSMBW L63-68) |
| Overlap process | TP `d_ovlp_fade*` + `f_op_overlap*` ([`d_ovlp_fade.cpp` L34-109][tp-ovlp]) | The transition is its own process that outlives both scenes: fade out → wait until the new scene has started → fade in → done |
| Wipe scrubbing | Galaxy `WipeRing` ([L48-66][smg-wipering]) | An authored ring animation with its playback rate set to zero; code sets its frame each step from an eased curve. The ring centres on the player's screen position |
| Transition with a cut point and a screen grab | Mario Strikers Charged `ScreenTransition` ([`ScreenTransitionManager.h` L11-35][msc-transition]) | `Update(dt)` in seconds, `IsFinished`, `Time`, **`CutTime`** (when the scenes swap under the transition) and **`ConsumeScreenGrabRequest`** (capture the old screen to a texture so it can be cross-faded). Callbacks report progress, the cut and the end |

**Lesson for ddr-wii's `SceneManager` (earlier plan §4.4):**

1. `replace(next)` stores the request; the swap happens when the Out transition reports done, **one covered frame later** (EGG/NSMBW rule).
2. Scenes have explicit states. *Loading* runs phased steps or waits on the `SongMedia` worker; *Ready* draws under the transition. Input goes only to the *Active* scene.
3. Each scene owns an arena. Freeing it waits for `GX_DrawDone` (EGG L297-298).
4. A transition has a **cut time**, so cross-fades are possible: grab the old frame with `GX_CopyTex` at the cut, and draw it fading over the new scene **[exp: one 640×480 RGB565 copy ≈ 600 KB and well under 1 ms]**.

---

## 7. Draw ordering

| Where | Mechanism [src] |
|---|---|
| TP `dDlst_list_c` | Every frame, objects register draw items into fixed lists: 3D buffers by category, 2D opaque, 2D opaque-top, 2D translucent, 2D after a screen copy, cursor ([`d_drawlist.h` L33-37, L355-386][tp-dlst-h]). Appending is a bounded pointer write ([`d_drawlist.cpp` L1818-1832][tp-dlst-cpp]); the renderer draws the lists in a fixed order |
| NSMBW `m2d` | Each 2D object calls `entry()` each frame; it is inserted into a list sorted by its draw order ([L202-212][nsmbw-m2d-entry]). `draw()` draws the list and clears it; `drawBefore/After/Between(order)` draw sub-ranges so 2D can be interleaved with other passes ([L41-84][nsmbw-m2d-draw]) |
| RHF `CCellAnimManager` | Sprites stay in a list kept in layer order. Two calls draw "layers from N back" and "layers in front of N", so layouts can be drawn between them ([L120-173][rhf-cell-layers] **[src, NM]**) |

**Lesson.** ddr-wii's render layers (earlier [architecture §6](wii_graphics_architecture.md#6-rendering-pipeline)) can be explicit buckets that UI and gameplay append to during update, drawn in a fixed order. Inside a bucket the sprite batch sorts by texture (J3D's idea, [engine analysis §8](nintendo_game_engine_analysis.md#8-rendering-and-texture-management)). Update order then never decides draw order.

---

## 8. Text and fonts

- **Glyph sheets.** NW4R fonts store glyphs as cells in a few large textures (sheets). The glyph index picks a sheet and a cell, with a 1-pixel gap between cells ([`GetGlyphFromIndex` L182-208][ut-glyph]). JSystem's `JUTResFont`/`JUTCacheFont` use the same idea; the cache variant keeps a few sheets resident for large fonts **[src]**.
- **One quad per glyph, but texture loads only when the sheet changes** ([`CharWriter::PrintGlyph`/`LoadTexture` L134-189][ut-print]). Texture coordinates are 16-bit fixed point to keep the vertex stream small.
- **Colour from the shader, not the art:**
  - Vertex-colour gradients, horizontal or vertical ([L191-203][ut-grad]).
  - A two-colour mapping, where the glyph's intensity selects between a "min" and a "max" colour ([L229-265][ut-colmap]). That gives outlined or shadowed text from one intensity sheet.
- **Inline markup through tag processors.** NW4R has `TagProcessor`; RHF has `MyTagProcessor.cpp`, Galaxy `CustomTagProcessor.cpp` and `ReplaceTagProcessor.cpp`. They handle colour changes, icons and substitutions inside a string **[src: files exist]**.

**Lesson.** These confirm the earlier plan's `render::FontAtlas` (FreeType → I8/I4 pages, drawn through the batch). Three additions:
- vertex-gradient text;
- a two-colour (fill + outline) TEV mode;
- a tiny inline tag set (`{c:accent}`, `{icon:start}`) for prompts like "Press START".

---

## 9. 16:9 and the safe area

| Game | Technique [src] |
|---|---|
| Twilight Princess (Wii build) | Layouts are designed 608 units wide. On draw, root panes are scaled horizontally from 608 to the current orthographic width about x = 304, which widens the layout to fill 16:9. Child panes tagged with user data `n_43` get the inverse scale, so they keep 4:3 proportions ([`J2DPane::draw` L270-288, L302-322][j2d-pane-draw] **[src, Wii unmatched]**) |
| NSMBW (NW4R) | With a 16:9 aspect setting, `DrawInfo` gets a "location adjust" horizontal scale of 19/26 (≈ 0.73) ([`m_2d.cpp` L214-222][nsmbw-wide]). Panes flagged *location adjust* are narrowed by it ([`lyt_pane.cpp` L229-232][lyt-calcmtx]), so circles look round again when the TV stretches the picture |
| Rhythm Heaven Fever | Sprites live in a 608×456 orthographic space. On 16:9 the canvas becomes 832×456, and full-screen captures are scaled by 832/608 ([`CellAnimManager.cpp` L157-159, L333-348][rhf-canvas] **[src, NM]**) |

608×456 (or 608×448) is Nintendo's habitual safe design area inside 640×480 **[src: TP, RHF]**.

**Lesson.**
- **Design and position all UI in a 608×456 safe space.** ddr-wii already keeps playfields inside the safe area (doc 06 §11.3).
- **For 16:9 later, pick one rule.**
  - *Widen the canvas* (RHF): more room, nothing distorted, but layouts must anchor to the edges.
  - *Narrow flagged elements* (NW4R): no layout changes, but only flagged panes are fixed.
- With anchors (§2), widening the canvas is the cleaner choice for ddr-wii.

---

## 10. Performance on the Wii

- **NW4R's per-pane cost is high but bounded.** For every picture or window, `Material::SetupGX` loads its textures and TLUTs and sets channels, texture generators and matrices, TEV colours, swap tables, stages and blend mode ([Galaxy L644-970][smg-mat]). That is comparable to GRRLIB's per-draw cost, which ddr-wii measured at ≈ 2.6 µs per sprite **[measured, doc 05]**. A menu of 50–150 panes costs well under 1 ms, which is why these games can afford it.
- **Text is cheaper** because `CharWriter` caches the bound texture ([L169-189][ut-print]).
- **Gameplay is the exception.** Hundreds of notes and explosions per player would cost several milliseconds this way. The earlier plan's batch is still needed for the playfield, and is optional for menus.
- **No per-frame allocation.** Panes, animations and lists are built when the scene opens. NSMBW's draw list and TP's buckets are fixed arrays filled each frame **[src]**.

---

## 11. What ddr-wii should take

Changes to the `ui/` sketch in [recommended_implementation_plan.md §4.4](recommended_implementation_plan.md#44-ui):

| Add to the plan | From | Notes |
|---|---|---|
| `Node` anchors (9), optional clip rectangle intersected with the parent's, and an inherit-alpha flag | J2D, NW4R | Small fields; clipping becomes a scissor in `render/` |
| Look elements up once, then keep typed handles; no per-frame string lookups | NSMBW `LytBase_c` | Scenes keep `Label*`, `Button*` members |
| Clips per element state (Idle, Focused, Pressed, Appear, Disappear), with mirrored-frame or reverse-from-here continuation | Galaxy, RHF | See [animation doc §6](nintendo_animation_systems.md#6-state-driven-tweens) |
| One shared `Cursor` that moves to the focused element | NSMBW | Simply Love's highlight bar |
| `SceneManager`: deferred `replace`, swap one covered frame after the Out transition ends, explicit scene states, phased loading, and an arena per scene freed after `GX_DrawDone` | EGG, RHF, NSMBW | Fits the worker-thread loading already planned |
| Transitions with a cut time and an optional screen grab for cross-fades | Mario Strikers Charged | `GX_CopyTex` at the cut **[exp]** |
| Render buckets filled during update, drawn in fixed order; sort by texture inside a bucket | TP, NSMBW, J3D | Decouples update from draw order |
| Font extras: gradient text, fill-plus-outline TEV mode, small inline tag set | NW4R `ut` | On top of the planned `FontAtlas` |
| A 608×456 safe design space, with a widened canvas plus anchors for 16:9 | TP, RHF | Decide when 16:9 is wanted (owner) |

Not adopted:
- binary layout files and a layout editor: ddr-wii's UI is small enough to build in code, in the style of Simply Love's Lua;
- frame-counted tweens (TP);
- pointer focus.

[j2d-pane-h]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/include/JSystem/J2DGraph/J2DPane.h#L51-L238
[j2d-pane-draw]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J2DGraph/J2DPane.cpp#L246-L344
[j2d-alpha]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J2DGraph/J2DPane.cpp#L296-L299
[jut-pad]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JUtility/JUTGamePad.cpp#L295-L339
[jut-stick]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/JUtility/JUTGamePad.cpp#L200-L202
[tp-panemgr]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/include/d/d_pane_class.h#L13-L116
[tp-ratecalc]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/src/d/d_pane_class_alpha.cpp#L71-L120
[tp-ovlp]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/src/d/d_ovlp_fade.cpp#L34-L109
[tp-dlst-h]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/include/d/d_drawlist.h#L33-L386
[tp-dlst-cpp]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/src/d/d_drawlist.cpp#L1818-L1832
[lyt-find]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_pane.cpp#L182-L198
[lyt-calcmtx]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_pane.cpp#L220-L278
[lyt-draw]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_pane.cpp#L284-L321
[ut-glyph]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_ResFontBase.cpp#L182-L208
[ut-print]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L134-L189
[ut-grad]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L191-L203
[ut-colmap]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/ut/ut_CharWriter.cpp#L229-L265
[egg-scenemgr-h]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/include/egg/core/eggSceneManager.h#L12-L130
[egg-scenemgr-calc]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggSceneManager.cpp#L357-L400
[egg-scenemgr-create]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggSceneManager.cpp#L126-L343
[egg-fader-calc]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggColorFader.cpp#L72-L105
[smg-mat]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/nw4r/lyt/lyt_material.cpp#L644-L970
[smg-btn-h]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/include/Game/Screen/ButtonPaneController.hpp#L1-L66
[smg-btn-mirror]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Screen/ButtonPaneController.cpp#L228-L247
[smg-btn-states]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Screen/ButtonPaneController.cpp#L273-L295
[smg-wipeholder]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/include/Game/Screen/WipeHolderBase.hpp#L7-L33
[smg-wipering]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Screen/WipeRing.cpp#L48-L66
[nsmbw-lytbase]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/include/game/bases/d_lytbase.hpp#L9-L145
[nsmbw-m2d-draw]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/mLib/m_2d.cpp#L41-L84
[nsmbw-m2d-entry]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/mLib/m_2d.cpp#L202-L212
[nsmbw-wide]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/mLib/m_2d.cpp#L214-L222
[nsmbw-cursor]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/include/game/bases/d_SelectCursor.hpp#L12-L113
[nsmbw-dfader]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/include/game/bases/d_fader.hpp#L8-L15
[nsmbw-phase]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/sLib/s_Phase.cpp#L9-L28
[nsmbw-fader]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/mLib/m_fader_base.cpp#L54-L72
[rhf-layout-h]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Layout.hpp#L10-L221
[rhf-btn]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Layout.hpp#L33-L124
[rhf-neighbours]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Layout.hpp#L126-L132
[rhf-layanim]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/LayoutAnimation.hpp#L8-L75
[rhf-btn-cpp]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Layout.cpp#L92-L215
[rhf-repeat]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L117-L129
[rhf-cooldown]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L19-L115
[rhf-chord]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L131-L195
[rhf-scene-h]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Scene.hpp#L18-L62
[rhf-scene-cpp]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Scene.cpp#L10-L63
[rhf-gm-h]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/GameManager.hpp#L27-L29
[rhf-cell-layers]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L120-L173
[rhf-canvas]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L157-L348
[msc-tlinstance]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/include/Game/FE/tlInstance.h#L7-L74
[msc-tlcomponent]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/src/Game/FE/tlComponent.cpp#L12-L55
[msc-transition]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/include/Game/Transitions/ScreenTransitionManager.h#L11-L35
