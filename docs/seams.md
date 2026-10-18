# PufferUI — designed seams

Interfaces that need a *shape* decided now so retrofitting later is cheap.
Items explicitly marked implemented describe the current contract; the rest
remain design notes. A planned interface is not an available API.

## 1. DPI / content scale — implemented

- **API**: `set_window_scale` / `window::scale` (1.0 by default). All theme metrics and
  font sizes are LOGICAL px; the renderer multiplies at draw time (vertex
  positions/sizes scale; the glyph atlas holds pages per `size*scale`).
- What must NOT change: rect-cutting math (apps keep thinking in logical
  px), IDs, key handling.
- Test plan: golden scenes at 1× and 2× (the same scene, scaled metrics —
  not a pixel doubling).
- Prerequisite (met): the `pui::comp` components (r91–93) centralize metric
  reads, which makes the scale multiply nearly free.

## 2. Text shaper interface — implemented (r104)

- **API**: `text_shaper::shape(ctx, font, size, text, emit)` emits
  `shaped_glyph{cp, kern_in}` per glyph; `set_text_shaper(ctx, shaper)`
  installs one (nullptr = built-in: codepoint by codepoint with stb's kerning
  pairs). Measuring and drawing share the one layout it produces.
- Decision (2026-10-14): no HarfBuzz in the library. An app that needs complex
  scripts can wrap a shaping engine of its choice in a `text_shaper`; editing
  still maps carets codepoint by codepoint, so such a shaper should emit one
  glyph per input codepoint.
- Why now: stb supports GPOS pair kerning, but has no BiDi/RTL or ligatures.
  Ordered glyph fallback is implemented separately. A shaping backend (HarfBuzz or a
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

## 4. Accessibility hook — implemented (r106)

- **API**: `u.a11y(id, rect, a11y_info{role, name, value, state})`; built-in
  widgets and components call it. Off until `set_accessibility` (zero cost);
  each window publishes its frame's nodes (`a11y_count` / `a11y_node`), and
  assistive actions come back as `a11y_invoke` (a click) / `a11y_focus`.
- Adapter: Windows UI Automation (`impl/uia_windows.inl`, `sdl3_enable_uia`,
  enabled by the SDL3 bootstrap, recording starts with the first client query).
  The tree is flat (one level under the window); other platforms read the same
  nodes when an adapter is written.
- Why components first: one call per component instead of retrofitting
  every widget.

## 5. Commands / shortcuts

Implemented: full common key/modifier input and `u.shortcut(chord, opts)` with
exact modifiers and first-caller consumption. Optional `.focus` limits ownership
to a widget. This small API is available now; the registry below remains a proposal.

- **Shape**: a keymap registry (`u.bind(key, id)` / a `command` table) so
  components check bindings instead of reading raw keys. Keyboard
  navigation already routes through the ring; commands add the
  app-level shortcut layer on top.

## 6. Stable window handles (C8) — implemented differently (r99)

- Each window is its own allocation, so a `window*` stays valid until that
  window is removed, and its root id (and every widget's state) survives other
  windows coming and going. No separate id type was needed.

## 7. Material / effect hook

- **Shape**: an optional `material` id in the draw command for shader-backed
  effects (SDF rounded rects, blur without render targets). Only justified
  if profiling (`docs/perf.md`) shows the geometry path dominating — the current
  numbers (2 draw calls/frame) do not.
