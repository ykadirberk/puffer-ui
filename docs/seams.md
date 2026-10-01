# PufferUI — designed seams

Interfaces that need a *shape* decided now so retrofitting later is cheap.
Each is design-first (this document); implementations land when the plan's
reach phase (r95) opens them.

## 1. DPI / content scale

- **Shape**: `window::scale` (f32, 1.0 by default). All theme metrics and
  font sizes are LOGICAL px; the renderer multiplies at draw time (vertex
  positions/sizes scale; the glyph atlas holds pages per `size*scale`).
- What must NOT change: rect-cutting math (apps keep thinking in logical
  px), IDs, key handling.
- Test plan: golden scenes at 1× and 2× (the same scene, scaled metrics —
  not a pixel doubling).
- Prerequisite: components (r91–93) — they centralize metric reads, which
  makes the scale multiply nearly free once they exist.

## 2. Text shaper interface

- **Shape**: an interface with `measure(text, style) -> extent`,
  `shape(text, style) -> glyph run`, `rasterize(...)`; the current
  stb_truetype implementation is the default.
- Why now: stb ignores GPOS kerning (modern fonts, incl. DejaVu, carry it),
  has no BiDi/RTL, no ligatures. A shaping backend (HarfBuzz or a
  built-in subset) becomes a drop-in behind the interface.
- What must NOT change: the layout cache's key contract (the shaper's
  output is cached the same way), the atlas paging, `text_*` APIs.

## 3. Input abstraction

- **Shape**: pointer events gain a `pointer_id` (mouse = 0); touch/pen
  become first-class; the current mouse API stays as sugar over
  pointer 0.
- Consequence for widgets: multi-pointer interactions (two-finger gestures)
  need per-pointer capture — the active/capture state moves from a single
  slot to keyed-by-pointer. Deferred until a touch use case exists; the
  seam is the `pointer_id` field.

## 4. Accessibility hook

- **Shape**: components (r91–93) may call
  `u.a11y({role, name, state, rect})` — compiled out by default; a platform
  adapter consumes the recorded tree later.
- Why components first: one call per component instead of retrofitting
  every widget.

## 5. Commands / shortcuts

- **Shape**: a keymap registry (`u.bind(key, id)` / a `command` table) so
  components check bindings instead of reading raw keys. Keyboard
  navigation (r85) already routes through the ring; commands add the
  app-level shortcut layer on top.

## 6. Stable window handles (C8)

- **Shape**: `window_id` (an integer generation-stamped handle) handed out
  alongside `window*`; `remove_window` invalidates pointers but not ids;
  `window_at(window_id)` re-fetches. Raw-pointer APIs deprecate over a
  release window.

## 7. Material / effect hook

- **Shape**: an optional `material` id in the draw command for shader-backed
  effects (SDF rounded rects, blur without render targets). Only justified
  if the r89+ profile shows the geometry path dominating — the current
  numbers (2 draw calls/frame) do not.
