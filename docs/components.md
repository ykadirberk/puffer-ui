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
   and `style` (a per-instance override) live there. No positional
   parameter piles.
3. **The result is a struct**, not a bool out-param: `{changed, selected,
   in}` with `explicit operator bool` for the primary event.
4. **State is the app's**: value references are written back directly;
   transient state (animation, hover) is keyed by uiid. **No file-scope
   statics.**
5. **Styles resolve theme → override.** Colors/sizes come from the theme
   slot (`theme::switch_ctrl`, `theme::radio`, `theme::segmented`, …);
   literals exist only as the style struct's defaults.
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
| `comp::segmented(u, area, span<const char *const> labels, i32 &selected, segmented_props{.id, .enabled, .style})` | Returns `{changed, selected, in}` |
| Theme slot | `theme::segmented` (`bg`, `selected`, `text`, `text_selected`, `radius`, `pad`, `anim`) |
| Keys | Every segment joins the Tab ring; Enter/Space selects the focused segment |
| Notes | The selected fill animates (`animate_color`), honors `reduced_motion` via the animation scopes |

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
| Notes | The scrim captures clicks but never joins the Tab ring (`interact(..., focusable = false)`); a scrim click closes when `close_on_scrim_click` |

## Roadmap

The next batches (r92–r93): tabs, accordion, drawer → toast host, table,
command palette — promoted from the example-level widgets in
`examples/atomic/patterns.cpp` into this library.
