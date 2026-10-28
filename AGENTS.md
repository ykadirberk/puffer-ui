# AGENTS.md

## Project

**PufferUI** — a C++20 immediate-mode GUI library with a "pure rect-cutting"
layout engine. The library is one header (`include/pufferui/pufferui.h`: the
declaration section, plus a `PUFFERUI_IMPLEMENTATION` block that includes the
`include/pufferui/impl/*.inl` slices in order) and exactly one implementation
TU (`src/pufferui_impl.cpp`). Edit implementation code in the slice that owns
its section; `tools/amalgamate.ps1` rebuilds the single-file header.

- `examples/pui_demo.cpp` — SDL3 demo (target `pui_demo`), two windows, dock, HUD.
- `examples/pui_counter.cpp` — small model/view example (target `pui_counter`).
- `tests/test_*.cpp` — headless core tests (target `pui_core_tests`, no SDL),
  split by area (layout, input, text, widgets, dock, render, components, motion);
  `tests/test_core.cpp` is the runner. A test is `PUI_TEST(test_name) { tf_env
  env; ... }` (`tests/test_util.h`): it registers itself, `tf_env` creates the
  context, installs the violation capture and asserts zero violations on exit
  (`env.expect_clean = false` for tests that provoke guards on purpose).
- `tests/golden/test_golden.cpp` — golden-image tests (target `pui_golden_tests`,
  SDL offscreen + software renderer).
- `tests/test_performance.inl` — private cache/allocation regressions and `--bench`
  workloads (`pui_perf_tests`), included at the end of the same implementation TU
  only with `PUFFERUI_INTERNAL_TESTS`. `pui_perf_pixels` checks 16 pre-optimization
  pixel fingerprints; `tools/compare_showcase.ps1` compares two showcase binaries.
  `tests/test_geometry_performance.inl` adds original-stream references for rounded
  fan topology and bulk index appends, including transforms and geometric growth.
- `pui_memory_tests` adds private vertex-growth, SDL color-prefix and portable
  D3D11 arena/upload regressions (`tests/test_memory_{performance.inl,upload.cpp}`).
  It compiles the implementation TU with `PUFFERUI_MEMORY_TESTS`; the existing
  tests and golden oracles remain independent and unchanged.
- `pui_preparation_tests` adds separate core/SDL preparation, exact stream,
  bounds/isolation and actual 180-frame 5000-tile Spiral allocation regressions
  (`tests/test_geometry_preparation.inl`). Existing tests stay unchanged.
- `tests/test_core_performance.inl` adds seven regressions for long-run kerning,
  exact spring integration, 5000 rects, bounded sector topology, exact state TTL,
  empty ID tables and nested text-area scratch. `pui_core_bench` compiles the
  actual implementation with eight workloads in `tests/bench_core_performance.inl`;
  `tools/compare_core.ps1` retains alternating before/after runs and allocations.
  `pui_core_perf_pixels` checks 24 pre-fix frames covering text-area gutters,
  long UTF-8 text, sector outlines, rect motion, raw text sizes, gamma and DPI.
- `tests/test_showcase.cpp` compiles the actual showcase helpers: `pui_showcase_tests`
  runs eleven geometry/layout/allocation/SDL-upload/paint regressions; `--pixels`
  checks four pre-fix Spiral golden frames at 1150 tiles; `--pixels --tiles 5000`
  checks four more. `--peak-pixels` checks ten pre-r113 5000-tile frames around
  the second Add and its geometry peak. `--bench <renderer> --tiles 1150|5000`
  emits 180 frames
  with three actual keyboard Add activations. `tools/compare_spiral.ps1` alternates
  baseline/candidate runs and compares full BMPs and per-frame geometry counts;
  its `-Tiles` option selects the matching workload; all fourteen native capture
  frames are compared. Painting regressions live in
  `tests/test_showcase_paint.inl`; existing test bodies and golden references stay fixed.
- `cmake/PufferUISdlD3d11.cmake` applies the `src/sdl_d3d11_{arena,buffer,geometry,draw}.inl`
  slices to a generated copy of the pinned SDL D3D11 source. It verifies the whole
  original file and upload function hashes. Indexed triangles retain vertex
  arithmetic and primitive order; renderer-owned upload slots separate sizes.
  Keep the submodule clean. System SDL and other renderers use their upstream code.
- `vendored/` — third-party code: `SDL/` (SDL3), `stb/stb_truetype.h`.

### Optional Vulkan backend

- Enable with `-DPUFFERUI_WITH_VULKAN=ON`; link only `pufferui::vulkan` for that
  executable. Core/SDL variants remain independent of Vulkan. Installed native
  consumers request `find_package(pufferui CONFIG REQUIRED COMPONENTS vulkan)`.
- Native SDK types live in `include/pufferui/vulkan.h` /
  `impl/vulkan_native.inl`, outside the main declaration section. Implementation
  belongs in `impl/vulkan_{dispatch,device,memory,resources,draw,surface,cache}.inl`
  and `impl/sdl3_vulkan_host.inl`, through the same single implementation TU.
- SDL still owns window/input policy; Vulkan uses native surfaces and no
  SDL_Renderer. Borrowed instance/device/queue and producer image/view remain
  caller-owned. Release external images and wait for their completion value
  before producer reuse/destruction. See `docs/vulkan.md` for the full contract.
- `pui_vulkan_tests`, `pui_vulkan_external_tests`, `pui_vulkan_cache_tests`,
  `pui_vulkan_internal_tests` and `pui_vulkan_fault_tests` run offscreen;
  `pui_vulkan_interaction_tests` compares 180 actual text/caret/scroll/atlas/
  popup/blur/DPI/gamma/target-recreation frames with cache off/on (also --staged).
  `pui_vulkan_wsi_tests` adds SDL lifecycle/overlay/resize/DPI/zero-extent coverage.
  `pui_vulkan_showcase_tests` checks the actual showcase's startup VSync/HUD
  agreement and off/on transitions; `--visible` is a separate bounded pacing probe.
  `pui_vulkan_spiral_tests` compares all 180 frames at 1150/5000 items; default
  cache/reference and `--upload-reference` direct/staged comparisons are exact.
  `--staged` also exercises the staged cache/reference route.
- `python tools/run_tests.py --vulkan --format [--release]` includes native tests
  and installed core/SDL/Vulkan consumers. Cross-backend visual parity is not
  assumed; existing tests/goldens remain unchanged, and Vulkan cached/uncached
  equality must remain exact. Never use `--update` to hide a backend difference.
- GLSL lives in `src/shaders/vulkan/`; `tools/compile_vulkan_shaders.py` generates
  checked-in SPIR-V plus a compiler/source manifest. Ordinary consumers do not
  run the shader compiler. INTEGRATED is the Vulkan device default. SDF_RASTER
  and COMPUTE_SDF modes use analytic shapes; compute performs ordered GPU tile
  binning and qualified RGBA8 suffix painting, with complete raster replay for
  other pixels. Exact caches and compute painting default on; native observation
  and adaptive compute remain explicit opt-ins. See
  `docs/vulkan_gpu_painter.md` for the exact eligibility and lifetime contract.
- Fresh Vulkan configurations default `PUFFERUI_WITH_HARFBUZZ=ON`; explicit OFF
  remains supported. This adds pinned HarfBuzz shaping and upstream Slug GPU
  outline rendering to the Vulkan variant only. `vector_text` requires an SDF
  painter. Core/SDL and existing bitmap goldens remain independent. See
  `docs/harfbuzz.md`; paragraph bidi and cluster-aware editing are not implied.
- `INTEGRATED` combines packed native operands, exact output/function/glyph caches,
  producer dependencies, shape groups and a guarded common-opacity scope. The
  Vulkan library, SDL Vulkan factory and showcase select it by default, with
  vector text when HarfBuzz is built. Explicit `--vulkan-painter` overrides select independent diagnostic
  configurations. Changed frames run GPU binning/SDF/Slug immediately; certified
  unchanged frames skip the painter. `native_observation=true` selects the old
  native-first policy and its deferred fine preparation. `pui_vulkan_identity_tests`
  covers that diagnostic policy's cold admission, glyph-tag
  churn and that automatic transition in direct/staged configurations.
  `pui_vulkan_unified_showcase_tests` covers the actual default and six overrides.
  Group/alpha/producer/LOAD/glyph-source tests retain independent native oracles.
- `pui_vulkan_default_tests` exercises the public factories and 24 exact default/
  native frames in direct/staged modes. Legacy diagnostic fixtures explicitly
  select `tests/vulkan_test_options.h`; their assertions and golden bytes stay fixed.
  `examples/tutorial/vulkan_manual.cpp` and tutorial chapter 10 show explicit SDL/
  Vulkan host/device/surface/context ownership without an example bootstrap.
- `pui_vulkan_compute_default_tests` proves the actual default on all 180
  5000-Spiral frames against native SDF: GPU work on frame zero and each change,
  Slug uploads, whole-image reuse on idle frames, direct/staged paths, and no
  draw allocations. Legacy observation fixtures explicitly pin their policy.
- `pui_vulkan_command_tests`, `pui_vulkan_binning_tests`,
  `pui_vulkan_compute_tests`, `pui_vulkan_sdf_tests` and
  `pui_vulkan_sdf_cache_tests` cover command ABI/order, actual GPU lists,
  calibrated finite blending, SDF isolation, exact cache comparison and budgets.
  `pui_vulkan_compute_spiral_tests` compares all frames using the actual showcase;
  `--sdf --vector-text --capture NEW_DIR` freezes a separate new raster reference,
  `--compute --vector-text [--cache] --reference DIR` compares against it, and
  `--adaptive` permits eligibility-based raster selection. `--long-run` runs
  3600 frames with 60 actual Add activations. Never overwrite a captured golden.
  `pui_harfbuzz_interaction_tests` covers 180 vector-text interaction frames.
  `pui_vulkan_painter_bench` and `tools/compare_vulkan_compute.py` retain separate
  readback-free 180-frame timings; do not overlap measurements with builds/tests.
- Prepared Vulkan resources live in `impl/vulkan_preparation.inl`: shared device
  kernels, bounded per-slot arenas/descriptors and an append-only resident glyph
  buffer. `pui_vulkan_preparation_tests` includes actual 5000-item direct/staged/
  adaptive allocation checks, history/readback lifetime, runtime cache transitions,
  budget rejection, copy/telemetry oracles and exhaustion fallback regressions.
  `tools/compare_vulkan_preparation.py` freezes binaries and retains every frame.
- `pui_vulkan_locality_tests` adds cold observation tape, contiguous fine-job
  ownership/rebuild/budget regressions and actual GPU function-admission checks
  against the original scalar reference. `tools/compare_vulkan_locality.py`
  alternates identical production/fine configurations, retaining cold/peak frames
  in 5000-item moving/idle/local direct/staged scenarios. See
  `docs/perf_vulkan_compute_locality.md` for allocation and locality boundaries.
- `vulkan_blur_mode::KAWASE` is opt-in and native-only; LEGACY remains the default.
  Implementation is `impl/vulkan_kawase.inl` + `src/shaders/vulkan/kawase.comp`.
  `pui_ex_vulkan_blur` and `--vulkan-blur kawase` exercise it. Separate Kawase
  interaction/5000-Spiral tests cover direct/staged cache/raster equality and a
  new golden domain; never replace existing legacy goldens with Kawase output.
- `pui_ex_vulkan` is the external VkImage atomic; `pui_showcase_vulkan` uses the
  actual showcase helpers. Both support the normal example selftest interface.
- Register a native test once with `pui_add_vulkan_test` in `CMakeLists.txt`;
  its generated JSON cases drive `run_tests.py`, `vulkan_tests` and
  `vulkan_selftest`. Use `RUNS` for extra argument cases and `FULL_ONLY` for
  workloads excluded by `--quick`.
- `tools/run_vulkan_showcase.py` verifies the selected adapter and can work
  around a stale Windows manifest using existing packages only in child-process
  environments. It never changes registry or system driver installation.
  Portable launcher regressions are discovered as `tests/test_*tools.py`.

## Build & run

The project uses CMake presets with the Ninja generator and MSVC (`cl.exe`), so
commands must run from a **Developer PowerShell/Command Prompt for VS** (the
environment provides `cl.exe` and `VSINSTALLDIR`).

```sh
cmake --preset x64-debug
cmake --build out/build/x64-debug

cmake --preset x64-release
cmake --build out/build/x64-release
```

There are no `buildPresets`, so build by directory (above), not `cmake --build --preset ...`.

Run:

```sh
out/build/x64-debug/Debug/pui_demo.exe         # SDL3 demo
out/build/x64-debug/Debug/pui_counter.exe      # small model/view example
out/build/x64-debug/Debug/pui_core_tests.exe   # headless core tests
out/build/x64-debug/Debug/pui_golden_tests.exe # golden-image tests (SDL offscreen)
```

Notes:
- `out/` is generated build output and is gitignored. Never edit files under `out/`; regenerate instead.
- `assets/fonts/DejaVuSans.ttf` is the bundled coverage font (see `assets/fonts/README.md`); targets get `PUFFERUI_ASSET_DIR` so they can load it deterministically. System fonts remain a fallback. `DejaVuSans-Bold.ttf` / `DejaVuSans-Oblique.ttf` are bundled too; pick them per scope with `ui.text_style(size, font)` (a `text_scope`), which also overrides the size.
- Targets: the implementation TU is built once into the `pufferui` (core) and
  `pufferui_sdl3` (core + SDL3 backend) static libraries; every executable is
  one `pui_add_exe(name [SDL3] SOURCES ...)` line in `CMakeLists.txt` (examples
  use `pui_add_selftest_exe`). `pui_core_tests` is the headless suite (no SDL);
  `pui_pump_tests` routes synthetic SDL_PushEvent input through the offscreen
  driver; `pui_demo`, `pui_counter` and `pui_golden_tests` link `pufferui_sdl3`.
  Add new `.cpp` files to the right `pui_add_exe` call.
- `pui_golden_tests` renders fixed scenes with SDL's offscreen driver + software
  renderer and compares against committed goldens in `tests/golden/`. Regenerate
  with `pui_golden_tests --update` **only after eyeballing the regenerated BMPs**
  (failures dump `tests/golden/dump/<scene>_actual.bmp` + `_diff.bmp`).
- The impl TU includes the header with angle brackets
  (`#include <pufferui/pufferui.h>`) and relies on `include/` being on the include
  path; every other TU includes it the same way.

## Examples

- `examples/example_common.h` — the shared bootstrap (SDL init, event routing,
  bundled fonts, `--selftest`, `--screenshot`). Keep it a bootstrap: no widget or
  layout helpers in there.
- `examples/atomic/<name>.cpp` — one standalone program per capability, each with
  its own `main()` and a single `frame(ui&, example_app&)` callback. Add it to
  `PUFFERUI_EXAMPLE_NAMES` (SDL) or `PUFFERUI_HEADLESS_EXAMPLES` (no SDL) in
  `CMakeLists.txt`.
- `examples/pui_tour.cpp` — the covers-all app: grouped chapter sidebar, one
  chapter per capability, each naming the standalone target to run.
- `examples/dock_helpers.h` — app-side dock-tree policy shared by the docking
  example and the tour.
- `examples/pui_demo.cpp` / `pui_counter.cpp` — the larger two-window demo
  and the state/view example (README chapters 20).
- `examples/pui_showcase.cpp` — the motion/glass showcase app (animated re-layout,
  blur, drawer, palette, board); `examples/atomic/relayout.cpp` is its
  `animate_rect` atomic. `--fuzz SEED` stress-tests it.

Rules:

- Every example is a standalone program that supports `--selftest`
  (offscreen, scripted input, asserts `violation_count == 0` and exits non-zero
  on failure). `cmake --build <dir> --target examples_selftest` builds and runs
  them all; CI runs it after the golden tests.
- **Identity stays explicit** (standing decision): `_id` literals,
  `id_child`, `u.local`, `u.auto_id`. No label-derived IDs, ever (panels take
  `panel_opts::id` or a dock id; none reports `VIOL_PANEL_NO_ID`). Loops and
  reusable components scope with `u.scope(key)` (RAII, no area/clip); debug
  builds report the same id interacted at two different rects
  (`VIOL_DUP_WIDGET_ID`).
- **New multi-parameter APIs take options structs** (designated initializers):
  `u.button(r, "Save", id, {.role = "primary"_id})`. Positional overloads
  remain as thin wrappers; never grow a positional parameter list.
- **Control metrics come from the theme** (`u.control_h()`, `u.spacing()`,
  `u.button_size(label)`, `u.text_size(s)`): examples and components never
  hard-code 26–30px row heights.
- **Reusable widgets are components** (`namespace pui::comp`, one convention
  — see `docs/components.md`): free functions taking
  `(ui&, rect area, ..., const props&)`; props/result structs; **explicit
  `props.id`** (never label-derived); theme slot + `props.style` (a
  `const <name>_style *`: copy the theme's style, edit it, pass the pointer —
  no per-component override structs); no file-scope statics; keyboard-operable
  with cursor feedback (inside the library, start with
  `detail::widget_activate`); a headless test, an atomic example and a
  catalogue row each. Improve the shared components instead of copying widget
  code between examples.
- **One place per change.** A style field, a keyed store, an input field or a
  test must be addable by editing one place. If a change needs the same name
  edited in more than two spots, add the shared struct or helper first
  (`interaction_state`, `window_input`, `ctx_stores`, `PUI_TEST` are those
  helpers). Do not hand-copy a field list (`theme_lerp` is the one
  intentional enumeration — add new slot fields there and in the test).
- Examples lay out from `example_app::width/height`, which `example_common.h`
  keeps in sync with the real window on resize (and at startup), so they re-flow
  instead of keeping their startup size. `--resize WxH` exercises that path
  inside `--selftest`.
- Everything interactive sets the hovered cursor: core widgets already do this
  (buttons/checkbox → hand, sliders/splitters → resize, fields → I-beam, dock
  tabs and panel close dots → hand, scrollbar thumbs → hand); example-level
  widgets must call `u.set_cursor(CURSOR_HAND)` (or the fitting cursor) when
  hovered. Keep hovered windows responsible: the core applies the cursor only for
  the window under the pointer.
- A new capability means: a new atomic example, a row in the README examples
  index, a README tutorial chapter, and (for headline features) a tour chapter.
- Examples share one visual language: start every frame with
  `example_begin_page(u, app, "Title", "subtitle")` and group content in
  `example_section(u, r, "Title", "caption", app.font_bold)` cards; keep the
  `example_ui` rhythm (16 page padding, 12 between sections, 6–8 inside one,
  26–30px control rows). Improve the shared helpers/theme instead of adding
  per-example one-off styling.
- README snippets are copied from the example sources; when an example changes,
  update the matching README snippet in the same change.
- README pictures (`docs/img/readme/`, one above each chapter's "Run:" line,
  plus the two `pui_showcase` shots) come from `tools/readme_shots.ps1`: when an
  example's look changes, rerun it (release build) and review the PNGs before
  committing them. A new chapter with a "Run:" line gets an entry there too.

## The tutorial

`docs/tutorial/` is a step-by-step course (setup to a liquid-glass todo app) whose
programs live in `examples/tutorial/` (`pui_tut_*` targets, built and
`--selftest`ed in CI; `pui_tut_todo_test` is a headless test).

- Code blocks in the chapters that start with `<!-- src: <path> -->` are excerpts
  of that file; `tools/check_tutorial.ps1` (a CI step) fails when a block drifts.
  Change the source, then fix the block it points at.
- The pictures in `docs/tutorial/img/` come from `tools/tutorial_shots.ps1`.
- A public API change that a tutorial program uses must update that program and
  its chapter in the same revision.

## Code conventions

- Drawing goes through the required `render_painter` contract. Every backend
  supplies every operation; no implicit core tessellation fallback. Shared mesh
  algorithms are an explicit backend choice (`create_mesh_painter`). Rendering
  implementation lives in `impl/renderer_*.inl`; UI methods only submit semantic
  operations. Preserve existing streams/goldens when moving algorithms. Painter
  storage is owned by the renderer; `context::dl` is borrowed introspection only.
  Keep text measurement on the selected painter and popup recorders isolated.

- Naming: types/functions/variables `lower_snake_case`; macros and
  constants/enumerators `UPPER_SNAKE_CASE` (e.g. `backend_caps::RENDER_TARGETS`,
  `SCROLL_ALWAYS_RESERVE_BAR`, `ALIGN_LEFT`, `PUFFERUI_CHECK`).
- One context per thread: the current-context slot is `thread_local`, so a
  thread that draws must create its own context (or serialize access to a
  shared one around whole frames). Two threads with two contexts never
  cross-report violations (`test_thread_isolation` pins this).
- No smart pointers (`unique_ptr` / `shared_ptr` / `weak_ptr`) anywhere. Raw
  pointers are non-owning; ownership is explicit (`context`, `render_device`,
  the draw list, and scope objects own storage).
- `namespace pui` only — no global `using` dump; callers opt in with
  `using namespace pui;`.
- Keep the declaration section tiny: no `<vector>/<map>/<unordered_map>/<cmath>/<charconv>`
  and no SDL there. Heavy types live in the implementation section and are reached
  from `context` through forward-declared, typed pointers: `dl`, `ts`, `st`
  (`ctx_stores`: edit/anim/scroll/split/defer stores and the per-frame id
  sets, one allocation), and the per-window `focus_store` / `blur`.
- A node is a region/slice: prefer `cut_top/bottom/left/right`, `pad`, `column`/
  `row`, `track_row`/`track_column`, `auto_fit_grid`, and `scroll` over manual
  coordinate math, and assert slice validity.
- New public API is added as `ui` methods (or free functions) in the header's
  declaration section with the implementation in the `PUFFERUI_IMPLEMENTATION`
  block.
- State/view: one app-owned state per screen, model fields written directly, and a
  per-frame `view_t` derived by `update_view()`; components read `state.view` and
  write model fields. There is no intent queue. See
  `docs/model_view.md`. Renderer backends are documented in
  `docs/porting_a_backend.md`.

## Guardrails that prevent real bugs

- **Overlapping hit rects use last-submitted capture.** A later enabled rect
  can supersede an earlier press claim. Both calls may report `activated`, but
  only the final owner receives the release click. Keep drag handles disjoint
  from sibling hit rects when their press-edge effects must be exclusive.
- **Disabled scopes and DPI.** End `u.disabled()` before `end_frame`, as with
  other RAII scopes. Layout is logical; backend geometry/scissors are pixels.
  Change window scale only between frames. Table row keys identify records,
  not sorted/filtered indices. Shortcuts are consumed in submission order.
- **Animated layout goes through `u.animate_rect`, keyed by identity.** Key it
  with `u.local(...)` inside `u.scope(item.id)` (never the list index, or
  reorders animate the wrong items), call it once per key per frame (a second
  call is `VIOL_DUP_MOTION_KEY` and does not step), and pass the moving
  parent's corner as `.origin` (a scroll view's content, an animating card) so
  scrolling never makes children lag. Draw *and* interact at the returned rect.
  Do not hand-roll per-rect springs in examples; improve `animate_rect` instead.
- **Use `interaction.activated` for drag anchors, not `pressed`.** `pressed` means
  "held"; re-capturing an anchor every frame makes drags silently do nothing.
- **A draw that cannot paint is not issued.** `detail::paint_bounds` is the
  client area intersected with the active clip; geometry wholly outside it is
  culled before the device (new primitives that build geometry should test
  `detail::outside_paint` first). A zero-size clip culls everything (device
  "no scissor" means unclipped, so it must never reach the device). Scopes
  (`region`, `scroll_view`, panels) clip while alive: a second one created in the
  same block is nested in the first, not a sibling — scope siblings in `{ }`.
- **Never cut a temporary.** `region::content()` / `panel_scope::content()` /
  `scroll_view::content()` return by value: `r.content().cut_top(h)` discards the
  cut, so the next slice overlaps the previous one. Always
  `rect c = r.content(); rect top = c.cut_top(h); …`.
- **Text fields clip and scroll.** The field body pushes a clip
  (`detail::clip_push`/`clip_pop`) and keeps `edit_state::scroll_x` in sync with
  the caret, so long values never paint outside the field and the caret stays
  visible. A focused field's buffer is authoritative: the model is written back
  while focused, when focus is lost, and after framework edits such as a text
  drop — other widgets that edit a field's buffer must respect that.
- **Multi-click detection checks the position too.** Time alone (within 0.4 s)
  turns any second click anywhere in the field into a double-click; require the
  same spot (≈5 px) so clicks that only *look* quick stay single clicks.
- **Borders are rings, not full fills.** `button`/`card`/`panel` draw the
  background first and then the outline with `detail::rounded_ring` (4 edge strips
  + 4 corner annuli). Filling the whole bounds with the border color and
  inset-filling the background makes translucent backgrounds impossible (the
  border shows through) and turns outline buttons into filled ones — keep new
  styled widgets on the ring path.
- **RAII scopes must be destroyed before `end_frame()`** (`region`, `panel_scope`,
  `popup_scope`, `scroll_view`, dock/panel clips). Ending the frame with a clip
  still pushed trips the clip-balance assert.
- **Stacks clamp.** `row::next` / `column::next` / `track_*::next` /
  `grid_cursor::next` return at most the remaining space (and a zero-size rect once
  exhausted); never assume the requested size was granted, and skip zero-size
  slices.
- **Scroll overflow is judged from the previous frame's content height.** Call
  `set_content_height()` while the scope is alive; the bar/gutter appears from the
  next frame on. The offset is clamped against the last known content height.
- **Panels and popups block input using previous-frame rects.** A panel's own
  widgets are allowed only while the panel is on `panel_stack`, which is pushed at
  the top of `panel()` — do not move that push after the titlebar/close handling,
  or the panel blocks itself.
- **Never rewrite large files through scripted full-text passes.** A
  `WriteAllText`-style rewrite flattened every newline in
  `tests/test_core.cpp` once; the damage hid behind comments
  and duplicated bodies and took a long per-function repair pass to undo.
  Use targeted edits, keep the file compiling after each change, and let
  the compiler (not brace counting) tell you what broke.
- **A fix without a regression test is incomplete** (see below).
- **Expected violations are captured, not printed.** Tests install
  `set_violation_handler` and assert the guard message; the suite's output must
  stay clean (`all core tests passed` only).
- **Don't call OS/window functions every frame.** `SDL_SetWindowTitle`, cursor
  changes and vsync changes cross into the platform; gate them on an actual change
  (the titlebar's grab-anchor drag does the same: the host call only fires while
  the pointer actually moves).
  The demo HUD's `ui`/`sdl` split exists to attribute frame-time changes.
- **Popups may leave the window.** A popup scope whose rect spills out of the
  window records into a host overlay (`window_host::open_overlay`) and is
  replayed onto it after the window presents; its input arrives in the parent's
  local coordinates. Place popups with `detail::popup_space(c)` (the display
  when overlays exist), never `c->screen`, and keep render-target effects out of
  overlays (`ui::blur` already returns early there).
- **The native caption is data, not geometry.** `window::caption` (drag
  regions from `ui::titlebar` / `ui::window_drag_region`, minus every rect that
  `interact` saw over them) is what the SDL hit-test drags by. Never hard-code
  titlebar geometry in a backend: a widget on the bar would lose its clicks.
- **Multi-window state is per-window.** A `window` owns its input queue, focus
  list, blur scratch, and active/drag widget state; the pointer position and the
  held left button are global. Feed pointer motion from window-local mouse events
  (`mouse_move(window&, …)`) so it stays in the same space as `window::client`;
  `set_global_mouse` must use that space too (SDL3's `SDL_GetGlobalMouseState`
  does not on content-scaled displays). Do not let one window's cursor request
  override the hovered window's: only the window under the pointer applies its
  cursor. Input fed while a window's frame is open applies to that frame; between
  frames it queues in `window::in`. The first window is always allowed; every
  later one needs `backend_caps::SHARED_DEVICE`.

## Verification

Every feature, revision, or fix must either **come with a test** or **pass the
existing suite unchanged**:

- `python tools/run_tests.py` runs all of the checks below in one go on Windows, Linux
  and macOS (on Windows it finds MSVC itself): `--quick` = core + pump + performance + showcase,
  `--no-build`, `--release`, `--format`, `--skip-examples`. It exits non-zero if any
  step fails (the API-drift and tutorial checks need `pwsh`; they are skipped without).

- Add or extend tests in `tests/` (headless `tests/test_core.cpp` is the default
  place) for any new behavior, and add a regression test for every bug fix.
- Keep `pui_core_tests` green: build it and run
  `out/build/x64-debug/Debug/pui_core_tests.exe`; it must print
  `all core tests passed` and exit 0.
- Keep `pui_pump_tests` green (SDL event-pump routing, offscreen driver):
  `out/build/x64-debug/Debug/pui_pump_tests.exe` must print
  `all pump tests passed` and exit 0.
- Keep `pui_golden_tests` green: it must print `0 failure(s)` and exit 0. Any
  intentional visual change means regenerating goldens with `--update` and
  reviewing the new BMPs before accepting them.
- Formatting: `clang-format 22.1.3` with the repo `.clang-format` (see the CI
  workflow for the file list). `clang-format --dry-run --Werror` must pass.
- Build `pui_demo` too, and smoke-launch it for UI-visible changes.
- Do not report work as done until the suite passes. A fix without a regression
  test is considered incomplete.

## Design plan & status

The single plan and roadmap lives at `~/.opencode/plan/pufferui-roadmap.md`
(a from-scratch plan). It defines the phase sequence (foundations, test
infrastructure + measurement, performance, ergonomics, the component layer,
structure, reach, motion), the standing decisions (notably: **no label-derived
widget IDs, ever**), and the per-phase AGENTS.md/README deliverables.

When a feature or fix lands: add/extend tests, then update
`pufferui-roadmap.md` (tick the phase items, record the revision). Read the
plan before starting new work so nothing is forgotten.
