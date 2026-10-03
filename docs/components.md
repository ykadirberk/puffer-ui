# PufferUI components (`pui::comp`)

The built-in component library: reusable, themeable widgets built **only on
the public API**, following one convention so they stay composable and
predictable.

## The convention (house rule)

A component is a free function in `namespace pui::comp`:

```cpp
result component(ui &u, rect area, ..., const props &p = {});
```

Rules, in short (the full checklist below):

1. **Identity is explicit.** `props.id` is required; internal parts derive
   with `id_child` / `u.scope`. Never derive an id from a label.
2. **Props are one struct** with designated-initializer defaults; `enabled`
   and `style` (a `const <name>_style *`) live there. No positional
   parameter piles.
3. **The result is a struct**, not a bool out-param: `{changed, selected,
   in}` with `explicit operator bool` for the primary event.
4. **State is the app's**: value references are written back directly;
   transient state (animation, hover) is keyed by uiid. **No file-scope
   statics.**
5. **Styles resolve theme slot → `props.style`.** Colors/sizes come from the
   theme slot (`theme::switch_ctrl`, `theme::radio`, `theme::segmented`, …);
   literals exist only as the style struct's defaults. To restyle one
   instance, copy the theme's style, edit the fields, and pass its address
   (it only has to outlive the call); there are no override structs:

   ```cpp
   switch_style mine = u.th().switch_ctrl;
   mine.track_on = u.th().tokens.danger;
   comp::switch_toggle(u, r, "Armed", on, {.id = "arm"_id, .style = &mine});
   ```
6. **Keyboard + cursor**: every component joins the Tab ring via `interact`,
   supports Enter/Space (or arrow) activation, and sets the hovered cursor.
7. **Works inside scroll views, panels and popups** (clips, id scopes).

### Definition of done for a new component

- [ ] Follows the convention above (props/result/style structs, explicit id)
- [ ] Disabled, hover, active, focus and `reduced_motion` handled
- [ ] Cursor set; keyboard operable; focus ring drawn when focused
- [ ] Headless test: behavior + `violation_count == 0`
- [ ] Atomic example (`examples/atomic/<name>.cpp`) with `--selftest`
- [ ] Row in this catalogue; README snippet matches the example

## `comp::switch_toggle`

An animated on/off track + knob.

| | |
| --- | --- |
| `comp::switch_toggle(u, area, label, bool &value, switch_props{.id, .enabled, .style})` | Toggles `value`; returns `{changed, in}` |
| `comp::switch_size(u, label)` | Track + gap + label width, for `row.next(...)` |
| Theme slot | `theme::switch_ctrl` (`track_off`, `track_on`, `knob`, `width`, `height`, `knob_pad`, `anim`, `animate`) |
| Keys | Enter/Space toggles when focused |

## `comp::radio_group`

A vertical radio list, one row per label.

| | |
| --- | --- |
| `comp::radio_group(u, area, span<const char *const> labels, i32 &selected, radio_props{.id, .enabled, .row_h, .style})` | Returns `{changed, selected, in}` |
| Theme slot | `theme::radio` (`ring`, `fill`, `size`, `gap`, `ring_w`) |
| Keys | Every row joins the Tab ring; Enter/Space selects the focused row |
| Notes | Drawn with the ring path (translucent backgrounds stay real) |

## `comp::segmented`

A horizontal segmented control (Day / Week / Month style).

| | |
| --- | --- |
| `comp::segmented(u, area, span<const char *const> labels, i32 &selected, segmented_props{.id, .enabled, .radii, .style})` | Returns `{changed, selected, in}` |
| Theme slot | `theme::segmented` (`bg`, `selected`, `text`, `text_selected`, `hover`, `radius`, `pad`, `anim`) |
| Keys | Every segment joins the Tab ring; Enter/Space selects the focused segment |
| Notes | The selected fill animates (`animate_color`), honors `reduced_motion` via the animation scopes. Every segment eases a `hover` highlight in. `.radii` (`opt<corner_radii>`) gives the control's *outer* corner radii: the first segment's left and the last one's right corners follow them (inset by `pad`, so they stay concentric in a rounded card); corners between segments keep `radius - pad`. Unset = all corners `radius` |

## `comp::tab_bar`

A horizontal tab bar (the app draws the content below it).

| | |
| --- | --- |
| `comp::tab_bar(u, area, span<const char *const> labels, i32 &active, tabs_props{.id, .enabled, .style})` | Returns `{changed, active, in}` |
| Theme slot | `theme::tabs` (`text`, `text_active`, `underline`, `hover_bg`, `radius`, `underline_h`, `gap`, `anim`) |
| Keys | Every tab joins the Tab ring; Enter/Space activates |
| Notes | The active underline fades via `animate_color` |

## `comp::accordion_scope`

A collapsible section: the header toggles `open`, the content area animates
its height and clips.

| | |
| --- | --- |
| `comp::accordion_scope acc(u, area, title, bool &open, accordion_props{.id, .enabled, .header_h, .content_h, .style})` | RAII: draw into `acc.content()` while alive; `acc.toggled()` reports; `if (acc)` = open |
| Theme slot | `theme::accordion` (`header_bg`, `header_hover`, `text`, `chevron`, `radius`, `header_h`, `anim`) |
| Keys | The header joins the Tab ring; Enter/Space toggles |
| Notes | `content_h` is the app-declared natural content height (drives the animation); the clip balances on scope exit |

## `comp::drawer_scope`

A sliding side drawer over a host rect.

| | |
| --- | --- |
| `comp::drawer_scope dr(u, host, bool &open, drawer_props{.id, .enabled, .width, .edge, .scrim, .close_on_scrim_click, .style})` | RAII: draw into `dr.content()`; `dr.toggled()`; `if (dr)` = open |
| Theme slot | `theme::drawer` (`bg`, `border`, `scrim`, `radius`, `anim`) |
| Edges | `drawer_edge::LEFT` / `RIGHT` (slides from that edge) |
| Notes | The scrim captures clicks but never joins the Tab ring (`interact(..., focusable = false)`); a scrim click *outside the drawer panel* closes when `close_on_scrim_click` (clicking the drawer's own empty area does not) |

## Theme tokens

`theme::tokens` is the semantic layer — `surface`, `surface_alt`, `on_surface`,
`on_surface_dim`, `primary`, `primary_hover`, `danger`, `success`, `warning`,
`outline` — separated from per-component styles. Component styles default from
these, so setting tokens once rethemes everything that opted in.
`theme_lerp(a, b, t)` interpolates two themes (colors + radius/spacing/padding/
control height), so an app can ease between a dark and a light theme.

## `comp::toast_draw`

Transient notifications from an app-owned queue.

| | |
| --- | --- |
| `toast_host h; h.push("Saved", 1);` | Queue a toast (kind 0 info / 1 success / 2 danger); oldest is dropped at 8 |
| `comp::toast_draw(u, anchor, h, toast_props{.id, .enabled, .style})` | Stacks the live toasts under `anchor`'s top-right corner, slides/fades them, ages them out |
| Theme slot | `theme::toast` (`bg`, `info`, `success`, `danger`, `text`, `width`, `height`, `gap`, `radius`, `lifetime`, `fade`) |

## `comp::table`

A uniform-column table with a pinned header and a virtualized body.

| | |
| --- | --- |
| `comp::table(u, area, span<const char *const> headers, i32 rows, function_ref<void(ui&, rect, i32 row, i32 col)> cell_draw, table_props{.id, .enabled, .row_h, .header_h, .style})` | Returns `{clicked_row, clicked_col, focused_row}`; `if (res)` = a row was activated |
| Theme slot | `theme::table` (`header_bg`, `row_bg`, `row_alt`, `row_hover`, `header_text`, `text`, `border`, `row_h`, `header_h`) |
| Notes | The body is a `scroll_view` + `virtual_list`: only visible rows are submitted; clicking resolves the column from the pointer; Enter/Space activates the focused row |

## `comp::command_palette`

A modal filter-and-run overlay (Ctrl+K style).

| | |
| --- | --- |
| `comp::command_palette(u, screen, bool &open, palette_state{.query, .active}, span<const palette_command>, palette_props{.id, .style})` | Returns `{chosen, active, shown}`; `chosen` is the ORIGINAL command index |
| Theme slot | `theme::palette` (`scrim`, `bg`, `border`, `text`, `hint`, `selected`, `accent`, `width`, `item_h`, `radius`) |
| Keys | Up/Down move the highlight, Enter chooses, Escape closes; typing filters (case-insensitive substring) |
| Notes | The query field takes focus while open; the scrim captures clicks (a click outside the panel closes it) and never joins the Tab ring. Hover moves the highlight only while the pointer moves, so Up/Down win over a resting pointer |

## `comp::section`

A titled card: title row, optional caption row, divider, then the body.

| | |
| --- | --- |
| `comp::section(u, area, title, caption = {}, section_props{.title_font, .style})` | Returns the body `rect` to lay content into; non-interactive, so no `id` |
| Theme slot | `theme::section` (`title_size`, `title_h`, `caption_h`, `caption_gap`, `no_caption_gap`, `divider_gap`, `body_gap`) |
| Notes | Colors come from the theme (`text`, `text_dim`, `border`, `card`); the examples' `example_section` is a one-line wrapper |

## Adding a component

1. Props/result/style structs + one function in `pufferui.h` (declaration
   section, `namespace comp`), a theme slot, its `theme_lerp` lines.
2. Inside the library start the interaction with
   `detail::widget_activate(u, id, area, enabled)` (interact + hand cursor +
   one `activated` flag for click or Enter/Space); draw from the resolved style.
3. A `PUI_TEST` using `tf_env`, a row here, and an atomic example.
