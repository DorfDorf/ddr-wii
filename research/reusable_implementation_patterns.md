# Reusable implementation patterns and recommendation

*The last of the Nintendo research set. It turns the findings of [nintendo_game_engine_analysis.md](nintendo_game_engine_analysis.md), [nintendo_gui_architecture.md](nintendo_gui_architecture.md), [nintendo_animation_systems.md](nintendo_animation_systems.md) and [rhythm_game_engine_comparison.md](rhythm_game_engine_comparison.md) into patterns for ddr-wii, and ends with the recommendation (§4). It refines, and doesn't replace, the first set's [recommended_implementation_plan.md](recommended_implementation_plan.md). Markers: **[src]**, **[src, NM]**, **[doc]**, **[mem]**, **[exp]**, **[measured]**. Nothing has been implemented, and the project's architecture is unchanged.*

---

## 1. How to read this catalogue

- Each pattern is an **idea seen in source code**, with links to where it lives, the problem it solves, where it would live in ddr-wii, and a verdict.
- **Every verdict means "re-implement from our own description".** No code comes from the decompilations ([engine analysis §10](nintendo_game_engine_analysis.md#10-licensing)), and the sketches below are new.
- Verdicts:
  - **Build**: worth adding to the plan.
  - **Already planned**: the Nintendo code confirms something in the first set.
  - **Later**: useful once the basics exist.
  - **Skip**: wrong for ddr-wii.
- The code rules still apply. Anything that doesn't need the Wii goes in `core/` and is tested on the PC; GX only in `render/`; SPDX headers on new files; no shipped third-party art.

---

## 2. The pattern catalogue

| # | Pattern | Seen in | Problem it solves | ddr-wii home | Verdict | Wii cost |
|---|---|---|---|---|---|---|
| 1 | Retained pane tree with anchors, inherited alpha, optional clip ∩ parent | J2D, NW4R ([GUI §2](nintendo_gui_architecture.md#2-the-shared-model-a-retained-pane-tree-built-from-layout-data)) | Layouts that survive resizing; fading whole panels; scrolling lists | `ui::Node` | **Already planned**, add anchor, clip, inherit flag | Scissor change per clipped node |
| 2 | Resolve names to typed handles once | NSMBW `LytBase_c` ([GUI §3](nintendo_gui_architecture.md#3-game-side-ui-layers)) | No per-frame lookups; compile-time types | Scene members | **Build** (a habit, not code) | None |
| 3 | Keyframe tracks: Hermite with slopes, step tracks, duplicate-time jumps, hold at the ends | NW4R `lyt` ([animation §3](nintendo_animation_systems.md#3-keyframe-curves)) | Authored motion with sharp cuts | `core/anim::Track`, `StepTrack` | **Already planned**, add jumps and step tracks | Binary search per channel |
| 4 | Clips bound by element and channel; groups | NW4R, NSMBW ([animation §4](nintendo_animation_systems.md#4-clips-and-binding)) | Reusing motion on several elements | `ui::Clip` | **Build** | Bound once at scene open |
| 5 | Playhead modes once / reset / loop-with-intro / ping-pong, reflected bounces, ended/looped/crossed events | J3D `J3DFrameCtrl`, NSMBW, RHF ([animation §5](nintendo_animation_systems.md#5-playheads-frame-controllers)) | Looping idle motion with an intro; one-shot sounds at a time | `core/anim::Playhead` | **Build** | — |
| 6 | Reverse from here / mirrored-frame continuation | RHF `CLayoutAnimation`, Galaxy `ButtonPaneController` | Highlights that reverse without a jump | `Playhead::reverseFromHere` | **Build** | — |
| 7 | State machine with time in state; entry on the first update; deferred switch | Galaxy Nerve/Spine, NSMBW `sState` ([animation §6](nintendo_animation_systems.md#6-state-driven-tweens)) | UI logic and timing in one readable place | `core/ui::StateMachine` | **Build** | — |
| 8 | Tweens as pure functions of time in state + easing | Galaxy `calcNerveEase*` | No tween objects or cleanup | `ease::*`, `StateMachine::rate` | **Build** | — |
| 9 | Reaction pop and converging vibration curves | Galaxy `MathUtil` ([animation §7](nintendo_animation_systems.md#7-easing-and-reaction-curves)) | "Boing" and shake that settles, with two parameters | `core/anim` easing | **Build** | — |
| 10 | Scrub an authored clip from code | Galaxy `WipeRing` | Artist shape, code timing | Transitions | **Later** | — |
| 11 | Button as states (Hidden/Appear/Idle/Focused/Pressed/Disappear), one clip each | Galaxy, RHF ([GUI §4](nintendo_gui_architecture.md#4-reusable-components)) | Consistent buttons and wheel rows | `ui::Button` | **Build** | — |
| 12 | One shared cursor that moves to the focused element | NSMBW `dSelectCursor_c` | Smooth highlight between items | `ui::Cursor` | **Build** | — |
| 13 | Explicit neighbour tables for D-pad focus | RHF `CExBtnPaneManager`; Wii64 (first set) | Predictable menus | `core/ui/Navigation` | **Already planned** | — |
| 14 | Key repeat with delay/rate; stick hysteresis; cool-down after actions | JUTGamePad, RHF ([GUI §5](nintendo_gui_architecture.md#5-menu-input-and-focus)) | Usable menus; no double "back" | `ui::MenuInput` (seconds) | **Already planned**, add cool-down | — |
| 15 | Scene manager: deferred change runs when the fade-out reports done, one fully covered frame later; parent/child scenes | EGG `SceneManager`, `ColorFader`; NSMBW `mFaderBase_c` ([GUI §6](nintendo_gui_architecture.md#6-scenes-loading-and-transitions)) | No visible pop when scenes swap | `ui::SceneManager` | **Build** | — |
| 16 | Scene states Loading → Ready → Active → ScheduleDown → Down | RHF `CScene` | Loading separate from showing; input only to the active scene | `ui::Scene` | **Build** | — |
| 17 | Phased loading steps that may WAIT | NSMBW `sPhase_c` | Never block a frame | `Scene::load` steps | **Build** | Bounded work per frame |
| 18 | Memory per scene, freed whole after the GPU is done | EGG scene heaps (`GXDrawDone` before freeing), RHF memory groups, JKR solid heaps ([engine analysis §7](nintendo_game_engine_analysis.md#7-frame-pipeline-and-memory)) | Fragmentation; freeing textures the GPU still reads | `core/mem::Arena` + Wii wrapper | **Build** | One `GX_DrawDone` per scene change |
| 19 | Transition with a cut time and a screen grab | Mario Strikers Charged `ScreenTransition` | Cross-fades between scenes | `ui::Transition` | **Later** | One full-frame EFB copy **[exp]** |
| 20 | Named faders/wipes of several shapes behind one interface | Galaxy `WipeHolderBase`, NSMBW `dFader_c`, EGG `Fader` | Variety without scene code changes | `ui::Transition` kinds | **Build** (fade, slide), **later** (iris) | — |
| 21 | Draw buckets filled during update, drawn in fixed order | TP `dDlst_list_c`, NSMBW `m2d`, RHF cell layers ([GUI §7](nintendo_gui_architecture.md#7-draw-ordering)) | Update order ≠ draw order | `render::Layers` | **Build** | Fixed arrays, no allocation |
| 22 | Material/texture-keyed merging inside a bucket | J3D `entryMatSort` ([engine analysis §8](nintendo_game_engine_analysis.md#8-rendering-and-texture-management)) | Fewer texture loads | `SpriteBatch` sort key | **Already planned**, make the key explicit | Sort cost ≪ texture switch cost **[exp]** |
| 23 | Named TEV presets with a registry | Mario Strikers Charged `GXMaterialProgram*` | No ad-hoc GX state in callers | `render::Blend`/`Shade` presets | **Build** | — |
| 24 | Two-colour recolour: colour0·(1−tex) + colour1·tex | RHF `CellAnim`, NW4R `CharWriter` | One greyscale texture, many colours | `SpriteBatch` shade mode | **Build** | Same TEV cost as tint |
| 25 | Glyph sheets, texture load only on sheet change, gradients, outline mapping, inline tags | NW4R `ut` ([GUI §8](nintendo_gui_architecture.md#8-text-and-fonts)) | Fast, styled text | `render::FontAtlas` | **Already planned**, add gradient/outline/tags | — |
| 26 | Multi-part cells from one atlas; attach a sprite to another's part | RHF `CellAnim` ([animation §8](nintendo_animation_systems.md#8-cell-animation-multi-part-sprites)) | Layered receptors/explosions from one texture | `render::Cell` groups | **Build** (small) | One batch run |
| 27 | Emitter attached to a UI element | Galaxy `PaneEffectKeeper` | Effects that follow UI | `ParticlePool` helper | **Later** | — |
| 28 | Musical time from the audio actually played | RHF `TickFlowManager` ([rhythm §3](rhythm_game_engine_comparison.md#3-the-song-clock-where-musical-time-comes-from)) | Sync | `SongClock` | **Already built** (finer than RHF's) | — |
| 29 | Beat events fired once when crossed (time-domain remainder carry) | RHF TickFlow ([rhythm §5](rhythm_game_engine_comparison.md#5-scheduling-events-on-the-beat)) | Cues on the beat, exactly once | `core/timing::BeatEvents` | **Build** | — |
| 30 | Judgement outcomes as events (hit/barely/through/empty press) | RHF `CInputChecker` callbacks ([rhythm §8](rhythm_game_engine_comparison.md#8-feedback)) | Feedback decoupled from judging | `core/game::Judge` events | **Build** | Fixed ring |
| 31 | Beat clock phase-locked to a stream | Galaxy `AudBgmTempoAdjuster` | A second clock that drifts | — | **Skip** (ddr-wii reads the stream position) | — |
| 32 | Async display: buffer swap in the draw-done callback; drop a frame if no buffer is free | EGG `AsyncDisplay` ([engine analysis §7](nintendo_game_engine_analysis.md#7-frame-pipeline-and-memory)) | CPU never waits for the GPU | `platform/wii/WiiVideo` | **Later** (the first set's optional own-GX path) | Needs triple buffering: +600 KB **[exp]** |
| 33 | 608×456 safe design space; 16:9 by widening the canvas (with anchors) or narrowing flagged elements | TP, RHF, NSMBW ([GUI §9](nintendo_gui_architecture.md#9-169-and-the-safe-area)) | One layout for 4:3 and 16:9 | `ui` layout rule | **Build** the 608×456 space now; **later** 16:9 | — |
| 34 | On-screen CPU/GPU time bars | JSystem `JUTProcBar`, EGG `ProcessMeter` | Seeing frame budgets on the TV | Debug overlay on top of the existing counters | **Later** | — |

### 2.1 Scene lifecycle, deferred swap and per-scene memory (patterns 15–18)

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later   (sketch only)
namespace ui {
enum class SceneState : u8 { Loading, Ready, Active, Leaving, Done };

class Scene {
public:
    virtual LoadStep load(core::mem::Arena&) = 0;   // called each frame while Loading; Wait / Next / Done
    virtual void update(double uiTime, const MenuInput&) = 0;   // input only while Active
    virtual void draw(render::Layers&) const = 0;               // drawn while Ready, Active, Leaving
    SceneState state = SceneState::Loading;
};

class SceneManager {
public:
    void replace(SceneFactory next, TransitionKind out = Fade, TransitionKind in = Fade);  // stored, not run
    void frame(double uiTime);
    // frame(): advance the current scene's state; when the Out transition reports "covered" for one full
    // frame, destroy the scene, GX_DrawDone(), reset its arena, construct the next scene in a fresh arena
    // (state Loading), run its load steps on later frames, then play In and set it Active.
};
}  // namespace ui
```

- The arena can be a simple bump allocator over a MEM2 block, the JKR solid heap idea. Song select and gameplay each get one, and the reset is O(1).
- Long-lived things stay outside it: config, scores, the song library cache, `SongClock`.
- Worker-thread loads (the existing `SongMedia`) report to `load()`; it returns *Wait* until they finish. That matches doc 06 §7's worker design.

### 2.2 Judgement events and beat-crossing events (patterns 29–30)

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later   (sketch only)
namespace core::game {
struct JudgeEvent {
    enum Kind : u8 { Hit, Miss, HoldOk, HoldNg, MineHit, EmptyPress } kind;
    u8 lane; u8 grade;       // grade for Hit (Marvelous … Boo)
    float offset;            // seconds, signed (early < 0)
    double time;             // song seconds of the press or the miss
};
// Judge appends to a fixed ring per player; play/ reads the new events each frame.
}
namespace core::timing {
// Fires each event once when prevBeat < beat_i <= beat (beats from TimingData, so stops and warps are correct).
template <class F> void forEachCrossed(const float* beats, int n, double prevBeat, double beat, F&& f);
}
```

- PC test: autoplay plus scripted presses produce a known event list.
- `play/` uses the events for explosions, judgement pops, receptor flashes on *EmptyPress*, and hit sounds if they are ever added.

### 2.3 Render buckets, sort key and shade presets (patterns 21–24)

- `render::Layers` holds a fixed number of buckets: background, playfield under, notes, playfield over, HUD, UI, cursor, transition.
- Each bucket collects quads `{sortKey, cell, xform, colour0, colour1, shade}`. `sortKey` is (bucket, texture, shade preset), and stable sorting keeps the order within equal keys.
- `shade` is one of a few named presets: *Modulate*, *Additive*, *TwoColour*, *FontI8*, *FontOutline*. This is Mario Strikers Charged's registry idea applied to the earlier plan's `Blend` enum.

---

## 3. Patterns not to copy

| Pattern | Why not |
|---|---|
| Frame-stepped animation, PAL multipliers (RHF ×1.2), loops paced at 59.94 Hz on PAL (Galaxy) | ddr-wii's rule is time-based animation. The patches show what frame stepping costs ([animation §2](nintendo_animation_systems.md#2-the-time-model-frames-everywhere)) |
| Input read once per frame (RHF) | ddr-wii's SI timestamps are finer and already built ([rhythm §6](rhythm_game_engine_comparison.md#6-input-sampling-and-timestamps)) |
| Timing windows in ticks; anti-mash window shrinking | Breaks ITG scoring, which ddr-wii follows ([rhythm §7](rhythm_game_engine_comparison.md#7-judging)) |
| A bytecode script VM for gameplay | Charts are data. Cues need only beat-crossing events |
| Full material state per element in the playfield (NW4R) | Fine for menus, too slow for hundreds of notes ([GUI §10](nintendo_gui_architecture.md#10-performance-on-the-wii)) |
| Binary layout files and a layout editor | ddr-wii's UI is small. Building it in code (as Simply Love does in Lua) is simpler, and there are no tools to maintain |
| Global singletons for every manager (RHF `g*Manager`) | ddr-wii passes services explicitly to scenes (current `runScene` style), which keeps `core/` testable |
| Phase-locking a secondary beat clock (Galaxy) | Not needed when the stream position can be read directly |

---

## 4. Recommendation

### 4.1 Build ourselves

Everything the game's look and feel depends on, written fresh in ddr-wii's style:

1. **Animation core additions** (`core/anim`, PC-tested), on top of the first set's tweens, smoothing, springs, periodic effects and beat clock:
   - `Playhead` with once / reset / loop-with-intro / ping-pong modes, reflected bounces, crossing events and `reverseFromHere`;
   - `Track` jump keys and `StepTrack`;
   - the reaction-pop and converging-vibration curves.
2. **UI behaviour** (`core/ui` + `ui/`):
   - `StateMachine` with time in state;
   - `Button` with a clip per state;
   - one shared `Cursor`;
   - neighbour navigation;
   - `MenuInput` with repeat and a cool-down in seconds;
   - `Node` anchors, clip and inherit-alpha.
3. **Scenes:** `SceneManager` with deferred swaps one covered frame after the Out transition; scene states; phased `load` steps; an arena per scene reset after `GX_DrawDone`; fade and slide transitions behind one interface.
4. **Gameplay feedback:** `Judge` event ring, beat-crossing events, multi-part cells and two-colour recolouring for notes and receptors.
5. **Rendering:** render buckets with an explicit sort key; named shade presets; `FontAtlas` gradients, outline mapping and a small inline tag set.
6. **Layout rule:** a 608×456 safe design space now; 16:9 by widening the canvas when the owner wants it.

### 4.2 Adapt from open-source libraries

Licences checked in the clones and in devkitPro's licence folder **[doc]**:

| Library | Licence | Use |
|---|---|---|
| **ITGmania v1.3.0** | GPLv3 (`LICENSE`) | Already the source of `TimingData`, judgement rules and life bar; the first set adds tween curves, periodic effects and wheel motion. Port with original notices and a list of changes (CLAUDE.md) |
| **Simply Love** | GPLv3 | Layout, colours and timings as values; no theme files copied (CLAUDE.md) |
| **FreeType** (devkitPro `ppc-freetype`) | Dual: FreeType License or GPLv2 (`portlibs/ppc/licenses/ppc-freetype/`) | Rasterising glyphs into the atlas. Use the FTL option, which works with GPLv3, and add the credit line it asks for to the docs and `CREDITS.md` |
| **libpng, libjpeg-turbo, zlib** (devkitPro portlibs) | Permissive | Already used for banners; keep |
| **GRRLIB** | MIT (`LICENCE.md`) | Keep for video init, present and texture helpers (first set) |
| **libogc** | zlib-style (`LICENSE`) | Direct GX for the sprite batch; ASND, SI, PAD as now |
| **devkitPro `gxtexconv` / TPL** | Part of the toolchain | Art pipeline for atlases (CI8 + palettes are an option for recolouring **[exp]**) |
| **libgui** | README says "GPL"; no licence file at the pinned commit | Design ideas only (layout containers, AA shapes). If code were ever wanted, first confirm the GPL version (v2-only wouldn't combine with GPLv3) |
| **SDL2, libwiisprite** | zlib | Not needed |

None of the six decompilation repositories is on this list. Their third-party parts (ODE, GameSpy, Broadcom BTE) are genuinely open, but ddr-wii doesn't need them.

### 4.3 Nintendo techniques worth reproducing

Ranked by value for a modern-looking 2D rhythm game on the Wii, all re-implemented from the behaviour described in this set:

1. **State machines with time-in-state tweens** (Galaxy). They make every UI element's motion short, readable and testable.
2. **Deferred scene changes with one fully covered frame, and memory per scene freed after the GPU finishes** (EGG, NSMBW, RHF). There is no flicker, no fragmentation, and no freed-texture-in-use bugs.
3. **Keyframe clips with step tracks, jump keys and full playhead modes** (NW4R, J3D), in seconds.
4. **Buttons as clip-per-state machines with mirrored continuation, and one shared moving cursor** (Galaxy, RHF, NSMBW). This is what makes Nintendo menus feel smooth.
5. **Judgement outcomes as events, including empty presses** (RHF), so feedback is rich and decoupled.
6. **Draw buckets plus texture-keyed sorting** (TP, NSMBW, J3D). Predictable layering, fewer texture loads.
7. **Two-colour recolouring in the TEV, and glyph sheets with gradients and outlines** (RHF, NW4R). Polished art from few textures.
8. **Reaction-pop and converging-vibration curves** (Galaxy). The "juice" on judgements, combos and grades.
9. **A 608×456 safe design space with a 16:9 rule** (TP, RHF, NSMBW).
10. Later: **cross-fade transitions with a cut time and screen grab** (Mario Strikers Charged) and **async display** (EGG).

### 4.4 Changes to the earlier plan

These go into the phases of [recommended_implementation_plan.md §6](recommended_implementation_plan.md#6-phased-plan) when it is implemented. The order of the phases doesn't change.

| Earlier step | Add |
|---|---|
| 1. Sprite batch + playfield atlas | Explicit sort key (bucket, texture, shade); named shade presets including *TwoColour*; render buckets |
| 2. Animation core + wheel motion | `Playhead` modes and events, `reverseFromHere`, jump keys and `StepTrack`, reaction/vibration curves, `StateMachine` |
| 3. Font atlas | Gradient text, outline mapping, inline tags |
| 4. UI layer, scenes, transitions, shared background, worker loading | Scene states, deferred swap with one covered frame, phased load steps, arena per scene after `GX_DrawDone`, `Button` clip-per-state, shared `Cursor`, menu cool-down, `Node` anchor/clip/inherit-alpha, 608×456 design space |
| 6. Gameplay polish | `Judge` event ring, beat-crossing events, multi-part cells for receptors and explosions, recoloured notes by quantisation |
| 7. Optional | Cross-fade transitions (screen grab), async display, 16:9 by widening the canvas, on-screen timing bars |

When implementing, update as the first set says: doc 06 §5/§7/§9, CLAUDE.md key decisions, doc 05's milestone, and the README docs table. The table needs a row for both research sets. New decisions this set would add to CLAUDE.md:
- "UI and scene timing use state machines with time in state; scenes swap one covered frame after the Out transition, and free a per-scene arena after `GX_DrawDone`";
- "`Judge` reports outcomes as events; renderers react to events".

### 4.5 Decisions for the owner

1. **Approve** adding §4.4 to the plan (no code changes until the plan's step 1 starts).
2. **16:9:** whether to support it at all; if yes, the recommended rule is to widen the canvas with anchors. It can wait until the plan's step 5.
3. **Cross-fade transitions** cost one full-screen copy per scene change **[exp]**. They are optional, and plain fades and slides are fine to start with.
