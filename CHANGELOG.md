# Changelog

The version is `PUFFERUI_VERSION_*` in `include/pufferui/pufferui.h` (CMake
reads it from there). Within one major version releases stay source
compatible; an API that is replaced keeps working for one minor version with a
deprecation warning before it is removed. Fields of `context` are not part of
the stable API: read frame state through `ui` (`u.now()`, `u.mouse()`,
`u.mods()`, `u.focused_id()`, ...) and the free functions.

Entries are grouped by plan revision (`r97` ...); see the README chapters named
in each entry for usage.

## Unreleased (0.1.0.1 working tree)

### r109 - Glyph checks and overlay replay
- `ui::text` checks cached glyph copies for recycled atlas pages only while a
  page has been recycled since the run was last validated (an epoch per cached
  run); stale sub-pixel copies are dropped on the checked frame. The atlas LRU
  stamp is written once per page and run instead of once per glyph.
- Popup overlays: the recorder shifts draws and clips into the overlay's pixels
  as they arrive and replays them without a per-frame vertex copy.

### r108 - Frame cost of large text and long lists
- `text_area` word wrap sums glyph advances in one pass instead of measuring
  every prefix (identical breaks; no prefix entries in the layout cache), with
  advances and kerning pairs memoized per rebuild.
- `text_area` line cache keys on an edit generation and the buffer size instead
  of hashing the text every frame; the line-number gutter's paragraph count and
  each line's paragraph number are cached with the lines; an unfocused area
  copies the model only when it differs.
- Added `virtual_list_opts::heights_version` for
  `scroll_view::virtual_list(count, height_of, fn, opts)`: kept row offsets,
  re-measured only when the version changes (a grown count measures new rows).
- Accessibility nodes reuse their slots (and string capacity) between frames.
- `ui::text` stops a run at the first glyph past the right edge of the paint
  area once no later glyph can paint (same output as the per-glyph cull).
- `pui_bench --wrap-type/--wrap-resize/--bigtext/--longlines/--vlist/
  --vlist-kept/--a11y`; `pui_showcase --perf/--a11y`.

### r107 - Widget gaps
- Added `comp::color_picker` (HSV, hue/alpha bars, hex, eyedropper) and
  `comp::date_picker`; `hsv_color`, `to_hsv`, `from_hsv`; `date`, `days_in_month`,
  `weekday`, `add_days`, `add_months`.
- Added `render_device::read_pixels`, `backend_caps::READ_PIXELS`,
  `ui::sample_pixel`, `ui::take_sampled_pixel`.
- Tables: `table_column::hidden`, `table_props::order/column_menu/horizontal/
  sticky_first`, `table_result::columns_changed`.
- Fixed: the color picker's square took the full area height.

### r106 - Platforms and accessibility
- Accessibility: `ui::a11y`, `a11y_info`/`a11y_role`/`a11y_state`,
  `set_accessibility`, `a11y_count`/`a11y_node`, `a11y_invoke`, `a11y_focus`;
  Windows UI Automation adapter (`sdl3_enable_uia`, on by default in the SDL3
  bootstrap, recording starts with the first client query).
- `window::os_frame`: the OS frame returns where the platform has no
  hit-test; `ui::titlebar` stands down.
- CI: macOS job; Linux runs the example selftests.

### r105 - API stability
- Added `PUFFERUI_VERSION_MAJOR/MINOR/PATCH/TWEAK` and `version()`; CMake takes
  the project version from the header.
- Added frame accessors on `ui`: `mouse()`, `mouse_down()`, `mouse_pressed()`,
  `mouse_released()`, `wheel()`, `mods()`, `now()`, `dt()`, `focused_id()`,
  `request_focus(id)`, `current_window()`, `clip_rect()`. Examples and the
  tutorial no longer read `context` fields.
- Added `window_count(ctx)` and `window_by_index(ctx, i)`.
- Added `std::span` overloads of `combo` and `context_menu`.
- The external consumer check builds a client that uses only this surface.

### r104 - Text quality (no HarfBuzz)
- Added `theme::text_gamma` (glyph coverage curve; 1 = linear, the default).
- Added the shaping seam: `text_shaper`, `shaped_glyph`, `set_text_shaper`.

### r103 - Long-running apps
- Glyph atlas pages are recycled least-recently-drawn first past
  `set_text_atlas_budget` (default 16); `text_atlas_pages`.
- Added `scroll_view::virtual_list(count, height_of, fn)` for rows of different
  heights.

### r102 - Multi-line text
- Added `ui::text_area` (`text_area_opts`: wrap, read_only, line_numbers),
  `ui::editor_info`, `ui::scroll_offset`.
- Fixed: a triple click in a text field selects everything again (the drag
  code pulled the selection back to a word).

### r101 - Popups beyond the window
- Popups that leave their window draw into host overlay windows
  (`window_host::popup_bounds/open_overlay/move_overlay/close_overlay`; the SDL3
  host uses popup-menu windows). Added `POPUP_IN_WINDOW`, `set_popup_overlays`,
  `dismiss_popups`. Combo dropdowns and context menus are placed within the
  display when overlays exist.

### r100 - OS integration
- Pictures: `create_image`, `destroy_image`, `ui::image` (`image_opts`: fit,
  tint, radius), `fit_image`, `sdl3_load_image` (BMP/PNG/JPEG via SDL).
- OS drag-and-drop: `drop_event`, `ui::drop_target`, SDL routing.
- File dialogs: `sdl3_file_dialog`, `take_file_dialog`, `deliver_file_dialog`.
- Window state: `get_window_state`, `save_window_state`, `load_window_state`,
  `clamp_to_displays`, `sdl3_apply_window_state`.
- `render_device::set_texture_filter`.

### r99 - Titlebars and windows
- Widgets placed on a custom titlebar receive their clicks (the native caption
  is rebuilt each frame minus interacted rects, `window::caption`).
- Added `titlebar_opts`, `titlebar_result::bar/free`, theme slot
  `titlebar_style`, `ui::window_drag_region`.
- `window*` pointers stay valid until that window is removed (breaking:
  `context::windows` is now `window**`).
- Added `render_device::destroy_surface`, `sdl3_app_open_window`,
  `sdl3_app_close_window`.
- Fixed: `sdl3_app_init` registered the primary window at the desktop origin.

### r98 - Frame pacing and unfocused clicks
- Fixed: example loops presented each frame twice (vsync waited two vblanks);
  `end_frame` presents, `sdl3_surface::present` ignores a repeat.
- With several windows only one waits for the vblank.
- The SDL3 glue enables mouse click-through at DEFAULT hint priority.

### r97 - Ergonomics and parity
- Disabled groups, read-only fields, portable keys and shortcuts, tables,
  tree/selectable rows, numeric/vector fields, RGBA color editing, DPI and
  font fallback, CMake install targets.
