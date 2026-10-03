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
inline void ring_hband(ui &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca, color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, ca},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, cb},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

inline void ring_vband(ui &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca, color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, cb},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, ca},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

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
    if (c.a == 0 || thickness <= 0.0f) return;
    const f32 t = min2(thickness, min2(r.w, r.h) * 0.5f);
    if (t <= 0.0f) return;
    const corner_radii q = clamp_radii(r, radii_in);

    if (q.tl == 0.0f && q.tr == 0.0f && q.br == 0.0f && q.bl == 0.0f)
    {
        u.draw_rect({r.x, r.y, r.w, t}, c);
        u.draw_rect({r.x, r.bottom() - t, r.w, t}, c);
        u.draw_rect({r.x, r.y + t, t, r.h - 2.0f * t}, c);
        u.draw_rect({r.right() - t, r.y + t, t, r.h - 2.0f * t}, c);
        return;
    }

    // Straight bands with the same 1px feathers the corner arcs get from
    // draw_sector (transparent 1px outside the outer edge and, when the hole
    // is wide enough, 1px inside the inner edge). Without them the hard strip
    // edges met the soft arc edges in a crisp step toward the inside.
    const f32 widest = max2(max2(q.tl, q.tr), max2(q.br, q.bl));
    const bool feather_in = max2(0.0f, widest - t) > 1.0f; // must match draw_sector's ring rule
    color fade = c;
    fade.a = 0;

    // extents of the four bands
    const f32 top_x0 = r.x + q.tl, top_x1 = r.right() - q.tr;
    const f32 bot_x0 = r.x + q.bl, bot_x1 = r.right() - q.br;
    const f32 left_y0 = r.y + (q.tl > 0.0f ? q.tl : t),
              left_y1 = r.bottom() - (q.bl > 0.0f ? q.bl : t);
    const f32 right_y0 = r.y + (q.tr > 0.0f ? q.tr : t),
              right_y1 = r.bottom() - (q.br > 0.0f ? q.br : t);

    // top / bottom
    ring_hband(u, top_x0, top_x1, r.y - 1.0f, r.y, fade, c);
    ring_hband(u, top_x0, top_x1, r.y, r.y + t, c, c);
    if (feather_in) ring_hband(u, top_x0, top_x1, r.y + t, r.y + t + 1.0f, c, fade);
    ring_hband(u, bot_x0, bot_x1, r.bottom(), r.bottom() + 1.0f, c, fade);
    ring_hband(u, bot_x0, bot_x1, r.bottom() - t, r.bottom(), c, c);
    if (feather_in) ring_hband(u, bot_x0, bot_x1, r.bottom() - t - 1.0f, r.bottom() - t, fade, c);

    // left / right
    ring_vband(u, r.x - 1.0f, r.x, left_y0, left_y1, fade, c);
    ring_vband(u, r.x, r.x + t, left_y0, left_y1, c, c);
    if (feather_in) ring_vband(u, r.x + t, r.x + t + 1.0f, left_y0, left_y1, c, fade);
    ring_vband(u, r.right(), r.right() + 1.0f, right_y0, right_y1, c, fade);
    ring_vband(u, r.right() - t, r.right(), right_y0, right_y1, c, c);
    if (feather_in) ring_vband(u, r.right() - t - 1.0f, r.right() - t, right_y0, right_y1, fade, c);

    if (q.tl > 0.0f)
        u.draw_sector(vec2{r.x + q.tl, r.y + q.tl}, max2(0.0f, q.tl - t), q.tl, PI, PI * 1.5f, c);
    if (q.tr > 0.0f)
        u.draw_sector(vec2{r.right() - q.tr, r.y + q.tr}, max2(0.0f, q.tr - t), q.tr, PI * 1.5f,
                      PI * 2.0f, c);
    if (q.br > 0.0f)
        u.draw_sector(vec2{r.right() - q.br, r.bottom() - q.br}, max2(0.0f, q.br - t), q.br, 0.0f,
                      PI * 0.5f, c);
    if (q.bl > 0.0f)
        u.draw_sector(vec2{r.x + q.bl, r.bottom() - q.bl}, max2(0.0f, q.bl - t), q.bl, PI * 0.5f,
                      PI, c);
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
    return detail::key_activate(*this, in) || in.clicked; // Enter/Space clicks a focused button
}

bool ui::button(rect r, std::string_view label, uiid id, const button_opts &opts)
{
    return button(r, label, id, opts.role, opts.style);
}
