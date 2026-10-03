# PufferUI — design overview

A one-page orientation for contributors. The long-form history lives in the
plan (`~/.opencode/plan/pufferui-roadmap.md`); this is the architecture as
it stands.

## The substrate: rect-cutting

Layout is arithmetic on `rect` values. A node is a *slice*:

- `rect::cut_top/bottom/left/right(px)` mutate the parent and return the
  removed slice (clamped, asserted valid).
- Cursors (`column`, `row`) own the remaining space and hand out slices with
  gaps; they clamp and return zero-size rects when exhausted.
- `track_row`/`track_column` mix fixed/flex/ratio/fit-content tracks;
  `auto_fit_grid` reflows items like CSS `repeat(auto-fit, minmax(...))`.

There is no layout pass, no solver, no retained tree: what you cut is what
you get, and a screenshot is the debug view.

## Identity

Widgets are addressed by `uiid` (64-bit) hashes:

- `"name"_id` — compile-time FNV-1a of the literal.
- `id_child(base, i)` / `u.local("part")` — scoped derivation.
- `u.auto_id()` — a per-frame sequence counter.
- Region (RAII scope) IDs are duplicate-checked per frame
  (`VIOL_DUP_REGION_ID`); in debug builds so are widget IDs interacted at two
  different rects (`VIOL_DUP_WIDGET_ID`). Panels take an explicit id
  (`panel_opts::id` or their dock id; `VIOL_PANEL_NO_ID` otherwise). No
  label-derived IDs — rejected by decision.

## Frame model

- `begin_frame(window, ...)` — per-window input edges copied in, per-window
  stores (focus ring, blur scratch) attached, previous-frame overlay rects
  become the input capture set.
- Widgets: `u.interact(id, rect)` → `interaction` (hovered/held/activated/
  clicked/blocked/focused...). Press capture is topmost-wins (last submitted
  enabled rect containing the press).
- Overlays (combo menus, context menus, tooltips) draw at **end of frame**
  and capture input via previous-frame rects; picks resolve there and are
  reported by the widget's next call.
- `end_frame` flushes the draw list, resets per-window input edges, and
  returns stores to their windows.

## Rendering contract

`render_device` + `render_surface` are the entire backend surface: a handful
of pure-virtual methods (texture create/update/destroy, draw, targets,
blit, clip, cursor) plus capability flags (`RENDER_TARGETS`, `SCISSOR`,
`STREAMING_TEXTURES`, `SHARED_DEVICE`). Missing capabilities degrade
explicitly (e.g. blur without render targets becomes an alpha tint). A
headless `null_device` implements the contract in memory — the whole UI
logic is testable with no GPU.

The draw list batches by clip state and texture; see `docs/perf.md` for the
current call/vertex profile. Batches that miss the active clip are culled
before the device, and an empty clip (zero width/height) culls everything:
"no scissor" means unclipped on a device, so it is never sent for a clip.
Scopes clip while alive, so two scopes alive in one block are nested, not
siblings.

## State and view

App-owned state, per-frame derived views, widgets writing model fields
directly — see `docs/model_view.md`. No intent queues.

## Text

stb_truetype rasterization into 1024² atlas pages (spilling to new pages
when full), UTF-8 decode, kerning via stb's legacy `kern` table, no shaping
(a deliberately deferred seam; see `docs/seams.md`).

## Violations

Every contract breach is a reported, enumerated `violation_code` — captured
by a handler, shown by a dev overlay, and (debug builds, unhandled) a
debugger break at the violating call. `PUFFERUI_ASSERT` marks invariants:
fatal, stops in every build.

## Structure notes: growable stacks and interning

- The overlay stacks (popups, panels, panel layers) **grow on demand**:
  `MAX_POPUPS` / `MAX_PANELS` are initial capacities, so deep nesting
  never refuses a layer (`VIOL_POPUP_OVERFLOW` is never raised).
- A panel's dock name is **interned** into context-owned storage when a drag
  starts: the drag ghost draws across frames, so the caller's string need
  not outlive the gesture.

## Structure notes: file layout and shared state

- **File layout.** `include/pufferui/pufferui.h` is the declaration section
  (what users read) plus the `PUFFERUI_IMPLEMENTATION` block, which is now a
  list of `#include "impl/<slice>.inl"` lines inside one `namespace pui`. The
  slices (`core_state`, `draw_context`, `input_interact`, `layout_text`,
  `dock_style`, `widgets`, `components`, `chrome_scroll`, `typed_inputs`,
  `draw_primitives`, `sdl3`) are plain text cuts at section banners, in order.
  The contract is unchanged: one TU defines `PUFFERUI_IMPLEMENTATION`.
  `tools/amalgamate.ps1` rebuilds the single-file header (verified at the
  split to be byte-identical to the pre-split header).
- **One allocation for keyed state.** `ctx_stores` (edit/anim/scroll/split/
  defer stores and the per-frame id sets) hangs off `context::st`, created
  with the context and freed with one `delete`; per-window state is the typed
  `focus_store`/`blur` pair. No `void*` handles, no casts, no lazy `new`.
- **Input and widget state are single structs.** `window_input` is the whole
  input snapshot; the context derives from it (and from `interaction_state`)
  so a frame copies whole structs in and out. Event handlers write one place
  (`detail::input_for(window)`): the open frame's copy, or the window's queue.
- **Interaction skeleton.** `detail::widget_activate` (interact + hand cursor +
  click/Enter/Space as one `activated`) starts every clickable component.
- **Styles are plain structs.** Components take `const <name>_style *`
  instead of an override struct per component; `opt<T>` overrides remain for
  `button`, `panel` and `card`. `theme_lerp` interpolates every slot.
- Still deferred: nothing structural. Open items live in
  `docs/limitations.md`.

## Threads

One context per thread. The current-context slot is `thread_local`; cross-
thread isolation is pinned by `test_thread_isolation`.