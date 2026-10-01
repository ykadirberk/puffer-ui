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
  (`VIOL_DUP_REGION_ID`); **widget IDs are not** (debug duplicate detection
  is planned; see the plan's r90). No label-derived IDs — rejected by
  decision.

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
current call/vertex profile and the batching plan.

## State and view

App-owned state, per-frame derived views, widgets writing model fields
directly — see `docs/model_view.md`. No intent queues.

## Text

stb_truetype rasterization into 1024² atlas pages (spilling to new pages
when full), UTF-8 decode, kerning via stb's legacy `kern` table, no shaping
(a deliberately deferred seam; see the plan's r95 phase).

## Violations

Every contract breach is a reported, enumerated `violation_code` — captured
by a handler, shown by a dev overlay, and (debug builds, unhandled) a
debugger break at the violating call. `PUFFERUI_ASSERT` marks invariants:
fatal, stops in every build.

## Structure notes (r94)

- The overlay stacks (popups, panels, panel layers) **grow on demand**: the
  old `MAX_POPUPS` / `MAX_PANELS` are initial capacities, so deep nesting
  never refuses a layer (`VIOL_POPUP_OVERFLOW` is no longer reachable).
- A panel's dock name is **interned** into context-owned storage when a drag
  starts: the drag ghost draws across frames, so the caller's string need
  not outlive the gesture.
- The header/implementation file split (with a generated single-header
  amalgamation) and the `slot_pool` keyed-store consolidation are deferred
  to a dedicated structural pass: both are behavior-neutral, large, and
  mechanical, and `docs/perf.md` shows the keyed stores are not hot. The
  single-header + one-TU distribution stays as-is until then.

## Threads

One context per thread. The current-context slot is `thread_local`; cross-
thread isolation is pinned by `test_thread_isolation`.