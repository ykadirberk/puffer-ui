# State / view

PufferUI is immediate mode with a small, explicit data flow. Each outer frame:

```cpp
state.update_view();                 // 1. derive the view (once per frame)
begin_frame(ctx, win, now, dt);      // 2. build
  draw(ui, state.view, state);       //    read the view, write state fields
end_frame(ctx);
```

## Ownership

| State | Owner | Persists | Examples |
|---|---|---|---|
| Model fields | app | yes | document, settings, dock layout, window geometry |
| View (`state.view`) | app, derived | no (per frame) | labels, formatted values |
| UI state | framework, keyed by region id | yes | focus, edit buffer, caret, hover/drag |

Rules:

- `update_view()` runs once at the top of the frame and fills the `view_t`
  section from the model. It is the only place derived data is built.
- Components read `state.view` for display data and write model fields directly:
  `s.count += 1`, `u.text_field(r, s.name, id)`, `u.panel(title, s.bounds, ...)`.
- Read-only components take `const state::view_t&` and physically cannot write.
- The view is a snapshot: every component in a frame sees the same derived
  values, even if an earlier component wrote the model. Writes appear in the next
  frame's `update_view()` (one-frame latency, invisible at frame rates).
- Keep `view_t` cheap to build; if a value is expensive, derive it there once
  instead of formatting it at every use site.

## Why not per-use accessors

A `count_view()`-style getter is rebuilt at every call site; in a large UI the
same formatting runs many times per frame. Deriving once per frame and reading
plain fields keeps reads O(1).

## Carve-outs (interaction state the framework adjusts)

- **Dock trees**: `dock_space` resizes split ratios and switches the active tab in
  place (app-owned tree, framework-adjusted interaction state). Structural moves
  are reported as a `dock_action` and applied by the app after the frames.
- **Floating panels**: `panel()` adjusts the passed `rect&` while dragging; pass a
  model field and it writes back directly.

## Reference

- `examples/pui_counter.cpp` — the small, complete example (target `pui_counter`).
- README tutorial chapter 20 walks through the same pattern; `examples/pui_tour.cpp`
  uses it for every chapter.
- `examples/pui_demo.cpp` — the full demo: `demo_state` with a `view_t` section and
  `update_view()`.
- `tests/test_widgets.cpp::test_state_view_update` — pins the snapshot + write-back
  flow: a click writes the model during the frame, the view stays stale for that
  frame, and the next `update_view()` reflects the change.
