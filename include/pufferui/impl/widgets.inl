// pufferui/impl/widgets.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- built-in widget ----
namespace detail
{
// A rounded-rect outline drawn as 4 edge strips + 4 corner annuli. Filling the
// whole bounds with the border color and then inset-filling the background (the
// classic two-rect trick) makes translucent backgrounds impossible: the border
// color would show through them instead of the backdrop.
// A straight band whose two long edges carry per-vertex alpha, matching
// draw_sector's 1px arc feathers: in a rounded_ring the straight and arced
// segments then shade identically instead of meeting in a crisp step.

// Each radius limited to half of the shorter side; under half a pixel is a
// square corner (so every rounded shape clamps the same way).
inline corner_radii clamp_radii(rect r, corner_radii in)
{
    const f32 max_r = min2(r.w, r.h) * 0.5f;
    auto one = [&](f32 v) { return v <= 0.5f ? 0.0f : (v > max_r ? max_r : v); };
    return {one(in.tl), one(in.tr), one(in.br), one(in.bl)};
}

// The radii of an inner shape inset by `by` pixels (a border drawn as two rects).
inline corner_radii inset_radii(const corner_radii &in, f32 by)
{
    return {max2(0.0f, in.tl - by), max2(0.0f, in.tr - by), max2(0.0f, in.br - by),
            max2(0.0f, in.bl - by)};
}

// An outline with a radius per corner (0 = square). Straight bands run between
// the corner arcs; a square corner has no arc: the horizontal band covers the
// corner square and the vertical band starts below it, so nothing is drawn twice
// (translucent outlines would show an overlap).
inline void rounded_ring(ui &u, rect r, color c, const corner_radii &radii_in, f32 thickness)
{
    u.ctx->painter->ring({r, c, radii_in, thickness});
}

inline void rounded_ring(ui &u, rect r, color c, f32 radius, f32 thickness)
{
    rounded_ring(u, r, c, corner_radii::all(radius), thickness);
}

// The keyboard-focus outline drawn on the widget that owns `c->focus`.
inline void focus_ring(ui &u, rect r, const corner_radii &radii)
{
    const theme &t = u.th();
    if (t.focus_border_thickness <= 0.0f) return;
    rounded_ring(u, r, t.focus_border, radii, t.focus_border_thickness);
}
inline void focus_ring(ui &u, rect r, f32 radius)
{
    focus_ring(u, r, corner_radii::all(radius));
}

// Enter or Space on the focused widget activates it; the keys are consumed.
inline bool key_activate(ui &u, const interaction &in)
{
    if (!in.focused) return false;
    if (!u.key_pressed(key::ENTER) && !u.key_pressed(key::SPACE)) return false;
    u.consume_key(key::ENTER);
    u.consume_key(key::SPACE);
    return true;
}

// The interaction skeleton every clickable component starts with: interact,
// hand cursor while hovered, and one `activated` flag for click or keyboard.
struct activation
{
    interaction in{};
    bool activated = false; // clicked, or Enter/Space on the focused widget
    bool keyboard = false;  // ... by keyboard
};
inline activation widget_activate(ui &u, uiid id, rect area, bool enabled = true)
{
    activation a;
    a.in = u.interact(id, area, enabled);
    if (!enabled) return a;
    if (a.in.hovered) u.set_cursor(u.th().button_cursor);
    a.keyboard = key_activate(u, a.in);
    a.activated = a.in.clicked || a.keyboard;
    return a;
}
} // namespace detail

bool ui::button(rect r, std::string_view label, uiid id, uiid role_id, button_override ov)
{
    interaction in = interact(id, r);
    if (in.hovered) ctx->want_cursor = ctx->active_theme.button_cursor;
    const button_style s = resolve_button_style(role_id, ov);
    color bg = in.held ? s.active_bg : in.hovered ? s.hover_bg : s.bg;
    if (s.transition.duration > 0.0f)
        bg = animate_color(id_child(id, "bg"_id), bg,
                           tween{s.transition.duration, s.transition.curve});
    // Opaque backgrounds keep the classic two-rect outline (a single crisp AA
    // edge); translucent or empty backgrounds need the ring path, otherwise the
    // border color shows through them (outline buttons, frosted glass). A fully
    // transparent border is treated as "no border" so the fill stays full-size
    // instead of being inset by the invisible ring.
    const corner_radii rad = s.radii.set ? s.radii.value : corner_radii::all(s.radius);
    if (s.border_thickness > 0.0f && s.border.a > 0 && bg.a >= 250)
    {
        draw_rounded_rect(r, s.border, rad);
        draw_rounded_rect(r.pad(s.border_thickness), bg,
                          detail::inset_radii(rad, s.border_thickness));
    }
    else
    {
        if (bg.a > 0) draw_rounded_rect(r, bg, rad);
        detail::rounded_ring(*this, r, s.border, rad, s.border_thickness);
    }
    text(r, label, s.text, ALIGN_CENTER);
    if (in.focused) detail::focus_ring(*this, r, rad);
    a11y(id, r, {.role = a11y_role::BUTTON, .name = label});
    return detail::key_activate(*this, in) || in.clicked; // Enter/Space clicks a focused button
}

bool ui::button(rect r, std::string_view label, uiid id, const button_opts &opts)
{
    auto disabled = this->disabled(!opts.enabled);
    return button(r, label, id, opts.role, opts.style);
}
