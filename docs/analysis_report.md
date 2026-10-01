# PufferUI — State Analysis & Improvement Report

Scope: repository at commit `04ba1f8` (PufferUI 0.1.0.1, r86).
Method: **static review only.** I read the public declaration section of
`include/pufferui/pufferui.h` (lines 1–1995), sampled the implementation
(text, draw list, regions, widgets, primitives, SDL3 device), and read
`CMakeLists.txt`, CI, `AGENTS.md`, the README, `docs/`, and representative
examples and tests. **Nothing was built, run or profiled** (no compiler was on
PATH in this session), so every performance item below is a *code-reading
hypothesis to be confirmed with a profiler*, not a measurement. Line numbers
refer to the header as of this commit.

---

## 1. Executive summary

PufferUI is a small, unusually disciplined immediate-mode GUI core. Its best
ideas are real differentiators: layout by **cutting rects** (no solver, no
layout pass), a **contract-violation system** with stable codes that tests
assert on, a **replaceable renderer contract** with a headless `null_device`,
**golden-image + headless + pump tests**, and every example running offscreen
as a CI selftest. For a 0.1 project the engineering hygiene is well above
average.

The main gaps are not correctness; they are **ceilings**:

| Axis | Grade | One-line verdict |
|---|---|---|
| Correctness / safety nets | **A−** | Guards, codes, regression-test culture. Threading claim is wrong (§4.1). |
| Developer experience | **B** | Great docs/examples; weak on build ergonomics, ID boilerplate, magic-number layout, no reusable component layer. |
| Ease of use | **B−** | Smallest program is tiny; real apps need manual heights, explicit IDs, hand-rolled widgets. |
| Performance | **C+ (unproven)** | Solid baseline, but batch-breaking on every rect↔text switch, no text cache, O(n²) hot spots, per-call heap vectors. |
| Flexibility | **B** | Theme cascade + roles + scopes are good; hard-coded limits and a thin widget set cap it. |
| Scalability | **C** | Fixed-capacity arrays (8 popups, 16 panels, 2048 ids, 16 tracks), no culling for non-list content, single-TU monolith, Windows-only CI. |

Top five recommendations (details in §5–§7):

1. **Give solid geometry and text the same texture** (1×1 white texel in the
   glyph atlas) so a frame becomes a handful of draw calls instead of one per
   rect/text alternation. Likely the single biggest performance win.
2. **Introduce a real component convention** (§6): `component(ui&, id/ctx, props) -> result`,
   props/result structs, theme tokens instead of literals, a `components/`
   folder with its own tests. Today, switch/radio/tabs/toast/table exist only as
   copy-paste code inside `patterns.cpp`.
3. **Stop every executable recompiling the 6,000-line implementation**: build
   `pufferui` as a real static library target. ~38 targets currently each
   compile `pufferui_impl.cpp`.
4. **Remove the ID boilerplate** with label-derived default IDs and a
   `ui::id_scope` stack so loops and reusable components are safe by default.
5. **Replace fixed caps and O(n²) checks** (`MAX_FRAME_IDS` linear scan, text
   ellipsis, measure) with growable/hashed structures, keeping the violation
   reporting.

---

## 2. What the project is today

**Shape.** `include/pufferui/pufferui.h` (8,187 lines) holds a ~2,000-line
declaration section and a ~6,200-line `PUFFERUI_IMPLEMENTATION` block;
`src/pufferui_impl.cpp` is a 3-line TU that defines the macro. The renderer is
an abstract `render_device` / `render_surface` pair; `null_device` and an SDL3
device ship. Tests: 96 functions in `tests/test_core.cpp` (5,647 lines), pump
tests, and 17 golden scenes. Examples: 29 atomic programs + tour + demo +
counter. Docs: a 1,370-line README tutorial, `AGENTS.md`, two `docs/` guides.

**Core model.**

- `context` owns state; `ui u(ctx)` is a per-frame stack object.
- Layout = `rect::cut_*` + cursors (`column`, `row`, `track_row/column`,
  `auto_fit_grid`). Cursors clamp and return zero-size rects when exhausted.
- Identity = `uiid` (FNV-1a 64) scoped by `region` RAII objects; widget IDs are
  passed explicitly (`"inc"_id`, `u.local("inc")`, `u.auto_id()`).
- Widgets write back into app fields (`bool&`, `f32&`, `std::string&`,
  `rect&`); "state/view" is an app convention documented in `docs/model_view.md`.
- Overlays (popups, combo menus, tooltips) are deferred to end of frame and
  capture input using **previous-frame rects**.
- Text: stb_truetype, glyph cache in `unordered_map`, 1024² atlas pages,
  kerning via the legacy `kern` table.
- Style: `opt<T>` override structs merged through theme → role → scope →
  instance (`button` only; panel/card have instance overrides but no roles or
  scopes).

**Public widget inventory (core):** button, checkbox, slider_float, number_field,
text_field, combo, tooltip, context_menu, card, progress_bar, panel, popup,
scroll/virtual_list, splitters, dock_space, titlebar. Switch, radio, segmented
control, tabs, toast, command palette, accordion, table and drawer appear only
as **example-level code** in `examples/atomic/patterns.cpp` (742 lines, file
`static` globals).

---

## 3. Strengths to preserve

1. **Rect-cutting is the right primitive.** It is easy to reason about,
   debuggable in a screenshot, allocation-free, and compose-able. Keep it the
   center of the design.
2. **Violations as a first-class feature** (`violation_code`, handler, overlay,
   debug-break) turns API misuse into testable failures. Rare and valuable.
3. **Guardrails are written down** with the reasons (`AGENTS.md`: overlapping hit
   rects, cutting temporaries, rings vs fills). This is institutional memory most
   projects lack.
4. **Renderer contract is small and honest**: capability flags, `null_device` for
   contract completeness, a documented porting guide.
5. **Verification culture**: regression-test-per-fix, golden images with diff
   dumps, every example has `--selftest`, CI runs debug *and* release.
6. **No smart pointers / explicit ownership / no hidden globals** (modulo §4.1)
   keeps the code legible and compile times low.
7. **Declaration section avoids heavy STL** in most places, keeping include cost
   moderate.

---

## 4. Findings

Severity: **H** high (correctness / ceiling), **M** medium, **L** low.
Evidence tags: *[read]* verified in source, *[hyp]* plausible from code,
needs profiling.

### 4.1 Correctness & safety

| # | Sev | Finding |
|---|---|---|
| C1 | **H** | **The "thread-local current context" is not thread-local.** `detail::current_slot()` holds `static context *slot` (≈L2022–2026) while the comment above `report_slice_clamp` (≈L2030) says "thread-local". `column`/`row` have no context pointer, so they rely on this global. Two threads (or two contexts, e.g. an editor with an embedded preview) will cross-report violations and clamps. *[read]* Fix: `thread_local`, and document the rule "one context per thread, or call `set_current_context` around frames". Better: make cursors take/record the context (see R5). |
| C2 | M | `context` is a ~6 KB struct of **public mutable fields** in the header (L1179–1324): hot/active/focus, stacks, deferred-overlay pointers, etc. Users can and will write them; any field change is an ABI/API break. *[read]* Fix: expose a narrow accessor surface and keep the struct in the implementation (you already use `void*` handles for the rest). |
| C3 | M | Deferred menus/tooltips store raw pointers (`defer_labels`, `defer_selected`) that "must outlive the frame" (L1285–1298). A local `const char*[]` or stack `i32` passed to `combo` is a dangling-pointer bug invisible until the end-of-frame draw. *[read]* Fix: copy labels into a per-frame arena (you already copy tooltip text into `defer_tip_text[240]`) and resolve picks via the ID result (`defer_result_*`) rather than a write-back pointer. |
| C4 | M | Per-frame input buffers are `char text_input[128]` (L1093, 1254); a fast IME commit or paste-as-text-events longer than 127 bytes is silently truncated. *[read]* Use a growable string or chunk the events. |
| C5 | L | Glyph cache key `((font*1000003 + size*4)*1000003 + cp)` (≈L3908): `cp` can exceed 1,000,003 (private-use planes), so `(size, cp)` pairs can alias. Practically rare but a real collision. Use a struct key with a proper hash. *[read]* |
| C6 | L | `report_violation` ignores its `fatal` argument (`/*fatal*/`). `PUFFERUI_ASSERT` therefore never asserts. Either honor it or remove the macro to avoid a false sense of safety. *[read]* |
| C7 | L | `CMAKE_*` release uses `/fp:fast` globally. Golden hashes compare rendered output; fast-math differences between compilers/CPUs will make goldens non-portable. Restrict to targets that don't affect golden scenes or compare with tolerance (§5.6). *[read]* |
| C8 | L | `remove_window` invalidates later `window*` pointers (documented, but a footgun). Return stable handles (`window_id`) instead of raw pointers. *[read]* |

### 4.2 Performance *(all [hyp] unless marked — profile first)*

| # | Sev | Finding |
|---|---|---|
| P1 | **H** | **Batching breaks on every solid↔text switch.** Solid shapes are drawn with `tex = nullptr` (`draw_triangles(nullptr, …)`), glyphs with the atlas texture. `dl_prepare` flushes whenever the texture changes (≈L2262–2282), so a typical button (rect, label, ring) produces ≥2–3 `device->draw` calls; a 500-widget screen is on the order of 1,000+ draw calls and 1,000+ `SDL_RenderGeometry` calls (L7686). Fix: reserve a white texel in the atlas and draw all untextured geometry with the atlas texture and that UV. Expected: draw calls fall to ≈ number of clip changes. |
| P2 | **H** | **No text layout cache.** `text()` calls `text_width()` (one hash lookup + one kern lookup per glyph), then loops again doing the same lookups (≈L4005–4068); it also calls `stbtt_GetFontVMetrics` and `stbtt_ScaleForPixelHeight` per call. `button` → `text(…ALIGN_CENTER)` measures every label every frame. `measure_text`/`text_wrapped` measure word by word with a `std::string word` buffer. Fix: frame-local (or LRU) cache keyed by `(font, size, hash(text))` → width/line breaks; precompute per-font scale and metrics when the font/size scope is entered; one pass for measure+emit. |
| P3 | M | **`text_ellipsis` is O(n²)**: it steps back one code point at a time calling `text_width(s.substr(0,n))` which is O(n) (≈L4100–4106). Long paths/filenames in lists (the typical use) degrade badly. Fix: one forward pass accumulating advances, stop at first overflow (binary search on prefix widths is also fine). *[read]* |
| P4 | M | **Duplicate-ID check is O(n²) per frame**: every `region` constructor scans `frame_ids[0..n)` linearly (L3392). At the 2,048 cap that is ~2 M compares/frame; beyond the cap checking silently turns off (with a violation). *[read]* Fix: open-addressed hash set cleared per frame (or sorted-on-demand in debug builds only), and raise/remove the cap. |
| P5 | M | **Per-call heap vectors in primitives.** `draw_sector`/`draw_arc`/`draw_polygon`/rounded-rect paths build temporary `std::vector<vertex>` + `std::vector<i32>` per call (≈L6811, L6984, L7066, L7276). A theme with rounded panels makes thousands of these per frame. *[read for sector/polygon]* Fix: write into the draw list directly (reserve, then emit), or use a reusable scratch buffer on the context. |
| P6 | M | **Rounded rings are expensive geometry**: `rounded_ring` emits 8 feathered bands + 4 sector annuli, each with trig (`sinf/cosf` per segment) every frame (≈L4560–4640). Border-heavy UIs pay this per widget. Fix: cache unit-arc sin/cos tables per segment count; consider a single-quad SDF/shader path when a backend supports shaders; LOD the segment count by radius. |
| P7 | M | **SDL device copies every vertex** into an `SDL_Vertex` scratch on each draw (L7388 scratch, L7671+) and the core copies indices with `push_back` per index (`dl_add`). Fix: pre-size/`resize`+write; let `render_device::draw` take the core's vertex format directly where the backend allows (SDL3 can use strided `SDL_RenderGeometryRaw`). |
| P8 | M | **No general culling.** Only `virtual_list` skips off-screen rows. Anything else inside a `scroll_view` is fully submitted and then scissored. Fix: cheap rect-vs-clip rejection in `draw_*`/`text` (compare against the current clip before emitting), and an `is_visible(rect)` helper for callers. |
| P9 | L | Glyph cache and atlas **never evict**; fractional sizes (`size*4` quantization) and many fonts/sizes grow the atlas monotonically. Add an atlas-full policy (evict LRU page / rebuild) and surface it as a violation/metric. |
| P10 | L | `window` array growth copies whole `window` structs (each contains `window_input` with several KB of key/text arrays) — fine now, but `frame_ids[2048]`, `clip_stack`, etc. inside `context` make it a large, cache-unfriendly object. Split hot per-frame data from cold config. |
| P11 | L | `ui::id_child` hashes 16 bytes byte-by-byte per region. Cheap, but `local("literal")` re-hashes the string each call. Prefer compile-time `_id` literals (already supported) and a two-word mix (e.g. `wyhash`-style) for the combine. |

**Add performance observability** (prerequisite for all of the above): per-frame
counters in `context` (draw calls, vertices, glyph cache hits/misses, text
measures, id-check time) exposed through a `frame_stats` struct and shown in the
development overlay. The demo HUD already separates `ui` vs `sdl` time — move that
idea into the library.

### 4.3 Developer experience

| # | Sev | Finding |
|---|---|---|
| D1 | **H** | **Every executable compiles the whole implementation.** `pufferui` is an `INTERFACE` target; each of ~38 executables lists `src/pufferui_impl.cpp` (CMakeLists.txt L37–170). One header edit rebuilds the 6,200-line impl ~38 times. *[read]* Fix: `add_library(pufferui STATIC src/pufferui_impl.cpp)` with `PUFFERUI_ENABLE_SDL3` as a PUBLIC definition on a `pufferui_sdl3` variant. Keep the installable "single TU" option for consumers who want it. Expect large clean/incremental build-time savings. |
| D2 | M | **Implementation lives inside the header** (`#if PUFFERUI_IMPLEMENTATION`), *and* a `.cpp` includes it, *and* the install step ships that `.cpp` into `include/pufferui/`. Editing implementation code invalidates IDE indexers/PCH for every includer of the header. Split into `pufferui.h` (declarations only) + `src/*.cpp` (several TUs: core, text, widgets, draw, sdl3). See §5.4. |
| D3 | M | **Windows/MSVC/Ninja-only presets and CI.** `CMakePresets.json` hard-codes `cl.exe` and a VS `SegmentHeap.cmake` include; CI is `windows-latest` only. SDL3 is cross-platform, and the code already has `__GNUC__` fallbacks, but they are never exercised. Add Linux (gcc+clang) and macOS jobs, `-Wall -Wextra -Wconversion` and ASan/UBSan presets; you will likely find real issues (the code is full of `static_cast`, which is a good sign, but nothing enforces it). |
| D4 | M | **Design notes live outside the repo**: `AGENTS.md` points at `~/.opencode/plan/*.md` as the source of truth for status and "known limitations". New contributors cannot see it. Move a concise `docs/design.md` + `docs/limitations.md` into the repo and delete the dangling reference. |
| D5 | M | **Tests are one 5.6k-line file** with a bespoke `CHECK` macro, no per-test filter/list, no fixtures. Hard to run one test, to see which failed, or to add tests without merge conflicts. Adopt a tiny runner (register macro + `--filter`, or vendor doctest/Catch2) and split by area: `test_layout`, `test_input`, `test_text`, `test_widgets`, `test_dock`, `test_render`. |
| D6 | M | **Version/documentation drift risk.** The README embeds copied snippets (rule in AGENTS.md: "update the matching snippet"). This is manual. Fix: generate README snippets from marked regions in `examples/*` (`// [snippet:name]`), verified in CI. |
| D7 | L | `PUFFERUI_ASSET_DIR` is an absolute source path baked into binaries (`"D:/myimgui/assets"`). Fine for dev, wrong for redistributed binaries; provide a runtime asset-dir API and an embedded-font fallback (`stb` + a compressed font array) so the default app works with no files. |
| D8 | L | README is 1,370 lines: good tutorial, poor reference. Add a generated API reference (Doxygen or a hand-rolled index of `ui::` methods with one-liners) and a "cookbook" page; slim the README to quick start + links. |
| D9 | L | `clang-format` is enforced for a hand-listed set of files in CI; any new directory is silently unformatted. Use `git ls-files '*.h' '*.cpp'` with excludes. |
| D10 | L | No `clang-tidy`, no `.editorconfig`, no `CONTRIBUTING.md`, no changelog/semver policy (the version string is a four-part `0.1.0.1` but CMake `SameMajorVersion` compatibility implies semver). |

### 4.4 Ease of use

| # | Sev | Finding |
|---|---|---|
| E1 | **H** | **Widget IDs are mandatory, manual and separated from the label.** `button(rect, label, uiid id, role, override)`. Loops need `id_child(base, i)`; forgetting it yields `VIOL_DUP_REGION_ID` (for regions) or *silent* shared state (for widgets, because only regions are dup-checked). The README spends a chapter on IDs. Fix: make the ID optional and derived from label + the current ID scope (ImGui's model): `u.button(r, "Save")`; keep the explicit overload for unlabeled or dynamic labels; add `ui::scope(key)` (RAII, sugar over `region` without clip/area). Extend dup detection to *widget* IDs in debug builds. |
| E2 | **H** | **Magic-number layout.** Examples are full of `col.next(24.0f)`, `r.next(90.0f)`, `26–30px control rows`. Nothing in the library knows a control's natural size, so apps duplicate `example_ui::ROW_H`. Fix: put metrics in the theme (`control_h`, `control_h_small`, `icon_size`, `spacing`, `padding`) and add measure helpers (`u.button_size(label)`, `u.text_size(label)`) so `row::next_fit(u.button_size("OK"))` works. Keep rect-cutting; just stop making users guess sizes. |
| E3 | M | **Overload/parameter sprawl.** `button(rect, label, id, role_id, override)` and `panel(title, rect&, flags, dock_panel, dock_name, override)` take 5–6 positional args of easily-confused types (`uiid` vs `uiid`). Fix: options structs with designated initializers (C++20 is already required): `u.button(r, "Save", {.role = PRIMARY, .enabled = ok})`. |
| E4 | M | **`const char* const*` + count for `combo`/`context_menu`**, and out-params (`i32&`) for results. Modern C++20 users expect `std::span<const std::string_view>`; results as a struct (`{changed, value}`) make call-sites composable and testable. |
| E5 | M | **Widget set is thin in the library and rich in examples.** Anyone building a real app must copy switch/radio/segmented/tabs/table/toast from `patterns.cpp`, which keeps state in file-scope `static` globals. See §6. |
| E6 | M | **Hit-rect overlap is a documented trap** ("first drawn claims the press"). Since the library already tracks `press_claim`, consider z-order by submission *with explicit layers* (`ui::layer(n)`) so overlapping widgets (e.g. a clickable card containing buttons) behave predictably rather than via `enabled=false` workarounds. |
| E7 | L | **Temporary-cut footgun** (`r.content().cut_top(h)` silently discards). Mark `content()` accessors `[[nodiscard]]` and add a `cut_top` overload deleted for rvalues (`rect cut_top(f32) &`). The compiler then refuses the exact bug AGENTS.md warns about. |
| E8 | L | **Bootstrap couples SDL init, window, renderer, context and theme** in `sdl3_app_init` with `void*` SDL handles in the public header. Good for the smallest program, awkward for embedding into an existing engine. Keep it, but make the decoupled path (create_context + device + window) equally short, with a documented "engine integration" sample. |
| E9 | L | **Accessibility, IME candidates, DPI scale, RTL/shaping, localization** are absent from the API surface (grep finds no DPI/content-scale handling). Not needed for 0.x, but each needs a designed seam (see §5.5) before the widget count grows, because retrofitting them into every widget is costly. |

### 4.5 Flexibility

- **Theming is good but uneven.** `button` supports theme→role→scope→instance;
  `panel`/`card`/`scrollbar` support only instance or theme. Generalize to one
  mechanism: `style_for<T>(role)` so every component participates in roles and
  scopes. Fixed arrays (`MAX_BUTTON_ROLES = 16`) should become a small hash map.
- **`merge_style` is hand-written per struct** (repeated `if (o.x.set) …`
  ladders). A single field-list macro or a reflection-lite table removes the
  class of bugs where a new style field is added but not merged.
- **No dark/light/density variants or runtime theme transitions** beyond a whole
  `set_theme`. Add theme tokens (semantic colors: `surface`, `on_surface`,
  `primary`, `danger`, …) separated from component styles, and an easing
  interpolation between themes.
- **Text is single-style, LTR, no shaping.** Kerning uses stb's `kern` table only
  (most modern fonts, including DejaVu, carry GPOS kerning that stb ignores), no
  ligatures, no BiDi, no fallback chains per glyph (`has_glyph` exists but
  falls back to nothing). For a GUI library this is the main flexibility ceiling.
  Offer an optional text backend (HarfBuzz/FreeType or `stb_truetype` + SDF) behind
  a `text_shaper` interface.
- **Renderer contract is flexible** but the vertex type is fixed and there is no
  shader/material hook (needed for SDF rounded rects, blur without render
  targets, custom effects). Add a `material`/`effect` id in the draw command.
- **Custom widgets are first-class** (`interact`, `draw_*`, `region`), which is a
  strength; formalize it as the component convention in §6.

### 4.6 Scalability

- **Hard-coded capacities**: `MAX_POPUPS 8`, `MAX_PANELS 16`, `MAX_DOCK_PANELS 8`
  per node, `MAX_DOCK_DEPTH 8`, `MAX_TRACKS 16`, `MAX_FRAME_IDS 2048`,
  `MAX_CLIP_DEPTH 64`, `MAX_ID_DEPTH 64`, `MAX_WINDOWS 8` (soft), `MAX_STYLE_SCOPES 16`.
  A docking editor with >8 tabs in a node, a spreadsheet with >16 tracks, or a
  large tree view hits these. Overflows are reported (good) but the feature
  degrades. Make the stores growable (`small_vector<T,N>`-style) and keep the
  *defaults* as the current numbers.
- **Dock panel limit and `const char*` names**: panel names must outlive the
  tree; persistence copies into caller-owned `name_storage`. Fine for now, but a
  string-interning table would remove the lifetime rule.
- **Single-threaded, single-context** by construction (C1). Decide and document:
  either "one context per thread" (simple) or make layout/record thread-safe
  (hard, probably unnecessary). Separately, consider **recording draw lists per
  window** and submitting at the end so two windows can build in parallel.
- **Retained costs are all per frame**: no dirty tracking. Add
  `animations_active()`-style "needs redraw" plumbing to the bootstrap so idle
  apps sleep (`SDL_WaitEventTimeout`) instead of spinning at vsync. This matters
  for tools/editors on laptops.
- **Monolith growth**: 6,200 implementation lines in one header block and one
  5,647-line test file will hurt as widgets are added (date picker, tree, table,
  multiline editor). The module split in §5.4 is the prerequisite.
- **Testing scale**: golden hashes are per-platform pixel hashes; they will
  multiply with theme variants and DPI. Add tolerance-based image diff and a
  "structural" layer — record the draw list (rect/text commands) and assert on
  that in headless tests, leaving pixel goldens for a few smoke scenes.

---

## 5. Recommendations (prioritized)

Effort: S ≤ 1 day, M ≈ 1 week, L > 1 week. Impact: ★ to ★★★.

### 5.1 Quick wins (do first)

| ID | Change | Effort | Impact |
|---|---|---|---|
| Q1 | `thread_local` the current-context slot; fix the comment (C1) | S | ★★ |
| Q2 | Static library target for the impl; stop recompiling per example (D1) | S | ★★★ (DX) |
| Q3 | `[[nodiscard]]` on `content()` and deleted rvalue `cut_*` (E7) | S | ★★ |
| Q4 | Linear `text_ellipsis` (P3) | S | ★★ |
| Q5 | Frame stats struct + overlay readout (§4.2) | S | ★★★ (enables measuring) |
| Q6 | Remove or honor `PUFFERUI_ASSERT`'s `fatal` (C6) | S | ★ |
| Q7 | Move plan/limitations docs into `docs/` (D4) | S | ★★ |
| Q8 | Glyph key as struct + fix collision (C5) | S | ★ |
| Q9 | Linux + clang/gcc CI job with `-Wall -Wextra`, ASan/UBSan (D3) | M | ★★★ |
| Q10 | Test runner with `--filter`, split `test_core.cpp` (D5) | M | ★★ |

### 5.2 Performance plan

1. **Measure first**: Q5 counters + a benchmark scene (2,000 buttons, 500 text
   rows, 50 rounded panels). Record baseline numbers in `docs/perf.md`.
2. **White texel in atlas** (P1). Change `draw_triangles(nullptr, …)` call sites to
   a `solid_uv()` helper; keep `nullptr` meaning "device white" for external
   backends, but the SDL device gets the atlas texture.
3. **Text cache** (P2): `text_layout_cache` keyed by `(font,size,hash)` with
   per-frame generation for eviction; single-pass measure+emit; hoist font
   metrics into a `font_scope` resolved once per `text_style`.
4. **Geometry without temporaries** (P5) and **arc tables** (P6).
5. **Culling** (P8) in the lowest layer: `dl_reserve(bounds)` rejects primitives
   entirely outside the current clip.
6. **ID dup-check via hash set** (P4), debug-only full check, release-time cheap.
7. **Idle frames**: bootstrap `wait_events_or_timeout` when
   `!animations_active() && !input_pending` (Scalability §4.6).
8. Re-measure; only then consider SDF text/rounded-rect shaders.

### 5.3 API ergonomics plan

```cpp
// Today
if (u.button(col.next(28.0f), "Save", "save"_id, "primary"_id)) save();

// Target
if (u.button(col.next_control(), "Save", {.role = PRIMARY})) save();   // id from label+scope
for (auto& item : items) {
    auto scope = u.scope(item.id);                                      // safe IDs in loops
    if (u.button(col.next_control(), item.name)) open(item);
}
```

- `ui::scope(key)` (RAII, no clip) + label-derived default IDs.
- Options structs (`button_opts`, `panel_opts`, `field_opts`) via designated
  initializers; retain old overloads as thin wrappers for a deprecation window.
- `span`-based list APIs; result structs.
- Theme metrics (`control_h`, …) + `col.next_control()` / `u.button_size()` (E2).
- Debug-build **widget-ID duplicate detection** (same ID submitted twice in one
  frame with different rects) reported as `VIOL_DUP_WIDGET_ID`.

### 5.4 Source layout (scalability)

```
include/pufferui/
  pufferui.h            // umbrella: includes the pieces below
  core/   types.h  rect.h  color.h  hash.h  ids.h  violation.h
  layout/ cursors.h  tracks.h  grid.h
  render/ device.h  draw_list.h  text.h  atlas.h
  input/  keys.h  input.h  interaction.h
  style/  theme.h  style.h  tokens.h
  ui/     ui.h  scopes.h
  components/  button.h  checkbox.h  field.h  combo.h  tabs.h  table.h ...   // see §6
  backends/sdl3.h
src/        core.cpp  layout.cpp  text.cpp  draw.cpp  input.cpp  widgets.cpp  dock.cpp  backend_sdl3.cpp
tests/      test_layout.cpp  test_input.cpp  test_text.cpp  test_components.cpp  golden/
```

Keep the single-header distribution by *generating* `pufferui_single.h` in CI
(amalgamation script), so the "header + one TU" selling point survives while
contributors work in small files. Build `pufferui` as a real library target plus
an interface "single-TU" package for consumers.

### 5.5 Seams to design before the widget count grows

- **DPI/scale**: `window::scale`; all metrics in logical px; atlas sized by
  `size*scale`; golden tests at 1× and 2×.
- **Text backend interface** (`measure`, `shape`, `rasterize`) with the current
  stb implementation as the default.
- **Input abstraction**: add `pointer_id` and touch/pen; keep the mouse API as sugar.
- **Accessibility tree hook**: each component may `u.a11y({role, name, state, rect})`;
  compiled out by default; a platform adapter consumes it later.
- **Commands/shortcuts**: a keymap registry so components don't read raw `key`s.

### 5.6 Test & quality plan

- Runner with filters; per-area files; parameterize over `null_device`.
- **Draw-list snapshot tests** (text form: `rect 10 10 80 28 #1e222a`) — stable
  across platforms, reviewable in PRs, much cheaper than BMP goldens.
- Golden compare with per-channel tolerance and platform-tagged hashes; drop
  `/fp:fast` for test builds.
- Fuzz the UTF-8 decoder, the dock-tree restore parser (`dock_restore_tree`), and
  the text field edit pipeline (random key/IME streams under ASan).
- Benchmarks in CI (non-gating, trend-tracked).

---

## 6. Recommended reusable component convention

**Goal:** make a new component something a contributor can write, test, theme and
reuse without reading the core. Everything here works with the library's existing
rules (rect cutting, IDs, no smart pointers, `lower_snake_case`).

### 6.1 The rules (one page)

1. **A component is a free function (or a small struct with `operator()`) in
   `namespace pui::comp`.** Signature:

   ```cpp
   result component(ui& u, rect area, const props& p);
   // or, with app-owned state written back:
   result component(ui& u, rect area, T& value, const props& p = {});
   ```
   - `area` is **given**, never allocated by the component. The caller decides
     placement with cursors; the component only paints and interacts inside it.
   - A component may **report its natural size** via a sibling function
     `vec2 component_size(ui&, const props&)` so callers can size rows
     (`row::next(component_size(...).x)`), removing magic numbers.
2. **Props are one struct with designated-initializer-friendly defaults.**
   No more than ~8 fields; every field has a sensible default. Include
   `uiid id = 0` (0 ⇒ derived from label + scope) and `bool enabled = true`.
3. **Result is a struct, not a bool out-param**, with `[[nodiscard]]` and an
   `explicit operator bool` for the primary event:
   ```cpp
   struct button_result { bool clicked=false; interaction in{}; explicit operator bool() const { return clicked; } };
   ```
4. **State is owned by the app or keyed by ID**: value state (`bool&`, `f32&`,
   `std::string&`) is passed in and written back; transient UI state (hover,
   caret, scroll) is keyed by `uiid` in the context. **No file-scope statics in
   components.**
5. **Identity**: the component opens an ID scope from `props.id` (or the label) so
   its internal widgets use `u.local("part")`. Two instances never collide.
6. **Style = tokens + one override struct.** A component reads a
   `component_style` resolved by `u.style<component_style>(props.role)`
   (theme → role → scope → `props.style` override, using `opt<T>`). It never
   hard-codes colors or sizes; literals are allowed only as theme defaults.
7. **Structure: part → state → paint.** Inside the function, in order:
   (a) resolve style; (b) `interact()` once per hit rect (disjoint rects, per the
   guardrail); (c) derive visual state enum (`NORMAL/HOVERED/ACTIVE/FOCUSED/DISABLED`);
   (d) paint from the style + state using primitives; (e) set cursor; (f) return.
8. **Containers are RAII scopes** named `*_scope`, non-copyable/non-movable,
   exposing `content()` by value with `[[nodiscard]]`, and pop everything in the
   destructor (same as `panel_scope`).
9. **Animation is opt-in via the style's `transition`**, using keyed
   `animate*` calls; honors `reduced_motion`.
10. **Every component ships with**: a header in `include/pufferui/components/`, an
    entry in the component catalogue (`docs/components.md`), a headless test that
    asserts behavior and **zero violations**, a draw-list snapshot test, and an
    atomic example (`examples/atomic/<name>.cpp`) with `--selftest`.

### 6.2 Naming & file layout

| Thing | Convention | Example |
|---|---|---|
| Namespace | `pui::comp` (core widgets may stay on `ui::` as sugar) | `comp::switch_toggle` |
| Function | noun or noun_verb, `lower_snake_case` | `segmented`, `tab_bar`, `toast_host` |
| Props struct | `<name>_props` | `switch_props` |
| Result struct | `<name>_result` | `switch_result` |
| Style struct | `<name>_style` + `<name>_override` (existing pattern) | `switch_style` |
| Scope (RAII) | `<name>_scope` | `drawer_scope` |
| Theme role keys | `ROLE_<NAME>` constants of type `uiid` | `ROLE_PRIMARY`, `ROLE_DANGER` |
| Files | one header per component, optional `.cpp` | `components/switch.h` |

### 6.3 Worked example — a switch (currently a snippet in `patterns.cpp`)

```cpp
// include/pufferui/components/switch.h
namespace pui::comp {

struct switch_style {
    color track_off, track_on, knob;
    f32   width = 38.0f, height = 22.0f, knob_pad = 3.0f;
    transition anim{0.12f, easing::EASE_OUT};
};
struct switch_override { opt<color> track_off, track_on, knob; opt<f32> width, height; };

struct switch_props {
    uiid  id = 0;              // 0 -> derived from label + scope
    uiid  role = 0;            // theme role (e.g. ROLE_DANGER)
    bool  enabled = true;
    switch_override style{};   // per-instance override
};
struct [[nodiscard]] switch_result {
    bool changed = false;
    interaction in{};
    explicit operator bool() const { return changed; }
};

vec2          switch_size(ui& u, std::string_view label, const switch_props& p = {});
switch_result switch_toggle(ui& u, rect area, std::string_view label, bool& on,
                            const switch_props& p = {});
} // namespace pui::comp
```

```cpp
// usage
row r(col.next_control(), u.th().spacing);
if (comp::switch_toggle(u, r.next(comp::switch_size(u, "Wi-Fi").x), "Wi-Fi", s.wifi))
    apply_wifi(s.wifi);
```

Implementation skeleton (inside the `.cpp` or header-inline):

```cpp
switch_result switch_toggle(ui& u, rect area, std::string_view label, bool& on, const switch_props& p) {
    auto scope   = u.scope(p.id ? p.id : u.local(label));                  // 5: identity
    const auto s = merge_style(u.style<switch_style>(p.role), p.style);    // 6: style cascade
    const rect track = area.cut_left(s.width).align_middle(s.height);
    interaction in = u.interact(u.local("hit"), area, p.enabled);          // 7b
    const bool changed = in.clicked && p.enabled;  if (changed) on = !on;
    const f32 t = u.animate(u.local("t"), on ? 1.f : 0.f, tween{s.anim.duration, s.anim.curve});
    u.draw_rounded_rect(track, lerp(s.track_off, s.track_on, t), track.h * 0.5f);   // 7d
    u.draw_rounded_rect(knob_rect(track, s, t), s.knob, track.h * 0.5f);
    u.text(area, label, p.enabled ? u.th().text : u.th().text_dim, ALIGN_LEFT);
    if (in.hovered && p.enabled) u.set_cursor(CURSOR_HAND);                // 7e
    return {changed, in};
}
```

### 6.4 Composition patterns (how components build on components)

- **Composite** components are just functions calling other components within
  their own ID scope (e.g. `labeled_field` = `text` + `text_field` +
  `error_text`). They forward a `props` subset rather than re-declaring fields.
- **Slots via callbacks**: for header/body/footer customization use
  `function_ref<void(ui&, rect)>` (no allocation, already in the library) —
  `card_scope` for ownership-free, RAII for the common case.
- **Collections** (`list`, `table`, `tree`) take data as `span`/iterator plus a
  `function_ref<void(ui&, rect, i32 index)>` row callback and use
  `virtual_list` internally; row identity comes from a user key
  (`function_ref<uiid(i32)>`), never from the index alone.
- **App-level screens** follow `docs/model_view.md` (state, `update_view()`,
  `draw_<screen>(ui&, const view_t&, state_t&)`). Components stay *stateless
  with respect to the app*: they take values and return events; the screen
  translates events into model writes.
- **Composition budget**: if a component needs >8 props or >2 nested scopes,
  split it. Prefer many small components over a configuration-heavy one.

### 6.5 Definition of done for a component (checklist)

- [ ] Header + props/result/style structs follow §6.2
- [ ] No literals except theme defaults; reads `ui::th()`/style only
- [ ] No file-scope statics; all state passed in or keyed by `uiid`
- [ ] Disabled, hover, active, focus (keyboard) and `reduced_motion` handled
- [ ] Sets cursor; keyboard operable (Tab focus, Enter/Space/arrows as applicable)
- [ ] Works inside a `scroll_view`, `panel`, popup and dock panel (clip, ID scope)
- [ ] `*_size()` provided (or documented as fill-only)
- [ ] Headless test: behavior + `violation_count == 0`
- [ ] Draw-list snapshot (and a golden only if visually novel)
- [ ] Atomic example with `--selftest`; row in `docs/components.md`

### 6.6 Migration path

1. Land §5.3 primitives (`scope`, options structs, theme metrics).
2. Promote existing example-level widgets to `components/` one per PR, starting
   with the ones `patterns.cpp` already proves: **switch, radio, segmented, tabs,
   toast host, accordion, table, drawer, command palette.**
3. Re-express core widgets (`button`, `checkbox`, `combo`, …) through the same
   convention; `ui::button(...)` remains a one-line forwarder for compatibility.
4. Generate `docs/components.md` from headers (props tables + embedded
   `--screenshot` output from each atomic example).

---

## 7. Roadmap

| Phase | Duration (est.) | Contents | Exit criterion |
|---|---|---|---|
| **0 — Foundations** | 1–2 wks | Q1–Q10, frame stats, benchmark scene, library target, Linux CI | Green on 2 OS × 2 compilers; baseline numbers committed |
| **1 — Performance** | 2–3 wks | White-texel batching, text cache, no-temporary primitives, ID hash set, culling, idle sleep | Benchmark shows ≥3× fewer draw calls; frame time deltas recorded |
| **2 — Ergonomics** | 2–3 wks | `scope`, label IDs, options structs, theme metrics + `next_control`, `[[nodiscard]]` guards | README quick start has no `_id` literals; examples shrink measurably |
| **3 — Component layer** | 3–4 wks | §6 convention, promote 9 pattern widgets, `docs/components.md`, draw-list snapshot tests | Every component meets §6.5 |
| **4 — Structure** | 2 wks | Split header/impl into modules, amalgamation script, growable stores | Single-header artifact still passes all tests |
| **5 — Reach** | open-ended | DPI, text backend interface, a11y hook, touch/pointer ids, theme tokens | Design docs + minimal implementations behind interfaces |

Risks: (a) P1/P2 changes will invalidate every golden image — land them in one
PR with reviewed regenerated BMPs, as `AGENTS.md` requires; (b) label-derived IDs
change state identity for existing apps — keep the explicit-ID overload and add a
debug warning when a label-derived ID collides; (c) module split is noisy in
blame — do it in a mechanical PR with no behavior change.

---

## 8. Evidence index

| Claim | Where |
|---|---|
| Non-thread-local slot vs. "thread-local" comment | header ≈L2022–2030 |
| Dup-ID linear scan, cap 2048 | L57, L1204, L3392–3412 |
| Draw-list flush on texture change | ≈L2250–2303 (`dl_prepare`, `dl_flush`) |
| Text: measure then emit, per-glyph hash lookups | `ui::text_width` ≈L4005, `ui::text` ≈L4022–4068 |
| Quadratic ellipsis | `ui::text_ellipsis` ≈L4088–4110 |
| Word-by-word measuring with `std::string` | `ui::measure_text` ≈L4132–4185 |
| Per-call vectors in primitives | ≈L6811, L6984, L7066, L7276 |
| Solid geometry uses `nullptr` texture | `ring_hband/ring_vband` ≈L4558–4575 |
| `SDL_RenderGeometry` per flush + vertex copy | L7388 (scratch), L7671–7690 |
| Glyph key composition | `get_glyph` ≈L3903–3912 |
| Public `context` struct | L1179–1324 |
| `button` signature (5 positional params) | L1504 |
| Impl compiled per executable | CMakeLists.txt ≈L37–170 |
| Windows-only presets/CI | CMakePresets.json; `.github/workflows/ci.yml` |
| External plan docs referenced | `AGENTS.md` ("Design plan & status") |
| Example-level widgets with static state | `examples/atomic/patterns.cpp` L14–40 |

*End of report.*
