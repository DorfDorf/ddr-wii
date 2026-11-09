# Graphics plan, 2026-11-09

*The research's [recommended_implementation_plan.md](recommended_implementation_plan.md) (§6 phases, §7 guardrails) with [reusable_implementation_patterns.md](reusable_implementation_patterns.md) §4.4 folded in, checked against `main` 48f35a3 after the target menu flow, and extended with the one thing the research doesn't cover: building the new look inside the game while the current look keeps working. Companion to [code_review_2026-11-09.md](code_review_2026-11-09.md). Markers: **[src]** read in source, **[mem]** from memory (verify), **[exp]** still to be tested, **[measured]** this project's hardware runs.*

---

## 1. What the research got right, and what changed

**Still right:** keep GRRLIB behind `render::`; add a direct-GX `SpriteBatch` with atlases; a glyph atlas instead of per-glyph textures; time-based animation through a PC-tested `core/anim`; a small retained UI layer for menus and an immediate-mode playfield; a scene manager with transitions and worker loading; the §7 budgets (≤ 1,000 GRRLIB draws or ≤ 5,000 batched quads, ≤ 4 Mpx, ≤ 50 texture switches, worst frame < 12 ms, zero gameplay allocations) and the "measure on hardware before and after" rule.

**What the menu flow changed** (both sets predate 2026-11-09):

| Research assumes | Now | Effect on the plan |
|---|---|---|
| Screens: PlayModeSelect, SongSelect, Gameplay (+ results), OffsetCalibration, tests | Title, Players, SongSelect + song settings popup + high scores popup, Gameplay + pause box, Results + name entry, Options (list, Pads page, Test input), exit prompt, OffsetCalibration, three test scenes; `screens/MenuUi` (help strip, hint, exit chord) | Phase 4 ports more screens, but they share one grammar and one strip already. Popups need a modal overlay in the UI model (a parent/child scene, Nintendo pattern 15). "Port PlayModeSelect first" becomes "port Title first" |
| Players join by port in song select; per-player focus | Players are fixed slots chosen before song select; each player has a column cursor in the popup, one shared wheel cursor | Focus is simpler than the research planned: two fixed slots, no join logic in the UI layer |
| No menu sounds | Four generated tones, played only when a press did something | `ui::MenuInput` must keep that rule: the sound belongs to the action, not the key |
| Repeat timer to be written | `core/input/MenuRepeat` exists and is tested | Done; the UI layer reuses it |
| `Judge` event ring to be added (pattern 30) | `Judge::events_` exists with kNote/kEarly/kRow/kHold/kMine | Mostly done; add an `EmptyPress` event if the receptor flash on a stray press is wanted |
| Banner worker to be extended | `screens/SongMedia` exists (banner + preview, latest-request-wins) | Phase 5 adds the LRU and RGB565, nothing more |
| 8×16 debug font everywhere | Still true | Phase 3 is the biggest visible change and the one the owner cares about most (readability from the mat) |

**Order.** The research says 1 and 2 are independent, 3 needs 1, 4 needs 2 and 3. I'd pull text (3) ahead of the animation core (2): it is the first change the owner can see and judge on the TV, and the animation core is portable work that can happen in parallel with anything. I'd also add a phase 0 (consolidation) and move the Look switch into phase 1, where it first does something.

---

## 2. Two looks side by side

### 2.1 Where the switch lives

**Recommendation: Options → ADVANCED → "Look: Classic / New (dev)"**, saved as `[Options] Look=Classic|New` in `config.ini`, applied at once; plus a controller-only hotkey (X, held 1 s, on any menu) that flips it without leaving the screen, for A/B comparison in Dolphin. Not the song settings popup, for two reasons:

- The popup's rows are **per player** (one column each, saved per port). The look is one setting for the whole TV; a per-player row would be the only global row in a per-player table.
- The research's UI layer re-skins **song select itself**, the title, Options and Results. A switch inside the popup can only reach gameplay. Options is the one place that is reachable before any of the screens it affects.

The owner's instinct (a gameplay-only switch) still holds for the early phases: until phase 4 ports the menus, "New" changes gameplay and nothing else, so the setting reads like the popup row would have. A per-screen switch is not worth its UI: the hotkey gives the same comparison with no menu.

### 2.2 How the code carries two looks without doubling the work

The rule: **never fork a screen; fork only draw helpers.** A screen's input handling, state and layout logic stay single. What differs between the looks is how a panel, a cursor, a help strip or a note is painted, and those are already (or should be) small functions.

```
screens/*.cpp        one copy; calls ui:: helpers and the playfield renderer; never GX/GRRLIB
screens/MenuUi (ui::) header(), panel(), choiceRow(), cursorEdge(), help(), hint(), text helpers,
                     palette; each reads render::look() and draws Classic (today's rects + 8x16
                     font) or New (atlas cells + FontAtlas). Mostly data: colours, fonts, metrics.
render/              Draw (GRRLIB), SpriteBatch (GX), Texture/Atlas, FontAtlas, Font (8x16),
                     ArrowArt, Banner; render::Look {Classic, New} set from config
play/                PlayfieldRenderer interface; ClassicPlayfield = Gameplay::drawPlayer moved
                     as it is; NewPlayfield = atlas + batch + core/anim effects
```

- **Classic is frozen.** It is today's code moved into helpers, bug fixes only. Pixel-identical Dolphin screenshots (title, Players, song select, popup, scores, gameplay, pause, Results, Options) guard every PR. The three test scenes stay Classic for good.
- **New is built behind the same helper names.** Phase 1 adds `NewPlayfield` (same picture, new path); phase 3 switches the helpers' text to `FontAtlas` under New; phase 4 replaces the helpers' bodies with `ui::` widgets under New. Classic never changes.
- **What actually doubles:** `drawPlayer` (about 110 lines) and the bodies of ten helpers. Everything else (input, Session, Judge, SongMedia, main.cpp flow) is shared. When New reaches parity on a screen, Classic's branch for it is deleted, or kept as a selectable theme if the owner likes it (decision 7).
- **Fallback.** Classic needs no assets, so it is also the safety net if an embedded asset fails to load or memory gets tight; `render::look()` can fall back and say so in a hint.

---

## 3. Asset pipeline

**Formats.** Ship textures as **TPL** built at build time by `gxtexconv` from PNG sources listed in an `.scf` script (`%.tpl : %.scf` is already in `wii_rules:23`; `gxtexconv.exe` is in the toolchain **[src]**). Pick the smallest GX format that looks right:

| Use | Format | Why |
|---|---|---|
| Arrows, receptors, hold body and caps, mines, glows, judgement words, panel edges, cursor | **I8** (or IA8 where the alpha differs from the grey) | Greyscale shapes tinted by vertex colour: quantisation colours, player colours and grade colours come from the draw call, not the texture; no premultiplied-alpha fringes (the colour comes from the vertex); 1 byte/texel; the Nintendo set's two-colour recolour (pattern 24) works on them |
| Coloured UI art with hard alpha (icons, logo) | RGB5A3 | 2 bytes/texel, 3-bit alpha where translucent |
| Banners (runtime, from SD) | **RGB565** instead of today's RGBA8 | Opaque; halves the cache; `GRRLIB_CreateEmptyTextureFmt(w, h, GX_TF_RGB565)` exists **[src]**; `Banner.cpp` changes one line and its box filter writes 16-bit |
| Anything else | RGBA8 only for render targets, never for art | — |
| CMPR | Later, only if memory gets tight | Needs gxtexconv offline (fine) or a software encoder at runtime (not needed) |

**Atlases.** Two: a `playfield.tpl` (512×512 I8 = 256 KB: 4 arrow quantisations or one arrow plus vertex colour, receptor, hold body and caps, roll variants, mine, glow, judgement words) and a `ui.tpl` (256×256 RGB5A3/I8: 9-slice panel, cursor edge, small icons, pad icon). Power-of-two sides because the hold body tiles with `GX_REPEAT` **[mem/exp]**. Code-generated art (`ArrowArt`) is rendered into the same atlas layout at boot, so the New path can run before any PNG exists.

**Embed, don't load from SD.** `game/data/*.tpl` and `*.ttf` go in through `bin2o` with one Makefile rule per extension (`%.tpl.o %_tpl.h : %.tpl`, `%.ttf.o %_ttf.h : %.ttf`), as CLAUDE.md says; the PNG sources and `.scf` scripts live in `game/assets/` (committed, not embedded). Reasons: the game must look right with nothing but `boot.dol` on the card; the whole set is under 2 MB of MEM1 (`.rodata`), against 24 MB with the code at 2.8 MB today; `GRRLIB_LoadTextureTPL` is zero-copy **[src]**. SD stays for songs. The one exception would be a CJK font (several MB): load from `sd:/ddrwii/fonts/` if present, else fall back to `titleTranslit` (§4).

**Texture memory (New look, all in):** atlases ≈ 0.4 MB, font pages ≈ 0.5 MB (two sizes, I8 512×512), banner cache 32 × 50 KB RGB565 = 1.6 MB, one background 640×480 RGB565 = 0.6 MB if ever added. About 3 MB, against the ≈ 24–28 MB of MEM2 left after the song buffer (research §2 **[derived]**). Add a heap readout to the Test scenes page when phase 5 starts.

**Premultiplied alpha.** Not needed with I8/IA8 art plus vertex colour (there is no colour in the texture to fringe). Only coloured RGB5A3 art would want it; keep that set tiny or premultiply in a 10-line Python step before gxtexconv **[exp]**.

---

## 4. Fonts and licences

FreeType is already linked (through GRRLIB's `pkg-config freetype2`; `TT_RunIns`, `cf2_interpT2CharString` and 120 KB of Brotli dictionary are in the ELF). Its licence is dual FTL / GPLv2; the research chose the **FTL** option, which is GPLv3-compatible and asks for a credit line in the docs and `CREDITS.md` (`portlibs/ppc/licenses/ppc-freetype/` **[src: folder present]**).

**Which fonts.** Two files, both **SIL OFL 1.1** so they can ship with a GPLv3 game with a line each in `game/data/CREDITS.md`:

| Role | Candidates (all OFL) **[mem: verify the licence file in each download]** | Size |
|---|---|---|
| UI text: wheel, panes, popups, Results, Options | **Inter** (tabular figures, very legible at small sizes) or **Noto Sans** (same, wider coverage, bigger file) | one static weight, ≈ 300–600 KB TTF; embed Regular, synthesise bold by drawing twice or ship a second weight |
| Display: title, grade, judgement words, combo | **Exo 2**, **Orbitron** or **Teko** (condensed, arcade feel) | ≈ 100–200 KB |

Not: Simply Love's and ITGmania's fonts (`_wendy`, `Miso`: unclear or non-free terms, and CLAUDE.md says no theme files), Dance Clone's seven TTFs (no licence files, doc 07 §8). **Japanese titles:** a CJK font is 5–15 MB; `SongEntry::titleTranslit` already carries StepMania's transliteration, so the New look shows the transliteration when a glyph is missing (the research's plan) and a CJK font from SD is a later option. Render sizes: prewarm ASCII + Latin-1 at two pixel sizes (about 20 px body, 32 px display) into I8 pages at boot (≈ 50 ms **[exp]**), rasterise other codepoints on the SongMedia worker, never on the frame path.

---

## 5. Where the art comes from

Three sources, used together:

1. **Generated in code** (as now, and the research's "no real art needed" point): panels, rounded corners, gradients, glows, the cursor edge, the pad icon, judgement words rendered from the display font into the atlas at boot. No licence, no files, scales to any TV. This should be most of the UI.
2. **Open-licence noteskin art** for the arrows, receptors, holds, mines and explosions, which are hard to draw well in code. Candidate: **StepMania 5's default `dance` noteskins** (the `default`, `cel`, `metal`... sets), part of the StepMania 5 repository, which is MIT **[mem: verify `Docs/Licenses.txt` and the noteskin folders before use]**. ITGmania's own noteskins are GPLv3 like this project **[mem]**. Either ships with a CREDITS line; neither is a Simply Love theme file, so CLAUDE.md's rule isn't touched. Still banned: Dance Clone's `Media/Game/*.png` and anything from StepMania-Wii.
3. **Owner-made** art for the identity pieces: the logo/wordmark, a background texture, maybe the receptor. Anything drawn by the owner is licence-clean by definition and makes the game theirs.

Style reference stays Simply Love (GPLv3): its colours (already in the code), layout and timings (the research has the numbers), redrawn, no files copied.

---

## 6. TV safe area and readability from the mat

- **Design space 608×456 centred in 640×480** (Nintendo pattern 33, which is doc 06 §11.3's 5% margin). All UI positions anchor to the edges or the centre of that space, as data in `ui::Theme`, not as literals in screens. Playfields stay inside it; text inside the 10% title-safe box (x 64–576, y 48–432), which the hardware checklist confirmed readable.
- **Text sizes.** The 8×16 font is 16 px on a 480-line picture. From the mat (2–3 m) the New look should use ≥ 20 px for anything that must be read while standing (wheel titles, popup values, help strip), ≥ 28 px for judgement, combo, grade and the Players choices, with a 1-px dark outline or shadow (FontAtlas, pattern 25) because banners and backgrounds vary. Measure on the TV: one hardware session with a text-size test page (the research's text stress mode) decides the two sizes.
- **480i.** No 1-px horizontal lines in the New look (they flicker); the pink cursor edge becomes 4 px as now; keep deflicker on at 480i, optional off at 480p **[exp]**.
- **Overscan setting** (Options "TV safe area") stays out until the UI layer scales positions from the theme; after phase 4 it is one multiplier in `ui::Theme`, so it can come back then.

---

## 7. The open research decisions

1. **Approve §4.4 of the Nintendo set: yes, with two trims.** Keep: explicit sort key and shade presets in the batch (two-colour recolour makes I8 art work), `Playhead`/`StepTrack`/reaction curves and a `StateMachine` with time in state in `core/anim`, FontAtlas gradient and outline, `Button` with a clip per state and one shared `Cursor` (the pulsing edge becomes one object), menu cool-down, scene states with the deferred swap one covered frame after the Out transition, beat-crossing events, multi-part cells, the 608×456 space. **Defer:** the per-scene memory arena freed after `GX_DrawDone` (pattern 18): the present path already blocks on `GX_DrawDone` every frame, so a freed texture is never in use, and the heap hasn't shown fragmentation; add it only if the heap readout says so. **Defer:** `Node` clip/scissor until a scrolling region needs it (the wheel is virtualised already).
2. **16:9: not now, design for it.** The UI layer anchors to edges and centre (the "widen the canvas" rule the Nintendo set recommends), so 16:9 later is a projection change plus the anchored layout, not a redraw. Whether to do it at all depends on the owner's TV and Wii setting (decision 4).
3. **Cross-fades: no.** Fades and slides are free; a cross-fade costs an EFB copy (≈ 600 KB RGB565 and one copy per scene change **[exp]**). Revisit after phase 4 if plain fades look cheap.

---

## 8. The plan, as PRs

Each PR builds, passes the PC tests when it touches `core/`, is checked in Dolphin against the budgets with the existing counters, and keeps Classic pixel-identical. Hardware runs are batched (memory note): one after PR 3, one after PR 6, one after PR 8.

| PR | Goal | Main changes | Dolphin check | Budget / guardrail |
|---|---|---|---|---|
| **1. Consolidation** | One copy of everything the screens share; no GRRLIB outside `render/` | See §9 | Screenshots of 9 screens identical before/after; the PC tests unchanged | Counters identical per screen |
| **2. Sprite batch + atlas + the Look switch** | Same picture through direct GX | `render::SpriteBatch` (begin/draw/drawRect/end, sort key = bucket·texture·shade, shade presets Modulate/Additive/TwoColour/FontI8), `render::Texture`/`Atlas` built from `ArrowArt` at boot; `play/PlayfieldRenderer` with `ClassicPlayfield` (moved code) and `NewPlayfield` (batch); `render::Look` + Options row + `config.ini` key + X hotkey; a "batched" mode in `RenderTest`'s ramp | Gameplay looks the same in both looks (screenshots); counters: texture switches per playfield drop to ≈ 1; the ramp runs both ways | ≤ 5,000 quads/frame **[exp]**; the batch restores GRRLIB's GX state in `end()` (verify with mixed scenes) |
| **3. Text** | Real fonts under New | `render::FontAtlas` (FreeType → I8 pages, prewarm ASCII + Latin-1 at two sizes, worker rasterisation for the rest, measure/draw/align, outline); `ui::` text helpers pick 8×16 or FontAtlas by look; a text stress page in the Test scenes; `game/data/*.ttf` + Makefile rule + CREDITS.md; a 4 MB cap and RGB565 for `Banner` | Every menu readable in New; the stress page at 0–2,000 glyphs; Classic identical | Glyph rasterisation never on the frame path; **hardware session 1:** ramp both ways (CPU busy, max frame), text sizes on the TV at 480i and 480p |
| **4. Animation core** | Time-based motion, PC-tested | `core/anim` (Ease, Tween, Track/StepTrack with jumps, Smooth, Spring, Periodic, Playhead, Flipbook, ParticlePool, StateMachine) with 50 vs 60 Hz equivalence tests; `core/ui/WheelMotion` ported from ITGmania `WheelBase`; replace the frame-counted pulses, blink, wheel easing and exit chords in **both** looks (they are logic, not look) | PAL50 and PAL60 runs look the same (the pulse period, the wheel settle, the 2 s chord) | PC tests; no heap use after setup |
| **5. UI layer, scenes, transitions** | The New menus | `ui::` Node/Label/Image/Panel/Button/ListView/ProgressBar, Theme (palette, fonts, metrics, 608×456 anchors), MenuInput (actions from `wii::Input`, repeat, cool-down, the sound rule), one shared Cursor; `ui::SceneManager` with scene states, the deferred swap one covered frame after Out, fade and slide, a shared background; Gameplay's song load on the worker behind a loading overlay. Port in this order, one or two per PR: Title + exit prompt → Players → Options (3 pages) → SongSelect + both popups → Results + name entry → pause box → OffsetCalibration | Transitions at 50/60 Hz; no frozen frame on song load; every hardware-checklist item of doc 05 M6 rerun in Dolphin under New | ≤ 1,000 draws or batched; worst frame < 12 ms; Classic identical (it bypasses `ui::` bodies) |
| **6. Song select polish** | Simply Love-style wheel with art | Banner LRU cache with a byte budget and prefetch ±3 rows (extends `SongMedia`), wheel rows as panels + text with `WheelMotion`, optional blurred banner backdrop; heap readout | Fast scrolling loads nothing until rest; heap stable over a long scroll | ≤ 50 texture switches; **hardware session 2:** a menu session on the TV, stutter report, readability |
| **7. Gameplay polish** | The noteskin and the juice | Real arrow/receptor/hold/mine art in the atlas (decision 2), judgement sheet rendered from the font, Simply Love/noteskin timings as `Track`s (receptor press, explosion, judgement pop, combo pulse), beat flash from the beat clock, repeating hold bodies, `EmptyPress` events, Results animations and a timing histogram | 1P and 2P full songs on autoplay with counters | ≤ 4 Mpx; worst frame < 12 ms; zero gameplay allocations; **hardware session 3:** full songs, max frame time, how it feels |
| 8. *(optional)* | Only if wanted | 16:9 canvas, cross-fades, async display, on-screen timing bars, CJK font from SD | As needed | — |

Dependencies: 1 → 2 → 3; 4 is independent (can run between any two); 5 needs 3 and 4; 6 and 7 need 5 (7 could start after 2 if the owner wants gameplay first). The doc work for each PR: doc 06 §5/§7/§9, CLAUDE.md key decisions ("all animation is time-based through `core/anim`"; "only `render/` and `platform/` call GX or GRRLIB"; "two looks: Classic frozen, New behind `render::look()`"), doc 05 gets a milestone "M7 — Graphics" with the hardware results, README gets the research row.

---

## 9. PR 1 in detail: consolidation

**Goal.** Every piece the screens share lives once, screens stop touching GRRLIB, and nothing visible changes. This is the seam the two looks hang on, and it closes the duplication findings of the review (§1.1, §1.5, #9).

**Changes.**

1. `render/Texture.h`: a `render::Texture` handle (wraps `GRRLIB_texImg*`; `w`, `h`, `destroy()`), and `render::setFilter(bool linear)` wrapping `GRRLIB_SetAntiAliasing`. `Draw.h`, `Font.h`, `ArrowArt.h`, `Banner.h` and `SongMedia.h` stop including `<grrlib.h>`; `SongMedia` frees through the handle. Only `render/*.cpp` and `platform/wii/*.cpp` include GRRLIB or GX headers after this (a grep in the PR description proves it).
2. `screens/MenuUi` grows into the shared toolkit, all moved code, no new behaviour:
   - palette: `ui::kBg`, `kBox`, `kDark`, `kWhite`, `kGrey`, `kBlack`, `kPink`, `kPlayerColour[2]`, `kGold`, `kRed`, `kWarn`, plus `gradeColour()`, `difficultyColour()`, `tierColour()`;
   - text: one `printCentred` (C string and `std::string`/fit overloads), `printRight`, `printFit`;
   - boxes: `header(title)` (the 46 px bar), `panel(x, y, w, h, colour)`, `choiceRow`/`choiceBox` with the pulsing edge (`cursorEdge(x, y, h, now_us)`, time-based), `dim()` (the 0x99 overlay);
   - `ui::Hint` used by Results and the pause box too; `ui::ExitChord` used by Gameplay, Results and the three test scenes; `startPressed`/`zPressed` from MenuUi only; the Players message becomes a `Hint`;
   - lane tables: `ui::kLaneDegrees[4]`, `kLaneButton[4]` (or in `play/Layout.h`), `withAlpha()`.
3. Time-based timing where it is logic: `ExitChord` 2 s by `nowUs()`, the pulses and the Players blink by time, the wheel easing `1 - exp(-dt / 0.045)` (0.35/frame at 60 Hz ≈ τ 45 ms). These are the only pixel changes, and they are invisible at 60 Hz.
4. The two test scenes that draw raw GRRLIB (`RenderTest`, `AudioClockTest`) switch to `render::`, so their counters mean something.
5. Dead code: Players' `lead_ < 0` branch, SongSelect's unused forward declaration.

**Not in this PR:** the Look switch (PR 2), fonts, any new art, the small-fixes bundle from the review (its own PR, which can go first).

**Check in Dolphin.** Capture the nine screens (title, exit prompt, Players, song select, song settings popup, high scores popup, gameplay 2P with pause, Results with name entry, Options list / Pads / Test input) before and after with the same `config.ini` and `scores.txt` and compare pixel for pixel (the pulse frames differ by design; capture on a frame where `sin` is 1, or mask the edge). Counters per screen (`draws`, `tex`, `Mpx` in the stress overlay style) unchanged. PC tests unchanged. Build size unchanged within a few KB.

**Size.** About 400 lines moved, 150 deleted, two new small headers. One afternoon; the screenshot comparison is most of it.

**Then PR 2** (sprite batch + atlas + switch) is where the first hardware number comes from: the `RenderTest` ramp through GRRLIB and through the batch, CPU busy and max frame per step, which decides how much of the 2.6 µs per sprite the batch saves on real hardware before anything else is built on it.

---

## 10. Choices for the owner

1. **Where the switch lives.** My pick: Options → ADVANCED "Look: Classic / New (dev)" + `config.ini` + X held 1 s as a controller hotkey. Alternative: the song settings popup (gameplay only, per-player table).
2. **Art for the notes.** (a) StepMania 5 default noteskin art, MIT **[verify]**, with a CREDITS line; (b) your own arrows; (c) stay code-generated. My pick: (a) for notes/receptors/holds/mines, (c) for all UI, your own for the logo.
3. **Fonts.** UI: Inter or Noto Sans; display: Exo 2, Orbitron or Teko (all OFL **[verify]**). And whether Japanese titles matter now (transliteration fallback) or a CJK font from SD should be planned.
4. **16:9.** Is your Wii set to 16:9 on the Bravia? If yes, 16:9 moves from optional to phase 5's layout rule; if no, it stays optional.
5. **Cross-fades.** My pick: skip; fades and slides.
6. **§4.4 trims.** Defer the per-scene arena and node clipping as in §7, or build them anyway.
7. **Classic's future.** Delete each screen's Classic branch once New reaches parity, or keep Classic as a selectable theme forever (costs a little upkeep per screen change).
8. **Order.** Text (3) before the animation core (4), as here, or the research's order. And whether gameplay polish (7) should jump ahead of the menu port (5).
9. **The start song.** Keep song select opening on "Click 120", remember the last song, or neither (review #14).
10. **Hardware sessions.** Three, after PRs 3, 6 and 8 as above, or fewer.
