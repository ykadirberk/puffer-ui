# 9. Where next

You have built a complete app and met the whole mental model. This last chapter is
for after: bigger exercises, a cheat sheet of everything the tutorial used, the
rough edges to know about, and where to read more.

← [8. Testing and debugging](08-testing-and-debugging.md) · [Tutorial index](README.md)

## Bigger exercises

Each one uses ideas you already have. They are ordered roughly by effort; pick the
ones that interest you.

| Exercise | Hints |
|---|---|
| **Edit a task in place.** Double-click a title and it becomes a text field. | Add `unsigned editing_id` to `todo_app`. In `todo_row`, if `editing_id == t.id` draw `u.text_field(title_rect, t.title, u.local("edit"))` and request focus; Enter or Escape ends it. `in.double_clicked` from `interact` gives the trigger. |
| **Reorder by dragging.** Grab a row's handle and move it. | `interact` → `in.activated` to start (store the id and the pointer's y offset), `in.held` to follow, release to drop; compute the target index from `ctx->mouse_y` and the row height; swap in the vector *after* the loop, like deletion. Draw the dragged row last so it floats. |
| **Due dates and priorities.** | Add fields to `todo_item`; show a small colored dot (a rounded rect with radius = size/2); pick priority with `comp::segmented`. |
| **Light and dark glass.** A theme switch that *fades*. | Two themes; `f32 t = u.animate("theme"_id, dark ? 1 : 0, tween{0.4})`; `set_theme(ctx, theme_lerp(light, dark, t))` each frame. Every color and metric in the theme, including the component slots, interpolates. Your `glass_*` helpers read `u.th()`, so they follow. |
| **"Deleted — Undo" toasts.** | `comp::toast_host` + `comp::toast_draw`: `host.push("Task deleted", 0)` from anywhere, one `toast_draw` call per frame. Keep the removed task to restore it. |
| **A command palette (Ctrl+K).** | `comp::command_palette` filters a list of commands (`add task`, `clear done`, `filter: done`) and returns which one was chosen. |
| **A settings drawer.** | `comp::drawer_scope` slides a panel over the page; put a `comp::switch_toggle` for "Reduce motion" in it, wired to `u.set_reduced_motion`. |
| **A table view.** | `comp::table` is a virtualized table with a pinned header: tasks as rows, columns for title, done, created. |
| **A second window.** | Create an SDL window and `add_window(ctx, handle, device->create_surface(window), client)`; frame it with its own `begin_frame`. The library README's chapter 14 has the pattern. |
| **Run the model on a thread.** | Networking or disk work goes on a worker; keep the UI single-threaded and hand results over through a mutex-protected mailbox, copy them into state at the top of a frame. A worker can wake the sleeping loop with `SDL_PushEvent` (a custom event). |

## Cheat sheet

### The frame

| | |
|---|---|
| `sdl3_app_init / _pump / _tick / _shutdown` | window + context bootstrap, events, clock, cleanup |
| `begin_frame(ctx, win, now, dt)` … `end_frame(ctx)` | one frame; `ui u(ctx)` inside |
| `u.ctx->now`, `u.ctx->dt` | frame clock |
| `u.request_redraw()` | "something moved: do not sleep" |
| `u.titlebar(win, rect&, title)` | custom titlebar; shrinks the rect |

### Layout

| | |
|---|---|
| `r.cut_top / bottom / left / right(n)` | remove and return a strip (`(void)` to drop it) |
| `r.pad(a)`, `pad(x,y)`, `pad(l,t,r,b)` | shrink a copy |
| `top_slice`, `bottom_slice`, … | a strip without cutting |
| `align_center(in)`, `align_right(in)`, … | place a rect inside another |
| `column(r, gap)` / `row(r, gap)` | cursors: `next(size)`, `cut_bottom`, `cut_right`, `remaining()` |
| `track_row`, `track_column`, `auto_fit_grid` | fixed/flex/ratio tracks; responsive card grids |
| `r.contains(x, y)` | hit test |

### Drawing and text

| | |
|---|---|
| `u.draw_rect / draw_rounded_rect(r, color, radius)` | solid shapes |
| `u.draw_rounded_rect(r, color, corner_radii::top(16))` | choose which corners round (`{.tl, .tr, .br, .bl}`, 0 = square); `u.blur` has the same overload |
| `button_override{.radii = some(corner_radii::right(14))}`, `field_opts{.radii = ...}` | the built-in button and text field round only some corners |
| `u.draw_line`, `draw_arc`, `draw_sector`, `draw_polygon` | strokes and fans |
| `u.draw_triangles(nullptr, verts, nv, idx, ni)` | custom vertices with per-vertex colors |
| `u.blur(r, radius, corner, alpha)` | blur what is behind `r` |
| `u.text(r, str, color, align)`, `u.textf(...)` | text; `printf`-style |
| `u.text_style(size, font)` | scope a font and size |
| `u.text_width(str)`, `u.text_ellipsis(...)`, `u.text_wrapped(...)` | measure and fit |
| `color{r,g,b,a}`, `fade(c, k)` | straight RGBA8 |

### Widgets and identity

| | |
|---|---|
| `"name"_id`, `id_child(id, salt)` | explicit ids |
| `u.scope(key)` / `u.local("part")` | ids inside loops |
| `u.button(r, label, id [, role])` | `bool` on click |
| `u.checkbox(r, label, bool&, id)` | |
| `u.text_field(r, std::string&, id)`, `u.number_field` | |
| `u.slider_float(...)`, `u.combo(...)`, `u.tooltip(...)`, `u.context_menu(...)` | more built-ins |
| `glass_button(u, r, label, id)` (tutorial) | a custom button: `smooth`ed hover/press amounts + a paint function |
| `u.scroll(rect, id)` → `virtual_list(n, h, fn)` | scrolling and virtualized lists |
| `comp::segmented / switch_toggle / radio_group / tab_bar / table / ...` | components: `(u, rect, data, {.id})` |
| `u.interact(id, rect)` | the raw material for your own widgets |
| `ctx->focus`, `ctx->focus_request` | keyboard focus |

### Animation

| | |
|---|---|
| `u.animate(key, target, tween{dur, curve})` | fixed time |
| `u.animate(key, target, spring{stiffness, damping})` | physics |
| `u.smooth(key, target, half_life)` | exponential follow |
| `u.appear(key, dur)`, `u.animate_color(...)` | enter; colors |
| `u.set_reduced_motion(bool)` | snap everything |

### Theme

| | |
|---|---|
| `theme t = default_dark(); … set_theme(ctx, t);` | the whole look |
| `set_button_role(t, "id"_id, button_override{.bg = some(...)})` | named styles |
| `theme.segmented`, `.tabs`, `.table`, … | component slots; override one with `.style = &mine` |
| `theme_lerp(a, b, t)` | interpolate two themes |

### Testing and debugging

| | |
|---|---|
| `null_device` + `create_context` | a renderer that draws nothing |
| `violation_count(ctx)`, `set_violation_handler` | contract checks |
| `draw_violation_overlay(u, rect)` | on-screen violations |
| `set_report_layout_overflow(ctx, true)` | report clamped layout slices |
| `--selftest`, `--screenshot` | offscreen runs |

## Rough edges to know about

Honesty about a young library saves you time. As of this writing:

* **Text input is single-line.** There is no multi-line editor, no password mode, no
  built-in placeholder (you drew one in chapter 4).
* **Text rendering is simple.** No complex-script shaping, no right-to-left, and
  characters the font lacks draw as nothing. The bundled DejaVu font covers Latin,
  Greek, Cyrillic and many symbols.
* **No images or icons from files yet.** Chapter 6's icons were drawn from lines
  and arcs; textures can be created through the renderer, but there is no loader.
* **High-DPI scaling is not handled by the library yet** (see `docs/seams.md` for
  the plan): check how your app looks on a display scaled to 150–200%.
* **No accessibility tree** (screen readers) yet.
* **Blur is approximate** (stepwise radius) and costs a batch break per call.
* **Per-corner rounding covers shapes you draw, `u.blur`, `button`, `text_field` and
  `comp::segmented`** (outer radii), but `card`, `panel`, `number_field` and the other
  `comp::` components still take one radius from the theme.
* **Translucent outlines** can show slightly thinner corners than straight edges,
  most visible on a bright, thick focus ring over a glass field.
* **One context per thread.** Draw from one thread; if you must use two, give each
  its own context.
* **Primary platform is Windows.** Linux is built and tested in CI; macOS is
  untested.

The up-to-date list is `docs/limitations.md` in the repository.

## Questions you will have

**Why are ids explicit instead of derived from labels?** A label is *content* (it
changes, it repeats); an id is *identity* (it must not). Deriving one from the other
means renaming a button resets its focus, and two "Delete" buttons collide. Explicit
ids cost one literal per widget and remove a whole class of surprises. For loops,
`u.scope(stable_key)` does the work.

**Where do I put widget state?** In your own structs. The library keeps only what
it must (focus, text-edit state, scroll offsets, animation progress), keyed by id,
and forgets keys you stop using.

**Can I use my own renderer or window system?** Yes: the renderer is a small
abstract class (`render_device`), documented in `docs/porting_a_backend.md`, and the
window/event layer is a handful of functions (`mouse_move`, `key_event`,
`text_input_event`, ...). The SDL3 bootstrap is the reference implementation, and
`null_device` is the smallest one.

**How do I draw something the library has no widget for?** Exactly as in chapter 6:
`interact` for input, the drawing primitives for output. Most of this app's visuals
are such "custom widgets" in under 40 lines.

**Is it fast enough?** The repository's benchmark draws 2,000 labeled buttons, 500
text rows and 50 panels in about 5 ms of CPU on a laptop, in two draw calls; a
screen like this one is a small fraction of that.

## Read more

| | |
|---|---|
| [`README.md`](../../README.md) | the long reference tutorial: 28 chapters, one per capability |
| [`docs/components.md`](../components.md) | the component catalogue and how to write your own |
| [`docs/api.md`](../api.md) | every public function, generated from the header |
| [`docs/model_view.md`](../model_view.md) | the state/view pattern used here |
| [`docs/porting_a_backend.md`](../porting_a_backend.md) | writing a renderer |
| [`examples/atomic/`](../../examples/atomic/) | one small runnable program per capability (`pui_ex_*`) |
| `pui_tour` | a guided app that walks through all of it |

Have fun making things.
