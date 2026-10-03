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
- `vendored/` — third-party code: `SDL/` (SDL3), `stb/stb_truetype.h`.

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

- **Overlapping hit rects steal presses.** If two `interact` rects overlap, the
  first one drawn claims the press and the second never activates. Keep sibling hit
  rects disjoint, or pass `enabled = false` to the one that must yield (the panel
  titlebar yields to its close dot this way; the scroll thumb interacts before
  content for the same reason).
- **Animated layout goes through `u.animate_rect`, keyed by identity.** Key it
  with `u.local(...)` inside `u.scope(item.id)` (never the list index, or
  reorders animate the wrong items), call it once per key per frame (a second
  call is `VIOL_DUP_MOTION_KEY` and does not step), and pass the moving
  parent's corner as `.origin` (a scroll view's content, an animating card) so
  scrolling never makes children lag. Draw *and* interact at the returned rect.
  Do not hand-roll per-rect springs in examples; improve `animate_rect` instead.
- **Use `interaction.activated` for drag anchors, not `pressed`.** `pressed` means
  "held"; re-capturing an anchor every frame makes drags silently do nothing.
- **An empty clip paints nothing.** A zero-size clip culls everything (device
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
  and macOS (on Windows it finds MSVC itself): `--quick` = core + pump only,
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
