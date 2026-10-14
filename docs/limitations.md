# PufferUI — known limitations

Honest list, kept current by the plan (`~/.opencode/plan/pufferui-roadmap.md`).
Items marked *(deferred)* are designed (see `docs/seams.md`) but not scheduled;
the rest are accepted for now.

## Text

- **No complex-script shaping** — no BiDi/RTL, no ligatures, no HarfBuzz.
  Kerning uses stb's supported GPOS pair adjustments, falling back to `kern`.
  `set_font_fallbacks` supplies ordered per-glyph fallback; it does not shape text.
- Glyph atlases never evict (pages are added when full) *(deferred: a surfaced LRU policy)*.
- Fractional font sizes quantize to quarter pixels for cache keys.
- Glyphs are rasterized unhinted (stb_truetype) at four horizontal subpixel
  offsets; small text is evenly spaced but slightly soft. A contrast/gamma curve
  on the coverage or an SDF/hinting backend would sharpen it *(not planned yet)*.

## Scale

- Fixed capacities (reported on overflow): `MAX_DOCK_PANELS 8`,
  `MAX_DOCK_DEPTH 8`, `MAX_TRACKS 16`, `MAX_STYLE_SCOPES 16`, `MAX_ID_DEPTH 64`.
  The popup/panel stacks and the window list grow on demand
  (`MAX_POPUPS`/`MAX_PANELS`/`MAX_WINDOWS` are initial capacities), and
  duplicate-region checking is an unbounded per-frame hash set.
- Culling is geometric: a draw whose bounding box lies wholly outside the
  client area or the active clip never reaches the renderer (shapes, text glyphs,
  blurs), and `scroll_view::virtual_list` skips building off-screen rows. Other
  off-screen content is still *built* (layout and widget calls run); only its
  geometry is culled.
- One context per drawing thread; per-window draw lists within a context are recorded
  sequentially (parallel recording is a design note, not planned).

## API surface

- `context` has public mutable fields — treat them as unstable API;
  there is no accessor surface yet *(deferred)*.
- `remove_window` invalidates `window*` pointers for windows after the
  removed one (documented; re-fetch via `window_at`) *(deferred: stable
  window ids)*.
- Deferred menu labels are copied per frame (cheap, but a very long menu
  pays a copy), so the caller's strings need not outlive the call.
- `combo`/`context_menu` still take `const char *const*` + count; the
  `pui::comp` components take spans.

## Widgets

- The core widget set is deliberately small; switch, radio, segmented, tabs,
  accordion, drawer, toast, table, command palette and section live in
  `pui::comp` (see `docs/components.md`).
- No multiline text editor or date picker. Tree rows and selectable rows are
  available; the app owns expansion and selection. Tables support stable keys,
  resizable widths and sort requests, but not column reordering/hiding or horizontal scrolling.
- Color editing currently uses RGBA sliders; there is no HSV picker or eyedropper.

## Platform

- Presets and CI are Windows/MSVC-first; a Linux (gcc/clang) CI matrix runs
  the headless and SDL-offscreen suites, and macOS has a preset but no CI.
- The default asset directory (`PUFFERUI_ASSET_DIR`) is baked at build time;
  `sdl3_app::asset_dir` overrides the bundled-font root at runtime, in the
  bootstrap only.
- DPI converts logical layout to framebuffer geometry and rasterizes glyphs at
  the window density. SDL follows scale-change events; custom hosts supply scale.
  Touch/pen input remains mouse-shaped; platform accessibility adapters are absent.
