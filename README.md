# PufferUI

A C++20 immediate-mode GUI library built on pure rect-cutting layout: a real
declaration/implementation split, model/view state, docking and multi-window
support, animation, and a replaceable renderer contract (SDL3 backend included).

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square)](https://en.cppreference.com/w/cpp/20)
[![Architecture: Header + One TU](https://img.shields.io/badge/Architecture-Header%20%2B%20One%20TU-brightgreen.svg?style=flat-square)](include/pufferui/pufferui.h)
[![Layout: Pure Rect Cutting](https://img.shields.io/badge/Layout-Pure%20Rect%20Cutting-orange.svg?style=flat-square)](#2-rect-algebra)
[![Backend: Pluggable](https://img.shields.io/badge/Backend-SDL3%20%2F%20Pluggable-blueviolet.svg?style=flat-square)](docs/porting_a_backend.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-teal.svg?style=flat-square)](LICENSE)

---

## Overview

PufferUI is an immediate-mode GUI library built on pure rect-cutting layout. It
splits declarations from the implementation, adds multi-window + docking,
animation, and a replaceable renderer contract, and lives in `namespace pui`
only — callers opt in with `using namespace pui;`.

- **Immediate mode, no retained tree.** Every frame draws the UI from your state.
- **Pure rect cutting.** `cut_top/bottom/left/right` mutate a rect and return the
  slice; cursors (`column`, `row`, `track_row`, `grid_cursor`) turn that into
  layout. No constraint solver, no layout pass, no surprises.
- **State/view, direct write-back.** Widgets write your model fields directly;
  there is no intent queue and no reducer. See [`docs/model_view.md`](docs/model_view.md).
- **One header + one implementation TU.** `include/pufferui/pufferui.h` +
  `src/pufferui_impl.cpp`, no smart pointers, no hidden globals.
- **Tested.** A headless suite (`pui_core_tests`) and golden-image tests
  (`pui_golden_tests`) run in CI, and every example runs `--selftest` offscreen.

## Quick start

### Build

The project uses CMake presets with the Ninja generator and MSVC (`cl.exe`), so
commands must run from a **Developer PowerShell/Command Prompt for VS**.

```sh
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

### Run the tour

`pui_tour` walks through every capability in one window (scrollable chapter
sidebar, Up/Down to switch):

```sh
out/build/x64-debug/Debug/pui_tour.exe
```

The `pui_ex_*` binaries each show one capability in isolation, and
`pui_demo` is a bigger two-window showcase (docking, floating panels, HUD).

### The smallest program

A complete PufferUI app. `sdl3_app_init` owns the SDL window (borderless +
resizable), the renderer, the device, the surface, the context, and the window
with the **native chrome** installed — so dragging and resizing (plus the
system's own Aero Snap) come from the platform. Your loop is
`sdl3_app_pump -> sdl3_app_tick -> begin_frame -> draw -> end_frame -> present`.

```cpp
#include <pufferui/pufferui.h>
using namespace pui;

int main()
{
    sdl3_app app;
    if (!sdl3_app_init(app, "hello", 640, 360, 0, nullptr)) return 1;

    bool running = true;
    while (running)
    {
        if (!sdl3_app_pump(app)) running = false;
        sdl3_app_tick(app);
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            const theme &th = u.th();
            const f32 w = app.win->area.w, h = app.win->area.h;
            u.draw_rect(rect::make(0, 0, w, h), th.bg);
            u.text(rect::make(24, 24, 300, 24), "Hello, PufferUI", th.text, ALIGN_LEFT);
        }
        end_frame(app.ctx);
        app.surface->present();
    }
    sdl3_app_shutdown(app);
    return 0;
}
```

Text just works: the bootstrap loads the bundled DejaVu font (system fonts as
the fallback) and installs a working base theme — `app.font` hands the handle
back, and a different look is one `set_theme` away. The same app without the
harness is `examples/atomic/bootstrap.cpp` (`pui_ex_bootstrap`) — run it, then
read it: it is ~110 lines including its own `--selftest`. The shared
boilerplate every *other* example builds on lives in
`examples/example_common.h` (bold/oblique fonts, scripted `--selftest`,
`--screenshot <file>`); the library-level bootstrap also powers it.

---

## Tutorial

Each chapter names the standalone example that shows it in isolation. Run them
with `pui_ex_*`; read them under `examples/atomic/`.

### 1. The frame loop

- `context *create_context(render_device*, render_surface*)` owns all state.
- `window *add_window(ctx, native_handle, surface, client_rect)` registers a
  window; `client` is in desktop coordinates, `window::area` is local `{0,0,w,h}`.
- `begin_frame(ctx, *win, now_seconds, dt_seconds)` … `end_frame(ctx)` surround
  one window's frame; call `present()` on its surface afterwards.
- Input is fed per window: `mouse_move(window&, x, y)` uses window-local
  coordinates, `mouse_button(window&, down)`, `mouse_wheel(window&, dx, dy)`,
  `key_event(window&, key, down)`, `text_input_event(window&, utf8)`,
  `mods_event(window&, shift, ctrl)`, `ime_event(window&, preedit, cursor)`.
  Between frames these queue in `window::in`; during that window's frame they
  apply immediately. The single-window overloads (`mouse_move(ctx, x, y)`, …)
  route to the primary window.
- `ui u(ctx)` is the drawing/layout entry point for the frame. It is created on
  the stack and is not stored.

### 2. Rect algebra

`rect` is 4 floats with slicing helpers. `cut_*` **mutate** the source and return
the slice; `*_slice` and `align_*`/`fit_aspect` are pure.

```cpp
rect area = {20, 20, 320, 200};

const rect top    = area.cut_top(40);         // area loses its top 40px
const rect bottom = area.cut_bottom(30);      // ... and its bottom 30px
const rect left   = area.cut_left(90);
const rect right  = area.cut_right(90);
const rect rest   = area;                     // what is left

const rect header = area.cut_top_ratio(0.25f);// same, but relative

const rect strip  = area.top_slice(14);       // does not touch `area`
const rect video  = area.pad(8).fit_aspect(16.0f / 9.0f); // centered 16:9
const rect badge  = rect::make(0, 0, 80, 22).align_right(area).align_top(area);
const rect hit    = rect::intersect(a, b);
const bool inside = a.contains(mouse_x, mouse_y);
```

> **Never cut a temporary.** `region::content()`, `panel_scope::content()` and
> `scroll_view::content()` return by value: `r.content().cut_top(h)` discards the
> result. Assign to a `rect` first.

Run: `pui_ex_rect`. Full source: `examples/atomic/rect.cpp`.

### 3. Cursors

Cursors are the loops of the layout system. `next` advances and clamps — it never
overflows the cursor, and returns a zero-size rect when exhausted.

```cpp
column col(client.pad(12), 8);          // top-to-bottom, 8px gap
rect header = col.cut_top(40);
rect footer = col.cut_bottom(28);
rect body   = col.remaining();

row main(body, 8);                       // left-to-right
rect sidebar = main.next(150);
rect content = main.remaining();

col.space(6);                            // skip a gap, produce nothing
```

`column`/`row` also have `cut_top/cut_bottom` and `cut_left/cut_right` variants
that advance past the gap like `next` does.

Run: `pui_ex_cursors`. Full source: `examples/atomic/cursors.cpp`.

### 4. Tracks and auto-fit grids

`track_size` describes a column: `fixed(px)`, `flex(weight)`, `ratio(fraction)`
or `fit_content(fallback, max)`, each with `.min(px)` / `.max(px)` clamps.
`track_row`/`track_column` resolve the sizes and hand out slices.

```cpp
track_row tr(col.next(30),
             {track_size::fixed(70), track_size::flex(2), track_size::flex()}, 8);
rect label = tr.next();
rect left  = tr.next();
rect right = tr.next();
```

`auto_fit_grid` implements CSS `repeat(auto-fit, minmax(min, 1fr))`:

```cpp
grid_cursor g = auto_fit_grid(col.next(200), 12, 120.0f, 64.0f, 8.0f);
for (i32 i = 0; i < g.count(); ++i)
{
    const rect cell = g.next();          // reading order
    u.card(cell);
}
const rect last = g.cell(11);            // random access, does not consume
```

Splitters are interactive and app-owned:

```cpp
static f32 side_w = 170;
const auto panes = u.split_horizontal_interactive("split"_id, area, &side_w, 90, 260, 6, 6);
// panes.first / panes.second
```

Run: `pui_ex_tracks`, `pui_ex_grid`. Full source: `examples/atomic/tracks.cpp`,
`examples/atomic/grid.cpp`.

### 5. Regions and IDs

Every interactive widget needs a stable `uiid`. `"Name"_id` is a compile-time
FNV-1a hash; `id_child(parent, salt)` derives ids; `u.local("name")` derives from
the current region, and `u.auto_id()` is a draw-order id for unlabeled widgets.

```cpp
static void card(ui &u, rect r, const char *title, i32 &value)
{
    region reg(u, title, r);              // scopes ids and clips content
    column c(reg.content().pad(8), 6);
    u.text(c.next(18), title, u.th().text, ALIGN_LEFT);
    if (u.button(c.next(26), "increment", u.local("inc")))
        value += 1;
}
```

Two calls with different region keys keep independent state even though the code
is identical. IDs matter:

- keep sibling hit rects **disjoint** (the first `interact` claims the press), or
  draw the one that must yield with `interact(id, r, false)`;
- **sibling regions with the same key** trip the duplicate-id assert;
- use `interaction.activated` (press edge) for drag anchors, never `pressed`.

Run: `pui_ex_ids`. Full source: `examples/atomic/ids.cpp`.

### 6. Text and fonts

```cpp
u.text(r, "left", th.text, ALIGN_LEFT);            // ALIGN_LEFT / CENTER / RIGHT
u.text_ellipsis(r, "a very long label", th.text);  // trims, appends U+2026
u.text_wrapped(r, "wraps on words inside r", th.text); // clips the rest
f32 used = u.text_fit(r, "wraps and reports the height", th.text);
measure_size m = u.measure_text(s, available_width);   // wrap-aware, no drawing
f32 w = u.text_width(s);                                // single line, with kerning
```

Sizes and fonts are scoped with `text_style`, which returns a `text_scope` and
restores the previous values on destruction:

```cpp
{
    text_scope title = u.text_style(24, app.font_bold);
    u.text(r, "Bold title", th.text, ALIGN_LEFT);
}
{
    text_scope obl = u.text_style(15, app.font_oblique);
    u.text(r2, "oblique", th.text_dim, ALIGN_LEFT);
}
```

`load_font(ctx, path)` returns a `font_handle` (`FONT_INVALID` on failure); the
repo bundles DejaVu Sans regular/bold/oblique under `assets/fonts/`, and targets
get `PUFFERUI_ASSET_DIR` so examples load them deterministically. `u.has_glyph(cp)`
answers coverage questions. Text is decoded per UTF-8 codepoint; the documented
coverage set (Latin-1/Extended-A, Greek, punctuation, currency, math, arrows,
check marks) is asserted by `test_font_coverage`.

Run: `pui_ex_text`. Full source: `examples/atomic/text.cpp`.

### 7. Buttons and roles

```cpp
if (u.button(r, "Save", "save"_id, "primary"_id)) save();

u.button(r, "Square", "sq"_id, 0, button_override{.radius = some(0.0f)});
u.button(r, "Outline", "out"_id, 0,
         button_override{.bg = some(color{0, 0, 0, 0}),
                         .border = some(th.accent),
                         .border_thickness = some(1.5f)});

{
    style_scope warn(u, button_override{.bg = some(color{200, 150, 40, 255})});
    u.button(r2, "Scoped", "scoped"_id);     // restyled by the scope
}

button_style st = u.resolve_button_style("primary"_id); // what a role resolves to
```

Roles are theme entries: `set_button_role(t, "primary"_id, button_override{...})`.
Hover/active colors animate when `t.button.transition = transition{0.10f}`.
Buttons request `theme.button_cursor` on hover automatically.

Run: `pui_ex_buttons`. Full source: `examples/atomic/buttons.cpp`.

### 8. Text and number fields

Fields edit your `std::string` / `f32` directly and return `true` on change; the
editing model is browser-like.

| Input | Behavior |
| --- | --- |
| click | focus and place the caret **where you clicked** (also on the first click) |
| Tab | focus the next field and select its value (typing replaces it) |
| drag | select a range; **double-click** selects the word, **triple-click** everything |
| drag a selection | lift and **move** it — drop into the same or another field; **Ctrl** when dropping copies; **Escape** or dropping outside cancels |
| ←/→, Home/End | move the caret; **Shift** extends the selection |
| Ctrl+←/→ | move by word; Ctrl+Shift+←/→ selects by word |
| Backspace / Delete | delete a character; **Ctrl**+Backspace/Delete delete a word |
| Ctrl+A / C / X / V | select all / copy / cut / paste through the app `clipboard` |
| Ctrl+Z / Ctrl+Y | undo / redo (Ctrl+Shift+Z also redoes); a run of typing coalesces into one step |
| Enter / Escape | commit (unfocus) / cancel focus |

Long values scroll horizontally so the caret stays visible, and the field body is
clipped — nothing paints outside the field.

```cpp
static std::string name = "PufferUI";
static f32 amount = 12.5f;

u.text_field(r, name, "name"_id);
u.number_field(r2, amount, "amount"_id, "%.2f");

if (u.key_pressed(key::ENTER)) submit(name, amount);
```

Set a clipboard implementation once: `set_clipboard(ctx, &my_clipboard)` (see
`example_clipboard` in `examples/example_common.h`, which bridges SDL).
`number_field` takes a printf format; step buttons / integer / unit codecs are
not implemented yet (see Status). Text drag & drop is **in-app only** — interop
with other applications is through the clipboard.

Run: `pui_ex_inputs`. Full source: `examples/atomic/inputs.cpp`.

### 9. Checkbox, slider, progress bar

```cpp
bool wrap = true;
f32 quality = 35.0f;

u.checkbox(r1, "Wrap long lines", wrap, "wrap"_id);
u.slider_float(r2, "Quality", quality, 0.0f, 100.0f, "quality"_id, "%.0f%%");
u.progress_bar(r3, quality / 100.0f, th.accent, th.widget_bg);
```

`slider_float` jumps to the click position, drags with the mouse held, clamps to
`[min, max]` and returns `true` while the value changes. All three take the theme
colors; the slider requests a horizontal-resize cursor on hover.

Run: `pui_ex_widgets`. Full source: `examples/atomic/widgets.cpp`.

### 10. Cards and floating panels

```cpp
u.card(r);                                       // theme card style
u.card(r2, card_override{.bg = some(color{40, 80, 60, 220}),
                          .border = some(th.accent),
                          .radius = some(10.0f),
                          .border_thickness = some(2.0f)});

rect bounds = {340, 190, 270, 160};              // app-owned; written back on drag
panel_scope p = u.panel("Tools", bounds, PANEL_NONE, "tools"_id);
if (p.close_requested) show_tools = false;       // your model decides
else { column pc(p.content(), 6); /* ... */ }
```

`panel` flags: `PANEL_NO_TITLEBAR`, `PANEL_NO_CONTROLS`, `PANEL_NO_DRAG`,
`PANEL_NO_SHADOW`. Panels block input to the UI underneath them (using
previous-frame rects), which is why the panel is constructed *after* the base UI.

Run: `pui_ex_panels`. Full source: `examples/atomic/panels.cpp`.

### 11. Scrolling

`scroll` returns a `scroll_view` scope: it clips to the viewport, offsets its
content, claims the wheel when the pointer is over it (innermost view wins),
draws a draggable scrollbar and manages the gutter.

```cpp
scroll_view sv = u.scroll(area.pad(4), "list"_id,
                          scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
column list(sv.content(), 4);

for (i32 i = 0; i < 40; ++i)
{
    const rect item = list.next(24);
    if (u.interact(u.auto_id(), item).clicked)
        sv.ensure_visible(item);         // scroll so `item` is visible
}
sv.set_content_height(40 * 24 + 39 * 4);
```

- flags: `SCROLL_ALWAYS_RESERVE_BAR` (stable gutter), `SCROLL_OVERLAY` (floating
  bar, no gutter), `SCROLL_NO_CLIP`;
- helpers: `set_content_height`, `scroll_to`, `scroll_by`, `ensure_visible`,
  `offset()`, `overflows()`;
- overflow is judged from the previous frame's content height, so a new scroll
  area settles in one frame. If you already know the content height (fixed rows,
  a form), call `set_content_height` **before** laying out — the first frame is
  then correct instead of briefly collapsing.

Run: `pui_ex_scroll`. Full source: `examples/atomic/scroll.cpp`.

### 12. Popups and menus

A popup is a top input-capturing layer: base UI is blocked while it is open, Tab
stays inside it (focus trap), and it reports close requests through the scope.

```cpp
if (u.button(menu_btn, "Menu", "menu"_id)) menu_open = true;

if (menu_open)
{
    const rect menu = {menu_btn.x, menu_btn.bottom() + 4, 180, 130};
    popup_scope p = u.popup("menu_popup"_id, menu,
                            POPUP_CLOSE_ON_ESCAPE | POPUP_CLOSE_ON_CLICK_OUTSIDE);
    if (p.close_requested) menu_open = false;
    else
    {
        u.draw_rounded_rect(menu, th.panel_bg, 6);
        column items(menu.pad(8), 4);
        if (u.button(items.next(26), "Rename", "rename"_id)) { /* ... */ menu_open = false; }
    }
}
```

Flags: `POPUP_CLOSE_ON_ESCAPE`, `POPUP_CLOSE_ON_CLICK_OUTSIDE`, `POPUP_MODAL`
(blocks the base UI entirely). Popups are capped at `MAX_POPUPS` (8) and layered
in creation order.

Run: `pui_ex_popups`. Full source: `examples/atomic/popups.cpp`.

### 13. Docking

`dock_space` reads an app-owned `dock_node` tree, resizes its splits, switches
tabs, and reports structural drags as a `dock_action` — the framework never
mutates the tree itself.

```cpp
dock_node root;
root.kind = DOCK_TABS;
root.panels[0] = "stats"_id; root.panel_names[0] = "Stats";
root.panel_count = 1;

const dock_action act = u.dock_space(
    "dock"_id, area, root,
    function_ref<void(uiid, rect, bool)>(
        [&](uiid panel, rect pc, bool active) { draw_panel(u, panel, pc); }));

if (act.active) apply_dock_action(root, act);   // see examples/dock_helpers.h
```

Node kinds: `DOCK_SPLIT_H`, `DOCK_SPLIT_V`, `DOCK_LEAF`, `DOCK_TABS`. Drop zones:
`DOCK_ZONE_CENTER` (tab into the target), `LEFT/RIGHT/TOP/BOTTOM` (split), and
`act.target == nullptr` (dropped outside → undock to a floating panel).
`examples/dock_helpers.h` implements the usual app-side policy (remove, collapse,
tab, split, fall back when the reported target was invalidated by a collapse).

Run: `pui_ex_dock`. Full source: `examples/atomic/dock.cpp`.

### 14. Multiple windows

One `render_device` serves several windows; each window needs its own
`render_surface` and requires `backend_caps::SHARED_DEVICE`. State is per window:
input queue, focus list, active/drag widget, blur scratch. The pointer position
and the held button are global.

```cpp
window *win1 = add_window(ctx, sdl1, surface1, rect::make(0, 0, 800, 600));
window *win2 = add_window(ctx, sdl2, device->create_surface(sdl2), rect::make(820, 40, 520, 420));

// frame them one after another
begin_frame(ctx, *win1, now, dt);
{ ui u(ctx); frame_a(u); }
end_frame(ctx);
surface1->present();

begin_frame(ctx, *win2, now, dt);
{ ui u(ctx); frame_b(u); }
end_frame(ctx);
surface2->present();
```

`set_window_client(w, rect)`, `remove_window(ctx, w)`, `focus_window(ctx, w)`,
`focused_window(ctx)`, `desktop_rect(ctx)`, `set_global_mouse(ctx, x, y)`.
Feed motion from window-local events so the coordinates match `window::client`.

Run: `pui_ex_windows`. Full source: `examples/atomic/windows.cpp`.

### 15. Shapes and images

```cpp
u.draw_rect(r, c);
u.draw_rounded_rect(r, c, 8.0f);                 // antialiased
u.draw_line(x0, y0, x1, y1, c, 2.0f);            // horizontal/vertical
u.draw_polygon({{0,0},{40,0},{20,30}}, c);       // convex, feathered edge
u.draw_sector(center, r_in, r_out, a0, a1, c);   // pie slice / ring
u.draw_arc(center, radius, thickness, a0, a1, c);
u.draw_triangles(nullptr, verts, 4, idx, 6);     // custom vertex geometry

texture_handle tex = device->create_texture(w, h, rgba);
u.draw_image(skin_image{tex, {0,0,1,1}, (f32)w, (f32)h}, dst);
u.draw_nine_slice(make_skin_image(tex, w, h, {0,0,w,h}, 6, 6, 6, 6), dst);
```

`vertex` is `{x, y, u, v, color}`; `nullptr` for the texture means solid vertex
color. Rounded rects, polygons, sectors and arcs are antialiased with a 1px alpha
feather; the straight radial edges of sectors are not.

Run: `pui_ex_drawing`. Full source: `examples/atomic/drawing.cpp`.

### 16. Blur

```cpp
u.blur(panel_bounds, 14.0f, 10.0f, 1.0f);   // radius, corner radius, alpha
panel_scope p = u.panel("Frosted", panel_bounds, PANEL_NO_CONTROLS, 0, nullptr,
                        panel_override{.bg = some(color{20, 24, 30, 120}),
                                       .titlebar_bg = some(color{20, 24, 30, 160})});
```

`blur` samples what has been drawn so far *behind* the rect and composites a
blurred copy over it; call it before drawing the surface that sits on top.
`alpha` controls that composite: **1 replaces the content** (what you want for
frosted glass — from ~5px on, text underneath is no longer readable), while lower
values blend toward the sharp original and are meant for appear/fade animations,
not for obscuring. The sampling reaches a little outside the rect for a correct
blur, but the composite stays **inside** the rect: it is pixel-aligned with the
scene (no shift) and honors `corner_radius`, so a rounded panel gets a rounded
blur. It needs `backend_caps::RENDER_TARGETS`; without it the core degrades to a
flat tint (`test_blur_fallback`), so a backend port can start without render
targets.

The radius is approximated with the only sampling primitive a backend must
provide (scaled blits): the pyramid depth is chosen from it (quarter → eighth →
sixteenth) and a few down/up round trips run at that level, so the response is
**monotonic but stepwise**, not a linear kernel. The golden `blur_f1` scene pins
the progression at r=3, 12 and 28.

**The frosting recipe matters:** blur only shows through a *translucent* surface.
A panel (or card/button) whose background alpha is ~245 hides it completely — pass
a `panel_override` with `bg` and `titlebar_bg` around 110–160, and use
`blur(..., alpha = 1.0)` so the sharp content underneath is actually replaced.
`button`, `card` and `panel` draw their outline as a **ring** (not as a filled
rect behind the background), so translucent and fully transparent backgrounds
(outline buttons) composite correctly with whatever is behind them.

`pui_ex_blur` shows the same scene raw on the left and blurred on the right, so
the smear is unmistakable.

Run: `pui_ex_blur`. Full source: `examples/atomic/blur.cpp`.

### 17. Animation

Animations are keyed values that move toward a target across frames; call them
every frame with the same key. Unused keys are collected after 5 seconds.

```cpp
if (u.button(btn, "Toggle", "toggle"_id)) open = !open;

// tween: fixed duration + easing curve
const f32 t = u.animate("slide"_id, open ? 1.0f : 0.0f,
                        tween{.duration = 0.5f, .curve = easing::EASE_OUT_BACK});
u.draw_rounded_rect(rect::make(20 + 300 * t, 40, 60, 30), th.accent, 6);

// spring: physics, keeps velocity across retargets
const f32 s = u.animate("grow"_id, open ? 1.0f : 0.0f, spring{260.0f, 0.7f});

// smooth: exponential follow (pointers, scroll)
const f32 f = u.smooth("follow"_id, open ? 1.0f : 0.0f, 0.25f);

// appear: fade/scale in once per key; animate_color: interpolate colors
const f32 a = u.appear("intro"_id, 0.5f);
const color c = u.animate_color("tint"_id, open ? th.accent : th.widget_hover, tween{});

bool busy = u.animations_active();
u.set_reduced_motion(reduced);   // snaps to targets
```

Easings: `LINEAR`, `EASE_IN`, `EASE_OUT`, `EASE_IN_OUT`, `EASE_OUT_BACK`
(`ease(curve, t)` evaluates one). `transition{duration, curve}` on
`theme.button.transition` animates built-in hover/active colors.

Run: `pui_ex_animation`. Full source: `examples/atomic/animation.cpp`.

### 18. Theming

Everything visual comes from a `theme` you own: colors, metrics, style groups
(`button`, `panel`, `card`, `scrollbar`), roles, fonts and cursors.

```cpp
theme t = default_dark();
t.font = app_font;
t.text_size = 15.0f;
t.accent = {60, 180, 255, 255};
t.radius = 6.0f;
t.border_thickness = 1.0f;           // full widget outline; 0 = flat
t.panel.shadow = true;
t.scrollbar.thickness = 8.0f;
t.button_cursor = CURSOR_HAND;       // hover cursor for buttons
t.decimal_separator = ',';           // numeric fields
set_button_role(t, "primary"_id, button_override{.bg = some(t.accent)});
set_theme(ctx, t);                   // copies; call again to change live
```

`text_style` (chapter 6) overrides font/size per scope; `button_override`,
`card_override`, `panel_override` and `style_scope` override per instance or per
scope. `theme` uses `opt<T>` (`some(v)` / `none`) for partial overrides that
cascade theme → role → scope → instance.

`default_dark()` is a layered dark ramp (background < surface < elevated) with a
soft 1px outline on controls, 7–12px radii, an 8px spacing rhythm and a muted
scrollbar; the demo, tour and examples all start from it, so overriding a few
fields (accent, radius, fonts) is usually enough to make an app look intentional.

Run: `pui_ex_theme`. Full source: `examples/atomic/theme.cpp`.

### 19. Custom widgets

Built-in widgets use only the public API; yours can too. `interact` gives you the
pointer state for a rect, and the drawing primitives do the rest.

```cpp
static bool toggle_switch(ui &u, rect r, bool &value, uiid id)
{
    interaction in = u.interact(id, r);
    if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);

    const theme &th = u.th();
    const f32 h = 22.0f;
    const rect track = {r.x, r.y + (r.h - h) * 0.5f, 42.0f, h};
    const f32 t = u.animate(id_child(id, 1), value ? 1.0f : 0.0f, tween{.duration = 0.15f});
    u.draw_rounded_rect(track, value ? th.accent : th.widget_bg, h * 0.5f);
    const f32 d = h - 6.0f;
    u.draw_rounded_rect({track.x + 3.0f + (track.w - d - 6.0f) * t, track.y + 3.0f, d, d},
                        th.text, d * 0.5f);

    if (in.clicked) { value = !value; return true; }
    return false;
}
```

`interaction` fields: `hovered`, `pressed` (held), `activated` (press edge),
`held`, `clicked`, `double_clicked`, `right_clicked`, `focused`, `disabled`,
`captured`, `blocked`. `u.hot_id()` is the widget under the pointer this frame.

Run: `pui_ex_custom`. Full source: `examples/atomic/custom.cpp`.

### 20. State and view

One app-owned state per screen; widgets write model fields directly. Derive a
per-frame read-only view for whatever the model does not store:

```cpp
struct app_state
{
    i32 clicks = 0;
    std::string name;
    struct view_t { char clicks_label[32]; } view;
    void update_view() { std::snprintf(view.clicks_label, sizeof(view.clicks_label),
                                       "Clicks: %d", clicks); }
};

app_state s;
// each frame:
s.update_view();
ui u(ctx);
u.text(r, s.view.clicks_label, th.text, ALIGN_LEFT);
if (u.button(r2, "Click me", "click"_id)) s.clicks += 1;
```

There is no intent queue and no reducer (removed in r53); components read
`s.view` and write `s.<field>`. See [`docs/model_view.md`](docs/model_view.md)
and the `pui_counter` example.

Run: `pui_counter`. Full source: `examples/pui_counter.cpp`.

### 21. Headless and custom backends

The whole core runs without a window: `null_device` implements the contract and
counts what it receives, which is how the test suite drives it.

```cpp
null_device nd;
context *c = create_context(&nd, nd.create_surface());

// capture expected violations instead of printing them
set_violation_handler(c, my_handler, nullptr);

begin_frame(c, 0.0, 1.0 / 60.0, rect::make(0, 0, 320, 200));
{
    ui u(c);
    u.draw_rect({0, 0, 320, 200}, color{18, 20, 26, 255});
    if (u.button({10, 10, 120, 26}, "Headless", "btn"_id)) { /* ... */ }
}
end_frame(c);
i32 problems = violation_count(c);
```

`PUFFERUI_CHECK(cond, "message")` reports contract violations (invalid slices,
clip imbalance, duplicate ids, …) through `report_violation`; the default prints
to stderr, `set_violation_handler` routes them to your log or test.

A backend implements two interfaces — `render_device` (textures, clear, clip,
`draw`) and `render_surface` (target, size, present) — plus `backend_caps`
(`SCISSOR`, `RENDER_TARGETS`, `STREAMING_TEXTURES`, `SHARED_DEVICE`). The
`pui_ex_renderer` example is a complete CPU rasterizer that writes a PPM; read it
together with [`docs/porting_a_backend.md`](docs/porting_a_backend.md).

Run: `pui_ex_headless`, `pui_ex_renderer`. Full source:
`examples/atomic/headless.cpp`, `examples/atomic/renderer.cpp`.

### 22. Component patterns (gallery)

`pui_ex_patterns` is a gallery of composites built from PufferUI's public API,
recreated from patterns in modern UI libraries and design guidelines:

- [coss.com/ui](https://coss.com/ui) — component set and naming (toast, kbd,
  meter, drawer, segmented control, table, breadcrumb, pagination, alert).
- [ui-skills.com](https://www.ui-skills.com/) — the playbook: scale feedback on
  press, 44 px touch targets, right-aligned numbers for data, consistent radii.
- [emilkowal.ski/ui/you-dont-need-animations](https://emilkowal.ski/ui/you-dont-need-animations)
  — motion rules applied throughout: animations stay under ~300 ms, they have a
  purpose, **keyboard-initiated surfaces are never animated** (the command
  palette appears instantly), tooltips wait before the first appearance and then
  open instantly while you move between them, and toasts enter and leave in the
  same direction so the motion reads as spatial.
- [reui.io](https://reui.io/components) and
  [designsystemchecklist.com](https://www.designsystemchecklist.com/) — the
  component/system checklists that shaped what the gallery covers.

The gallery includes a toast stack with actions and stacking, tooltips with the
delay rule, a ⌘K command palette with filter + arrow-key navigation, a
right-edge drawer with a scrim, an accordion whose chevron rotates and body
height animates, a spinner/skeleton/progress/meter/ring loading set, segmented
control, switches, radio group, input group with validation, tag chips with
dismiss, kbd chips, avatar stack, an empty-state alert, a sortable table with
breadcrumb and pagination, and a copy button that morphs to “Copied!” (delight —
kept to infrequent actions only).

The toast pattern is the one worth copying:

```cpp
// enter and leave from the same direction, ~250ms, auto-dismiss with an action
t.slide = u.animate(id_child("toast"_id, t.id), t.leaving ? 0.0f : 1.0f,
                    spring{.stiffness = 420.0f, .damping_ratio = 0.85f});
const rect r{width - 298.0f, height - 24.0f - (i + 1) * 54.0f + (1.0f - t.slide) * 60.0f, 280, 46};
```

Run: `pui_ex_patterns`. Full source: `examples/atomic/patterns.cpp`.

### 23. Expanders (collapsible sections)

`pui_ex_expander` is a dedicated example for disclosure widgets with animated
height: independent expanders, an accordion (one open at a time), nesting, and
expanders inside a scrolled list — with a tween/spring switch, a duration slider
and a reduced-motion toggle.

Two patterns make it work:

1. **Measure first, then draw with the returned value.** `ui.animate` advances the
   animation, so it must run exactly once per key per frame: compute the animated
   height, then lay out and draw with it — that is what moves the following
   content smoothly.

```cpp
const f32 t = u.animate(id_child(id, 1), open ? 1.0f : 0.0f,
                        tween{.duration = 0.22f, .curve = easing::EASE_OUT});
const f32 height = HEADER_H + (t > 0.004f ? GAP + content_h * t : 0.0f);
const rect area = col.next(height);          // the list makes room as it grows
```

2. **Clip the body while it grows.** Lay the body out at its full size inside a
   `region` whose height is the animated height, so the content is *revealed*
   instead of reflowing (and nothing spills mid-animation):

```cpp
region body_reg(u, id_child(id, 2), {area.x, area.y + HEADER_H + GAP, area.w, content_h * t});
draw_body(u, body_reg.content());
```

The chevron is a small `draw_polygon` rotated by `t * 90°`, and row summaries
fade out with `alpha * (1 - t)`. The example also shows why UI motion stays under
~300 ms and how `set_reduced_motion(true)` snaps every animation for users who
ask for it.

Run: `pui_ex_expander`. Full source: `examples/atomic/expander.cpp`.

---

### 24. Combo boxes, tooltips and context menus

The popup family ships as three one-call widgets. All three paint at the
**end of the frame** — after every base widget — so nothing drawn after their
anchor can ever cover them (immediate mode paints in call order; popup
surfaces must go last). A consequence: a mouse pick resolves at end-of-frame,
the model write-back lands the same frame, and the widget's *next* call
reports the change (keyboard picks resolve in-frame).

- **`combo`** — a closed header showing the current item; a click opens a
  dropdown popup under it. Items are plain `interact` rects, so hover and
  Up/Down arrows share one highlight counter; Enter picks, Escape or a click
  outside closes. Returns true when the selection changed.

```cpp
static const char *kinds[4] = {"Sprite", "Emitter", "Light", "Camera"};
if (u.combo(row, "", kinds, 4, kind, "kind"_id, 180.0f))
    apply(kind); // changed this frame (keyboard) or on the next call (mouse)
```

The dropdown sizes to its content (width fits the widest item) and clamps to
`max_popup_height` and the window bounds — a clamped list scrolls inside the
panel with a floating bar, so every item stays reachable; near the bottom
edge it flips above the header.

- **`tooltip`** — call it after the anchor widget with the *same* `uiid`: it
  draws nothing unless that widget is `hot`. The first tooltip waits ~0.5 s;
  another one within ~1.2 s of the last opens instantly, so moving across a
  toolbar does not blink a box under the pointer for every stop. Returns the
  placement rect (zero when silent).

```cpp
if (u.button(r, "Save", "save"_id)) save();
u.tooltip(r, "save"_id, "Write the scene to disk");
```

- **`context_menu`** — opens on right-press over the anchor, at the pointer,
  clamped to the window; the app keeps no open flag (the menu state lives in
  the popup table under the menu's id). Returns the picked index, or -1
  (picked on the next call after the click).

```cpp
static const char *actions[3] = {"Fit to view", "Duplicate", "Delete"};
if (const i32 picked = u.context_menu("canvas_menu"_id, canvas, actions, 3); picked >= 0)
    run(actions[picked]);
```

All three draw from `theme` colors and reuse `popup_scope`'s input rules, so a
menu blocks the base UI for its lifetime, closes on Escape (context menus) or
click-outside, and never animates on keyboard interaction (motion rules from
chapter 22).

Run: `pui_ex_combo`. Full source: `examples/atomic/combo.cpp`.

---

### 25. Borderless windows: custom titlebars and the event pump

Examples (and any app) run **borderless**: the OS titlebar is replaced by a
custom one drawn in the window's own frame, so the UI owns its whole surface.

- **`ui.titlebar(window, rect&, title)`** — a draggable titlebar with
  minimize / maximize / close glyph buttons and double-click maximize.
  Platform actions go through a small interface the backend provides:

```cpp
struct window_host
{
    virtual void move_window(window &w, f32 desktop_x, f32 desktop_y) = 0;
    virtual void resize_window(window &w, f32 width, f32 height) = 0;
    virtual void minimize_window(window &w);
    virtual void toggle_maximize(window &w); // maximize <-> restore
};
```

The SDL3 glue installs one automatically (with `sdl3_route`/`sdl3_pump`, see
below); a headless or custom backend can install its own with
`set_window_host`, or none — the bar still draws and reports clicks, but
moving does nothing. A close click sets `window::close_requested` and the
app decides what that means (quit the app, remove the window, show a
confirmation).

- **Native window chrome** — the SDL3 glue installs a hit-test
  (`install_window_chrome`): the bar minus its buttons maps to the system
  caption (drag, Aero Snap with its preview, maximize on double-click), and
  the 8px edge bands map to the native resize borders. Chrome needs no
  per-frame work and keeps the OS behaviors users already know; without a
  host the bar still draws and reports clicks, but moving does nothing. A
  close click sets `window::close_requested` and the app decides what that
  means (quit the app, remove the window, show a confirmation).

- **`sdl3_pump(context*)` / `sdl3_route(context*, void* event)`** — one call
  routes every SDL event to the right window: pointer, left + right buttons,
  wheel, keys with modifiers, text input, IME preedit, focus and window
  geometry (clients stay in sync with SDL). Apps no longer hand-wire a switch
  per event type; the pump returns true on SDL_EVENT_QUIT, and window-close
  requests surface as `window::close_requested` for app policy:

```cpp
if (sdl3_pump(ctx)) return quit;         // poll + route everything
// or, in SDL_AppEvent style:
if (sdl3_route(ctx, event)) return quit; // route one polled event
```

With `example_common.h`, all of this is automatic: windows are created
borderless, `example_begin_page` draws the chrome, and `example_pump` applies
the close policy (the primary window quits, an extra window is removed).

### 26. Keyboard focus: the Tab ring

Every widget is reachable from the keyboard with nothing wired up. Each frame
`interact` records every enabled widget into a per-window ring in submission
order (the order widgets paint), and Tab walks it:

```cpp
// in begin_frame, before your widgets run:
// Tab moves c->focus to the next ring entry (the first entry when nothing is
// focused); Shift+Tab goes back. Fields included — one ring for everything.
```

- **Focus rectangle** — a widget that owns `c->focus` reports
  `interaction.focused` and draws the theme's focus outline
  (`focus_border` / `focus_border_thickness`, 1.5px in `default_dark`).
- **Activation** — a focused button or checkbox responds to Enter and Space
  like a click; a focused combo opens on Enter; a focused slider adjusts with
  Left/Right (5% of the range per press, Shift for 1%).
- **Text fields keep their own keys** — Enter commits (blurs), Escape blurs,
  and Tab focus selects the field's value so typing replaces it. Tab itself
  moves the ring; the field never swallows it.
- **Focus is per window** (the ring lives on the window, like every other
  widget store), and a focused popup traps it — widgets under a popup layer
  are blocked and cannot take focus.
- **Custom widgets** join by calling `interact` — the ring and the outline
  helper come for free. To draw the outline yourself, use the theme's focus
  colors when `in.focused`.

Run: `pui_ex_keyboard`. Full source: `examples/atomic/keyboard.cpp`.

### 27. Virtual lists

A uniform list can be far longer than what a frame submits. `virtual_list`
computes the visible slice of the item grid and calls you back only for rows
that can paint — the cost of a frame follows the viewport, not the list
length:

```cpp
scroll_view sv = u.scroll(body, "rows"_id);
sv.virtual_list(5000, 26.0f, [](ui &u, i32 index, rect row) {
    u.text(row.pad(8.0f, 0.0f), row_label(index), u.th().text, ALIGN_LEFT);
});
// no set_content_height(): it derives from count * row height
```

- **The slice is clamped to the list** — the callback never runs for an index
  past the end, and a partial row straddles each viewport edge.
- **Content height is derived**, so the gutter and the scroll extent both
  follow from `count * item_height`.
- **Only visible widgets exist this frame** — they are the ones in the Tab
  ring, and their keyed state (animation, scroll, edits) purges after the
  usual idle retention when scrolled away.
- **Irregular rows** (variable heights, section headers) keep using manual
  slicing with `sv.content()` and an explicit `set_content_height` — the
  report when you skip rows past `MAX_FRAME_IDS` is your sign to virtualize.

Run: `pui_ex_vlist`. Full source: `examples/atomic/vlist.cpp`.


---

## Examples index

| Target | Source | Shows |
| --- | --- | --- |
| `pui_tour` | `examples/pui_tour.cpp` | **every capability**, chapter by chapter |
| `pui_demo` | `examples/pui_demo.cpp` | two-window showcase: dock, floating panels, HUD, transitions |
| `pui_counter` | `examples/pui_counter.cpp` | state/view + direct write-back |
| `pui_ex_hello` | `examples/atomic/hello.cpp` | context, window, frame loop, card, raw drawing |
| `pui_ex_bootstrap` | `examples/atomic/bootstrap.cpp` | the one-call bootstrap: `sdl3_app_init/pump/tick/shutdown`, native chrome, dev overlay — **start here** |
| `pui_ex_rect` | `examples/atomic/rect.cpp` | cuts, ratio cuts, slices, align, fit_aspect |
| `pui_ex_cursors` | `examples/atomic/cursors.cpp` | `column`/`row`, spacing, clamping |
| `pui_ex_tracks` | `examples/atomic/tracks.cpp` | `track_size`, `track_row`/`track_column`, splitters |
| `pui_ex_grid` | `examples/atomic/grid.cpp` | `auto_fit_grid`, `grid_cursor` |
| `pui_ex_ids` | `examples/atomic/ids.cpp` | regions, `local`/`auto_id`/`id_child`, id scoping |
| `pui_ex_text` | `examples/atomic/text.cpp` | align, ellipsis, wrap, fit, measure, fonts, UTF-8, kerning |
| `pui_ex_buttons` | `examples/atomic/buttons.cpp` | roles, overrides, `style_scope`, transitions |
| `pui_ex_inputs` | `examples/atomic/inputs.cpp` | `text_field`, `number_field`, focus, clipboard, IME |
| `pui_ex_widgets` | `examples/atomic/widgets.cpp` | `checkbox`, `slider_float`, `progress_bar` |
| `pui_ex_panels` | `examples/atomic/panels.cpp` | `card`, `panel`, flags, close requests |
| `pui_ex_scroll` | `examples/atomic/scroll.cpp` | scroll views, modes, `ensure_visible`/`scroll_to` |
| `pui_ex_popups` | `examples/atomic/popups.cpp` | popup flags, menus, modal, focus trap |
| `pui_ex_dock` | `examples/atomic/dock.cpp` | `dock_space`, tabs/splits, drag-drop, undock |
| `pui_ex_windows` | `examples/atomic/windows.cpp` | multi-window, shared device, per-window input |
| `pui_ex_drawing` | `examples/atomic/drawing.cpp` | primitives, polygons/sectors/arcs, textures, 9-slice |
| `pui_ex_blur` | `examples/atomic/blur.cpp` | `blur` + capability fallback |
| `pui_ex_animation` | `examples/atomic/animation.cpp` | tween, spring, smooth, appear, color, reduced motion |
| `pui_ex_theme` | `examples/atomic/theme.cpp` | live colors/metrics/roles, light/dark |
| `pui_ex_custom` | `examples/atomic/custom.cpp` | `interact` + primitives, `interaction` fields |
| `pui_ex_patterns` | `examples/atomic/patterns.cpp` | component gallery: toast, tooltip, ⌘K palette, drawer, accordion, loading states, table, … |
| `pui_ex_expander` | `examples/atomic/expander.cpp` | expanders with animated height: accordion, nesting, motion controls |
| `pui_ex_combo` | `examples/atomic/combo.cpp` | `combo` dropdown, delayed tooltips, right-press context menu |
| `pui_ex_keyboard` | `examples/atomic/keyboard.cpp` | the focus ring: Tab/Shift+Tab, Enter/Space activation, arrow keys |
| `pui_ex_vlist` | `examples/atomic/vlist.cpp` | `virtual_list`: 5,000-row scroll, only the visible slice submitted |
| `pui_ex_headless` | `examples/atomic/headless.cpp` | `null_device`, violations, caps, no display |
| `pui_ex_renderer` | `examples/atomic/renderer.cpp` | a CPU `render_device` writing a PPM |

Every example supports:

```sh
pui_ex_<name> --selftest                 # offscreen, scripted input, no violations
pui_ex_<name> --selftest --frames 10     # more frames
pui_ex_<name> --size 1280x720            # override the size
pui_ex_<name> --selftest --resize 520x420  # resize mid-run (exercises re-layout)
pui_ex_<name> --selftest --screenshot shot.bmp
pui_ex_<name> --help                     # (prints the same list)
```

Examples lay out from the live client size, so they re-flow when the window is
resized.

All examples share one visual language, provided by `examples/example_common.h`:
a page header (bold title + subtitle + divider) and card sections with a title,
a caption and a divider. New examples should start from it:

```cpp
static void my_frame(ui &u, example_app &app)
{
    example_page page = example_begin_page(u, app, "My example", "one-line summary");
    column col(page.content, example_ui::SECTION_GAP);

    rect body = example_section(u, col.next(120.0f), "A section", "what it shows",
                                app.font_bold);
    column rows(body, 8.0f);
    (void)u.button(rows.next(example_ui::ROW_H), "Do it", "my_btn"_id, "primary"_id);
}
```

---

## API reference

Everything below is declared in `include/pufferui/pufferui.h`, in `namespace pui`.

### Core types

| Name | Notes |
| --- | --- |
| `context` | Owns the frame, windows, theme, draw list and keyed state. Created with `create_context`, destroyed with `destroy_context`. |
| `window` | One window: `client` (desktop coords), `area` (local), input queue, focus list, active/drag state. |
| `ui` | Per-frame facade: `ui u(ctx)`; all drawing/layout/interaction goes through it. |
| `rect`, `color`, `vec2` | Value types. `color` is straight RGBA8. |
| `uiid` | 64-bit widget id; `"Name"_id`, `id_child(parent, salt)`, `ui.local(key)`, `ui.auto_id()`. |
| `f32/f64`, `u8..u64`, `i8..i64`, `usize` | Fixed-width aliases used everywhere. |
| `font_handle`, `texture_handle` | `i32` / `void*` handles; `FONT_INVALID` = -1. |
| `opt<T>` / `some(v)` / `none` | Partial overrides in `*_override` structs. |
| `function_ref<Sig>` | Non-owning callable reference (dock callbacks, `ui.measure`). |
| `clipboard` | Abstract `get(std::string&)` / `set(std::string_view)`; install with `set_clipboard`. |
| `measure_size` | `{f32 width, height}` returned by measurement. |
| `vertex` | `{f32 x, y, u, v; color c}` — the only geometry format backends see. |

### Rect algebra (`rect`)

`cut_top`, `cut_bottom`, `cut_left`, `cut_right` · `cut_top_ratio`,
`cut_bottom_ratio`, `cut_left_ratio`, `cut_right_ratio` · `top_slice`,
`bottom_slice`, `left_slice`, `right_slice` · `align_center`, `align_h_center`,
`align_v_center`, `align_left`, `align_right`, `align_top`, `align_bottom` ·
`fit_aspect` · `pad(amount)`, `pad(x, y)`, `pad(l, t, r, b)` · `intersect`,
`contains(x, y)`, `contains(rect)` · `left/right/top/bottom`, `center_x`,
`center_y` · `is_finite`, `is_valid`, `make(x, y, w, h)`.

### Layout

| Name | Notes |
| --- | --- |
| `column`, `row` | Cursors: `next(size)`, `cut_top/bottom` / `cut_left/right`, `space(amount)`, `remaining()`. Both clamp. |
| `track_size` | `fixed(px)`, `flex(weight)`, `ratio(fraction)`, `fit_content(fallback, max)`, `.min(px)`, `.max(px)`. |
| `track_row`, `track_column` | Cursors over resolved tracks: `next()`, `remaining()`, `size(i)`, `count()`, `index()`, `done()`. |
| `resolve_track_sizes` | Resolves a track list into `f32*` (caller storage). |
| `auto_fit_grid(container, count, min_w, item_h, gap)` | CSS `repeat(auto-fit, minmax(min_w, 1fr))`. |
| `grid_cursor` | `next()`, `cell(i)`, `columns()`, `rows()`, `item_width()`, `item_height()`, `count()`, `done()`. |
| `region` | RAII: scopes ids, clips to the area (`content()`, `id()`, `clip()`, `area()`). |
| `split_horizontal_interactive` / `split_vertical_interactive` | Draggable splitters: `(id, bounds, f32* position, min, max, thickness, gap)` → `{first, second}`. |

#### Choosing a layout tool

| You are building | Use | Why | See |
| --- | --- | --- | --- |
| A linear flow of rows (a form, a settings page) | `column` | Stacks vertically with a gap; `next(height)` clamps to what is left. | `pui_ex_layout` |
| A horizontal toolbar or row of chips | `row` | Same cursor, horizontal. | `pui_ex_layout` |
| A sidebar / toolbar with a mixed fixed + flexible remainder | `track_row` / `track_column` with `fixed` + `flex` tracks | One place declares sizes; `next()` walks them. | `pui_ex_tracks` |
| Proportional panels (25% / 75%) | `track_row` with `ratio` tracks | Fractions of the container, not pixels. | `pui_ex_tracks` |
| A card / thumbnail grid that reflows with the width | `auto_fit_grid` + `grid_cursor` | CSS `repeat(auto-fit, minmax(...))` in one call. | `pui_ex_grid` |
| A user-draggable two-pane split | `split_*_interactive` | The splitter owns the drag; you store the position. | `pui_ex_split`? see the examples index |
| A group of widgets that share an id scope and clip | `region` | RAII scoping; the only one of these that creates an identity domain. | `pui_ex_rect` |
| A scrollable list | `scroll_view` (`u.scroll`) | Wheel + draggable bar + gutter modes. | `pui_ex_scroll` |

Rule of thumb: `column`/`row` until you need fixed/flex mixing (`track_row`),
a grid (`auto_fit_grid`), or an identity scope (`region`) — then use the
specialized tool.

Overlapping hit rects are allowed and have defined semantics: the **topmost**
(last submitted) enabled rect containing a position owns it. A lower rect's
`activated` report is superseded, and its release lands no click. If you want
a widget *under* another one to stay clickable, give the top one
`enabled = false` (the panel titlebar yields to its close dot this way).

### Text

`ui.text(rect, string_view, color, align)` · `ui.text_width` · `ui.measure_text`
· `ui.measure(function_ref<measure_size(f32)>, available_width)` ·
`ui.text_wrapped` · `ui.text_ellipsis` · `ui.text_fit` · `ui.line_height` ·
`ui.text_style(size, font)` → `text_scope` · `ui.has_glyph(codepoint)` ·
`load_font(ctx, path)` · `align`/`ALIGN_LEFT|CENTER|RIGHT`.
`ui.text_wrapped_height(width, s)` (the wrapped height, measured without
drawing — size containers around paragraphs).

Glyph atlases page automatically: one 1024x1024 page holds a full UI set;
when it fills, new glyphs land in additional textures (batching already
flushes on texture change), so non-Latin vocabularies scale past a single
page.

### Widgets

| Name | Notes |
| --- | --- |
| `ui.interact(id, rect, enabled)` | Pointer state for a rect → `interaction`. Also joins the widget to the Tab focus ring. |
| `ui.button(rect, label, id, role_id, override)` | Returns `true` on click — or on Enter/Space when focused. |
| `ui.checkbox(rect, label, bool&, id)` | Returns `true` on change (click, or Enter/Space when focused). |
| `ui.slider_float(rect, label, f32&, min, max, id, fmt)` | Drag/click; Left/Right adjust when focused; returns `true` while changing. |
| `ui.progress_bar(rect, fraction, fill, bg)` | Draw-only. |
| `ui.text_field(rect, std::string&, id)` | Returns `true` on change. |
| `ui.number_field(rect, f32&, id, fmt)` | Returns `true` on change. |
| `ui.card(rect, card_override)` | Themed rounded rect. |
| `ui.panel(title, rect&, flags, dock_id, dock_name, panel_override)` | Floating panel scope; flags + per-panel style override (translucent backgrounds for frosted glass). |
| `ui.popup(id, rect, flags)` | Top input-capturing layer. |
| `ui.titlebar(window, rect&, title)` | Borderless chrome: paints the bar + min/max/close; buttons route through `window_host`. Native dragging/resizing/Aero Snap come from `install_window_chrome`. |
| `window_host`, `set_window_host`, `install_window_chrome` | Platform actions for the custom chrome; `install_system_chrome` maps the bar to the system caption (native drag + snap) and adds native resize bands. |
| `sdl3_pump(ctx)` / `sdl3_route(ctx, void* event)` | One-call SDL event routing; true on quit. |
| `ui.combo(rect, label, items, count, i32&, id, max_popup_h)` | Dropdown; returns `true` on change. |
| `ui.tooltip(rect anchor, id, text)` | Delayed tooltip; returns the placement rect (zero when silent). |
| `ui.context_menu(id, rect anchor, items, count)` | Right-press menu; returns the picked index or -1. |
| `ui.scroll(viewport, id, scroll_options)` | Scrollable viewport scope. |
| `ui.dock_space(id, rect, dock_node&, callback)` | Dock host; returns a `dock_action`. |
| `button_style`, `button_override`, `button_role`, `set_button_role`, `style_scope`, `resolve_button_style` | Button styling cascade. |
| `panel_style`/`panel_override`/`panel_flags`/`PANEL_*` | Panel styling and flags. |
| `card_style`/`card_override` | Card styling. |
| `scroll_style`, `scroll_options`, `SCROLL_*` | Scrollbar styling and modes. |
| `popup_flags`, `POPUP_*` | Popup behavior. |
| `widget_state`, `interaction`, `cursor`, `CURSOR_*` | Interaction results and cursors. |

#### Widget write-back semantics

Immediate-mode widgets need a clear rule for *when the model changes* and
*what the return value means*. The rules differ per widget by design:

| Widget | Model changes | Return value |
| --- | --- | --- |
| `ui.button` | never | `true` on the click edge (press + release inside) — or on Enter/Space when the button owns keyboard focus. |
| `ui.checkbox` | immediately when the release flips it (or an Enter/Space on the focused box) | `true` on that change. |
| `ui.slider_float` | continuously while dragging; Left/Right by ±5% of the range (Shift 1%) when focused | `true` while the value changes. |
| `ui.text_field` | while focused (every edit), on focus loss, and after framework edits such as a text drop — a focused buffer is the source of truth | `true` when the value changed. |
| `ui.number_field` | same rules as `text_field` | `true` when the value changed. |
| `ui.combo` | on the pick, but reported by the NEXT call (the pick resolves at end-of-frame) | `true` when the selection changed (previous pick). |
| `ui.context_menu` | no model | the picked index, or -1. |
| `ui.scroll` | no model (offset state is keyed per id) | a `scroll_view` scope; `overflows()`/`offset()`/`content_height()`. |
| `ui.dock_space` | no model (structural changes come back as a `dock_action`) | the action to apply (app-side). |

The combo / context-menu / tooltip pick is resolved at the end of the frame
because later widgets must not change what the pointer lands on; the write-
back lands in the same frame and the widget's next call reports it. A
focused field's buffer is authoritative: other widgets must not edit a
field's buffer while it is focused.

### Drawing and images

`ui.draw_rect` · `ui.draw_line` (axis-aligned) · `ui.draw_rounded_rect` ·
`ui.draw_polygon(span<vec2>, color)` · `ui.draw_sector(center, r_in, r_out, a0,
a1, color)` · `ui.draw_arc(center, radius, thickness, a0, a1, color)` ·
`ui.draw_triangles(texture, vertices, count, indices, count)` ·
`skin_image`, `make_skin_image`, `SKIN_CENTER_STRETCH|TILE|NONE` ·
`ui.draw_image`, `ui.draw_nine_slice` ·
`render_device::create_texture/update_texture/destroy_texture`.

### Animation

`ui.animate(key, target, tween|spring)` · `ui.animate_global(key, target, spec)`
· `ui.smooth(key, target, half_life)` · `ui.appear(key, duration, curve)` ·
`ui.animate_color(key, target, tween)` · `ui.animations_active()` ·
`ui.set_reduced_motion(bool)` · `easing`/`ease` · `tween` · `spring` ·
`transition`.

### Theme

`theme` (colors, radius, border_thickness, focus_border_thickness, spacing,
padding, font, text_size, selection, caret, decimal_separator, button_cursor,
text_cursor, `button`, `button_roles`, `panel`, `card`, `scrollbar`) ·
`default_dark()` · `set_theme(ctx, theme)` · `set_button_role(theme&, id,
button_override)`.

### Frame, windows and input

| Name | Notes |
| --- | --- |
| `create_context(device, surface)` / `destroy_context` | Context lifetime. |
| `set_device(ctx, device, surface)` | Swap backend/surface. |
| `add_window(ctx, native, surface, client)` / `remove_window` | Window registration. |
| `set_window_client`, `focus_window`, `focused_window`, `desktop_rect` | Window management. |
| `begin_frame(ctx, window&, now, dt)` / `begin_frame(ctx, now, dt, rect)` / `end_frame` | Frame loop (per window / primary). |
| `mouse_move`, `mouse_button(down)`, `mouse_button(button, down)`, `mouse_wheel`, `key_event`, `text_input_event`, `ime_event`, `mods_event` | Input, `window&` and `context*` overloads. |
| `set_global_mouse(ctx, x, y)` | Set the shared pointer position (same space as `window::client`). |
| `set_clipboard(ctx, clipboard*)` | Install the app clipboard. |
| `key`, `KEY_COUNT`, `pointer_button` | Input enums. |
| `render_device`, `render_surface`, `backend_caps`, `has_cap`, `create_sdl3_device`, `destroy_sdl3_device`, `null_device`, `null_surface` | Backend contract. |
| `PUFFERUI_CHECK`, `set_violation_handler`, `violation_handler`, `violation_count`, `violation_last`, `draw_violation_overlay`, `report_violation`, `set_current_context` | Diagnostics; the overlay draws count + last message for development. |

#### Limits

The context is a fixed-size struct; its capacities are compile-time constants.
Exceeding one is a *reported violation* (never silent UB), and the call
degrades to the documented behavior:

| Constant | Value | Notes |
| --- | --- | --- |
| `MAX_WINDOWS` | 8 | The window list's *initial* capacity — it grows on demand, so there is no window-count limit. |
| `MAX_CLIP_DEPTH` | 64 | nested region/scroll/panel clips. |
| `MAX_STYLE_SCOPES` | 16 | `style_scope` nesting. |
| `MAX_POPUPS` | 8 | popup layers per frame. |
| `MAX_PANELS` | 16 | floating panels per frame. |
| `MAX_DOCK_PANELS` / `MAX_DOCK_DEPTH` | 8 / 8 | the dock tree. |
| `MAX_TRACKS` | 16 | tracks per `track_row`/`track_column`. |
| `MAX_ID_DEPTH` | 64 | `region`/`id_child` nesting. |
| `MAX_FRAME_IDS` | 2048 | distinct region ids per frame (overflow now reports `VIOL_FRAME_IDS_OVERFLOW`; virtualize long lists). |
| `MAX_BUTTON_ROLES` | 16 | button roles per theme. |

Sizes beyond a limit are rejected by the guard and the operation is a no-op;
the violation message names the limit, so an assert in dev catches it
immediately. Raise a constant in the header if your app genuinely needs more.

#### Violations are enumerated and defined in the header

Every contract guard carries a stable code from the `violation_code` enum —
`violation_last_code(ctx)` hands the last one back, `violation_code_name`
renders it. In **debug builds** (`NDEBUG` undefined, MSVC/clang), a violation
with *no handler installed* stops the debugger right at the violating call:
tests install a handler, so they capture and assert instead of breaking, and
retail builds keep the cheap detection but never stop.
`set_break_on_violation(ctx, false)` opts out.

| Code | Guard text | Meaning |
| --- | --- | --- |
| `VIOL_INVALID_SLICE` | "`X` produced an invalid slice", "column/row slice is invalid", splitter bounds, `region corner` | A cut or cursor produced a degenerate/negative slice — the call clamps to the previous rect. |
| `VIOL_INVALID_RECT` | "`draw_*` with invalid rect" | A draw primitive got a degenerate/negative rect. |
| `VIOL_INVALID_AREA` | "region area is invalid", "scroll viewport is invalid" | A scope's container rect is invalid. |
| `VIOL_DUP_REGION_ID` | "duplicate region id among siblings" | Two sibling regions share an id; the second is refused (its widgets would steal input). |
| `VIOL_SHARED_DEVICE` | "a second window requires backend_caps::SHARED_DEVICE" | Multi-window needs the backend capability. |
| `VIOL_REMOVE_WHILE_OPEN` | "remove_window while a frame is open" | Remove after the frame closed. |
| `VIOL_DESTROY_WHILE_OPEN` | "destroy_context with an open window frame" | Close the frame first. |
| `VIOL_FRAME_ALREADY_OPEN` | "begin_frame: another window frame is open" | One frame at a time. |
| `VIOL_WINDOW_NOT_REGISTERED` | "begin_frame: window is not registered" | A stale pointer after `remove_window`; re-fetch with `window_at`. |
| `VIOL_END_WITHOUT_BEGIN` | "end_frame without begin_frame" | Mismatched frame pair. |
| `VIOL_CLIP_UNBALANCED` | "unbalanced region clip push/pop" | A scope outlived the frame; destroy scopes before `end_frame`. |
| `VIOL_SCOPE_UNBALANCED` | "unbalanced button style scope", "button style scope overflow" | Style-scope nesting. |
| `VIOL_POPUP_OVERFLOW` | "popup stack overflow" | More than `MAX_POPUPS` layers. |
| `VIOL_COMBO_EMPTY` | "combo needs at least one item" | A combo with zero items. |
| `VIOL_LAYOUT_CLAMPED` | "column slice clamped: requested N px > remaining M px" | A requested slice was clamped; opt-in via `set_report_layout_overflow`. |
| `VIOL_NO_FONT` | "text drawn while the theme has no loaded font (load_font + set_theme)" | Text drawn while the theme has no working font: it renders nothing. The bootstrap handles this for you. |
| `VIOL_FRAME_IDS_OVERFLOW` | "more than MAX_FRAME_IDS regions in one frame; duplicate-id checking is incomplete" | More than 2048 regions in one frame; duplicate detection stops there. Virtualize long lists (`scroll_view::virtual_list`). |

---

## Using PufferUI in your project

PufferUI is one header plus one implementation TU. There are two ways to
consume it:

**In-tree** (you have the repo as a subdirectory):

```cmake
add_subdirectory(pufferui)
add_executable(my_app main.cpp src/pufferui_impl.cpp)   # or the installed copy
target_link_libraries(my_app PRIVATE pufferui::pufferui)
```

**Installed** (`cmake --install out/build/x64-release --prefix <dir>`):

```cmake
find_package(pufferui REQUIRED)
add_executable(my_app main.cpp ${PUFFERUI_IMPL})        # the impl TU path
target_link_libraries(my_app PRIVATE pufferui::pufferui)
```

Both link the *interface* target (headers + SDL3 as a transitive dependency)
and compile the implementation TU exactly once. `${PUFFERUI_IMPL}` is provided
by the package so the installed impl path stays correct.

### What to define, and where

- `PUFFERUI_ENABLE_SDL3` — define it in exactly **one** TU (the one that
  compiles the implementation) to get `create_sdl3_device`,
  `sdl3_route`/`sdl3_pump` and the `sdl3_app` bootstrap. Without it the core
  is backend-agnostic and compiles headless (see the `pui_ex_headless`
  example and `docs/porting_a_backend.md`).
- `PUFFERUI_ASSET_DIR` — define it per target as a path that ends with a
  separator; it is where the bootstrap (and `load_font`) finds the bundled
  DejaVu fonts. System fonts are the fallback, so text works even without it.

### The recommended path

Write your app on the one-call bootstrap (the README's smallest program, or
`pui_ex_bootstrap` as a runnable reference):

```cpp
sdl3_app app;
if (!sdl3_app_init(app, "my app", 640, 360, 0, nullptr)) return 1;
while (sdl3_app_pump(app))          // events + client sync; false = stop
{
    sdl3_app_tick(app);             // the frame clock
    begin_frame(app.ctx, *app.win, app.now, app.dt);
    { /* ui u(app.ctx); ... draw ... */ }
    end_frame(app.ctx);
    app.surface->present();
}
sdl3_app_shutdown(app);
```

Then load fonts, `set_theme`, and draw. For a second window, create it with
SDL yourself and `add_window(ctx, window, device->create_surface(window), client)`
(see `pui_ex_windows`); the first window is always allowed, later ones need
`backend_caps::SHARED_DEVICE`.

### Development workflow

- Build and run any `pui_ex_*` binary; they are single-purpose.
- `--violations` (or interactive runs by default) draws the
  **violation overlay** whenever the context has contract violations —
  a duplicate id or a stack overflow shows up on screen, not just in a log.
- `--selftest` runs the example offscreen with scripted input and asserts
  **zero** violations; `pui_tour` walks every capability.

---

## Testing, goldens and formatting

```sh
cmake --build out/build/x64-debug --target pui_core_tests   # or build all
out/build/x64-debug/Debug/pui_core_tests.exe                # "all core tests passed"
out/build/x64-debug/Debug/pui_golden_tests.exe              # "0 failure(s)"
cmake --build out/build/x64-debug --target examples_selftest # every example, offscreen
```

- `pui_core_tests` is the headless suite: layout/id/focus/dock/window invariants,
  expected violations captured with `set_violation_handler`, no SDL.
- `pui_golden_tests` renders fixed scenes with SDL's offscreen driver + software
  renderer and compares committed BMPs in `tests/golden/` (hash + tolerance
  compare). Regenerate with `--update` **only after eyeballing the new images**;
  failures dump `tests/golden/dump/<scene>_actual.bmp` + `_diff.bmp`.
- Formatting is `clang-format 22.1.3` with the repo `.clang-format`
  (`clang-format --dry-run --Werror` must pass); CI builds debug + release, runs
  both suites and the example selftests, and checks formatting.

## Status and roadmap

Implemented: everything in the tutorial above. Known gaps:

- `number_field` has no step buttons, integer codec or unit-suffix codec
  (deferred by decision).
- `draw_line` is axis-aligned; use `draw_polygon` for diagonals.
- Sectors/arcs feather their curved edges; their straight radial edges are not
  antialiased.
- The demo's dock node pool is fixed at 16 and an undocked panel snaps to a
  default position.
- Doxygen comments on the public API are pending.

Design documents: [`docs/model_view.md`](docs/model_view.md) (state/view
convention), [`docs/porting_a_backend.md`](docs/porting_a_backend.md) (renderer
contract), and [`AGENTS.md`](AGENTS.md) (build, test, golden and style rules).