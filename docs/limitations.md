# PufferUI — known limitations

Honest list, kept current by the plan (`~/.opencode/plan/pufferui-roadmap.md`).
Items marked *(planned)* have a phase; the rest are accepted for now.

## Text

- **No complex-script shaping** — no BiDi/RTL, no ligatures, no HarfBuzz;
  kerning uses stb's legacy `kern` table (GPOS kerning in modern fonts is
  ignored) *(planned: text-shaper seam, r95)*.
- No per-glyph font fallback chains (`has_glyph` reports coverage; a missing
  glyph draws nothing for that character).
- Glyph atlases never evict *(planned: surfaced LRU page policy, r89/r94)*.
- Fractional font sizes quantize to quarter pixels for cache keys.

## Scale

- Fixed capacities (all reported on overflow, several growable already):
  `MAX_POPUPS 8`, `MAX_PANELS 16`, `MAX_DOCK_PANELS 8`, `MAX_DOCK_DEPTH 8`,
  `MAX_TRACKS 16`, `MAX_STYLE_SCOPES 16`, `MAX_ID_DEPTH 64` *(planned:
  growable stores, r94)*. `MAX_FRAME_IDS` (2048 regions/frame) reports
  `VIOL_FRAME_IDS_OVERFLOW`; beyond it duplicate-region checking is
  incomplete — virtualize long lists.
- Only `scroll_view::virtual_list` culls; other content inside a scroll view
  is fully submitted and scissored *(planned: draw-list-level culling, r89)*.
- Single-threaded, single-context; per-window draw lists are recorded
  sequentially (parallel recording is a design note, not planned).

## API surface

- `context` has public mutable fields — treat them as unstable API;
  encapsulation is incremental *(planned: accessor surface, r90/r95)*.
- `remove_window` invalidates `window*` pointers for windows after the
  removed one (documented; re-fetch via `window_at`) *(planned: stable
  window ids, r90)*.
- Deferred menu labels are copied per frame (cheap, but a very long menu
  pays a copy) — the old dangling-pointer rule is gone.
- `combo`/`context_menu` still take `const char *const*` + count (span-based
  APIs arrive with the component layer, r90+).

## Widgets

- The core widget set is deliberately small; switch/radio/segmented/tabs/
  toast/table/drawer live in `examples/atomic/patterns.cpp` until the
  component layer promotes them *(planned: r91–r93)*.
- No multiline text editor, no date picker, no tree view yet.

## Platform

- Presets and CI are Windows/MSVC-first; Linux (gcc/clang) CI lands with
  r88 and the `__GNUC__` fallbacks may need first-run fixes.
- `PUFFERUI_ASSET_DIR` is baked at build time *(planned: runtime asset-dir
  API, r90)*.
- DPI/content-scale is not applied to metrics *(planned: `window::scale`,
  r95)*; touch/pen input is mouse-shaped today.
