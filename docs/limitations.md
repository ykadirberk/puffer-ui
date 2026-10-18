# PufferUI — known limitations

Honest list, kept current by the plan (`~/.opencode/plan/pufferui-roadmap.md`).
Items marked *(deferred)* are designed (see `docs/seams.md`) but not scheduled;
the rest are accepted for now.

## Text

- **No complex-script shaping built in** — no BiDi/RTL, no ligatures, no
  HarfBuzz (a standing decision). Kerning uses stb's supported GPOS pair
  adjustments, falling back to `kern`. `set_font_fallbacks` supplies ordered
  per-glyph fallback; `set_text_shaper` lets an app plug in substitution and
  kerning of its own (one glyph per codepoint, so editing still works).
- Glyph atlas pages are recycled least-recently-drawn first past
  `set_text_atlas_budget` (default 16 pages); a recycled page's glyphs
  re-rasterize on their next draw. Eviction is per page, not per glyph.
- Fractional font sizes quantize to quarter pixels for cache keys.
- Glyphs are rasterized unhinted (stb_truetype) at four horizontal subpixel
  offsets; small text is evenly spaced but slightly soft. `theme::text_gamma`
  (1 = linear, the default) applies a coverage curve at rasterization: around
  1.3 makes thin light-on-dark strokes denser. No hinting or SDF backend.

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

- `context` still has public mutable fields; they are not stable API. Read
  frame state through `ui` (`u.now()`, `u.mouse()`, `u.mods()`,
  `u.focused_id()`, `u.clip_rect()`, ...) and the free functions
  (`window_count`, `window_by_index`); see CHANGELOG.md for the policy.
- Deferred menu labels are copied per frame (cheap, but a very long menu
  pays a copy), so the caller's strings need not outlive the call.
- `combo`/`context_menu` take spans too (the pointer + count forms remain).

## Widgets

- The core widget set is deliberately small; switch, radio, segmented, tabs,
  accordion, drawer, toast, table, command palette and section live in
  `pui::comp` (see `docs/components.md`).
- Tree rows and selectable rows are available; the app owns expansion and
  selection. Tables support stable keys, resizable widths, sort requests,
  column hiding/reordering, horizontal scrolling and a sticky first column.
- The eyedropper samples the app's own window, not the rest of the desktop.
- Pictures: the core takes decoded RGBA8; file decoding is the SDL glue's
  (`sdl3_load_image`: BMP, PNG, JPEG). No GIF/WebP/SVG, no animation.

## Platform

- Presets and CI are Windows/MSVC-first; a Linux (gcc/clang) CI matrix runs
  the headless and SDL-offscreen suites, and macOS has a preset but no CI.
- The default asset directory (`PUFFERUI_ASSET_DIR`) is baked at build time;
  `sdl3_app::asset_dir` overrides the bundled-font root at runtime, in the
  bootstrap only.
- DPI converts logical layout to framebuffer geometry and rasterizes glyphs at
  the window density. SDL follows scale-change events; custom hosts supply scale.
  Touch/pen input remains mouse-shaped. Accessibility: a Windows UI Automation
  adapter exists (flat tree; invoke/toggle/read value; values are read-only to
  assistive technology); macOS/Linux adapters are not written yet.
