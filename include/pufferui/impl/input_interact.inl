// pufferui/impl/input_interact.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- input ----
namespace detail
{
inline bool window_is_current(window &w)
{
    return w.owner != nullptr && w.owner->current_window == &w;
}
// Where an event for `w` lands: the open frame's input (so it applies to this
// frame) or the window's queue (so it applies to the next one).
inline window_input &input_for(window &w)
{
    return window_is_current(w) ? static_cast<window_input &>(*w.owner) : w.in;
}
} // namespace detail

void mouse_move(window &w, f32 x, f32 y)
{
    context *c = w.owner;
    if (!c) return;
    c->mouse_global_x = w.client.x + x;
    c->mouse_global_y = w.client.y + y;
    if (detail::window_is_current(w))
    {
        c->mouse_x = x;
        c->mouse_y = y;
    }
}

void mouse_button(window &w, bool down)
{
    context *c = w.owner;
    if (!c) return;
    window_input &q = detail::input_for(w);
    if (down && !c->mouse_down) q.mouse_pressed = true;
    if (!down && c->mouse_down) q.mouse_released = true;
    c->mouse_down = down;
}

void mouse_wheel(window &w, f32 dx, f32 dy)
{
    if (!w.owner) return;
    window_input &q = detail::input_for(w);
    q.wheel_x += dx;
    q.wheel_y += dy;
}

void mouse_wheel(context *c, f32 dx, f32 dy)
{
    if (!c) return;
    window *w = c->current_window ? c->current_window : detail::ensure_primary(c);
    if (w) mouse_wheel(*w, dx, dy);
}

void mouse_button(window &w, pointer_button b, bool down)
{
    context *c = w.owner;
    if (!c) return;
    if (b == pointer_button::LEFT)
    {
        mouse_button(w, down);
        return;
    }
    if (b != pointer_button::RIGHT) return; // middle button is not wired yet
    if (down && !c->right_down) detail::input_for(w).right_pressed = true;
    c->right_down = down;
}

void key_event(window &w, key k, bool down)
{
    const i32 i = static_cast<i32>(k);
    if (i < 0 || i >= KEY_COUNT) return;
    window_input &q = detail::input_for(w);
    if (down && !q.key_down[i]) q.key_pressed[i] = true;
    if (!down && q.key_down[i]) q.key_released[i] = true;
    q.key_down[i] = down;
}

void text_input_event(window &w, const char *utf8)
{
    window_input &q = detail::input_for(w);
    q.ime_preedit_len = 0;
    q.ime_preedit[0] = '\0'; // committing text ends any composition
    if (!utf8) return;
    // Never silently truncated: an event that will not fit reports the
    // overflow once, then the part that fits is kept (graceful, but loud).
    usize in_len = 0;
    while (utf8[in_len] != '\0') ++in_len;
    const i32 cap = static_cast<i32>(sizeof(q.text_input)) - 1;
    if (static_cast<i32>(in_len) > cap - q.text_len)
    {
        PUFFERUI_CHECK(
            VIOL_INPUT_OVERFLOW, false,
            "text input did not fit the per-frame input buffer (IME commit or paste too long)");
    }
    for (const char *p = utf8; *p && q.text_len < cap; ++p) q.text_input[q.text_len++] = *p;
    q.text_input[q.text_len] = '\0';
}

void ime_event(window &w, const char *preedit, i32 cursor)
{
    window_input &q = detail::input_for(w);
    q.ime_preedit_len = 0;
    q.ime_cursor = cursor;
    const i32 cap = static_cast<i32>(sizeof(q.ime_preedit)) - 1;
    if (preedit)
        for (const char *p = preedit; *p && q.ime_preedit_len < cap; ++p)
            q.ime_preedit[q.ime_preedit_len++] = *p;
    q.ime_preedit[q.ime_preedit_len] = '\0';
}

void mods_event(window &w, bool shift, bool ctrl)
{
    window_input &q = detail::input_for(w);
    q.shift_down = shift;
    q.ctrl_down = ctrl;
}

void mouse_move(context *c, f32 x, f32 y)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_move(*w, x, y);
}

void mouse_button(context *c, bool down)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_button(*w, down);
}

void mouse_button(context *c, pointer_button b, bool down)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_button(*w, b, down);
}

void key_event(context *c, key k, bool down)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) key_event(*w, k, down);
}

void text_input_event(context *c, const char *utf8)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) text_input_event(*w, utf8);
}

void ime_event(context *c, const char *preedit, i32 cursor)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) ime_event(*w, preedit, cursor);
}

void mods_event(context *c, bool shift, bool ctrl)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) mods_event(*w, shift, ctrl);
}

void set_clipboard(context *c, clipboard *clip)
{
    c->clip = clip;
}

i32 violation_count(context *c)
{
    return c->violations;
}

const char *violation_last(context *c)
{
    return c ? c->vlast : "";
}

violation_code violation_last_code(context *c)
{
    return c ? c->vlast_code : VIOL_NONE;
}

void set_break_on_violation(context *c, bool enabled)
{
    if (c) c->break_on_violation = enabled;
}

const char *violation_code_name(violation_code code)
{
    static const char *const names[] = {
        "none",
        "invalid_slice",
        "invalid_rect",
        "invalid_area",
        "dup_region",
        "window_limit",
        "shared_device",
        "remove_while_open",
        "destroy_while_open",
        "frame_already_open",
        "window_not_registered",
        "end_without_begin",
        "clip_unbalanced",
        "scope_unbalanced",
        "popup_overflow",
        "combo_empty",
        "layout_clamped",
        "no_font",
        "input_overflow",
        "dup_widget_id",
        "frame_ids_overflow",
        "dup_motion_key",
    };
    return (code >= 0 && code < VIOL_COUNT) ? names[code] : "unknown";
}

// ---- animation ----
namespace detail
{

inline anim_store *ensure_anim(context *c)
{
    return &c->st->anim;
}

inline uiid anim_scoped_id(context *c, uiid key, bool global)
{
    return global ? id_child(0x676C6F62616C0000ull, key) : id_child(c->current_region, key);
}

inline f32 tween_value(context *c, anim_store::entry &e, f32 target, const tween &spec)
{
    if (!e.initialized || e.kind != 0 || e.target != target)
    {
        if (e.initialized && e.kind == 0 && e.duration > 0.0f)
        {
            // Re-evaluate the old tween at `now` before retargeting.
            const f32 told = clampf(static_cast<f32>((c->now - e.t0) / e.duration), 0.0f, 1.0f);
            e.current = e.start + (e.target - e.start) * ease(e.curve, told);
        }
        e.kind = 0;
        e.start = e.current;
        e.target = target;
        e.t0 = c->now;
        e.duration = spec.duration;
        e.curve = spec.curve;
        e.initialized = true;
    }
    if (c->reduced_motion)
    {
        e.current = target;
        e.settled = true;
        return e.current;
    }
    const f32 t = spec.duration > 0.0f
                      ? clampf(static_cast<f32>((c->now - e.t0) / spec.duration), 0.0f, 1.0f)
                      : 1.0f;
    e.current = e.start + (e.target - e.start) * ease(spec.curve, t);
    e.settled = (t >= 1.0f) || (e.start == e.target);
    return e.current;
}

// One spring step for one frame: the single integrator behind u.animate's
// springs and u.animate_rect. Semi-implicit Euler is only stable while
// step * omega stays small, and a frame can be arbitrarily long (a window
// drag, a debugger stop, a slow first frame): one big step would blow the
// spring up. So a frame is integrated in small steps; a hitch longer than one
// second is capped. Snaps to `target` and returns true once both the distance
// and the speed are under their epsilons (the value's own units).
inline bool spring_step(f32 &x, f32 &v, f32 target, f32 frame_dt, const spring &spec, f32 eps_x,
                        f32 eps_v)
{
    const f32 m = max2(spec.mass, 0.001f);
    const f32 omega = std::sqrt(max2(spec.stiffness, 0.0f) / m);
    const f32 dt = clampf(frame_dt, 0.0f, 1.0f);
    const f32 max_step = min2(1.0f / 120.0f, 0.5f / max2(omega, 1.0f));
    const i32 steps = static_cast<i32>(clampf(std::ceil(dt / max_step), 1.0f, 480.0f));
    const f32 step = dt / static_cast<f32>(steps);
    const f32 d = 2.0f * spec.damping_ratio * std::sqrt(spec.stiffness * m);
    for (i32 i = 0; i < steps; ++i)
    {
        const f32 accel = (spec.stiffness * (target - x) - d * v) / m;
        v += accel * step;
        x += v * step;
    }
    if (std::fabs(target - x) < eps_x && std::fabs(v) < eps_v)
    {
        x = target;
        v = 0.0f;
        return true;
    }
    return false;
}

inline f32 spring_value(context *c, anim_store::entry &e, f32 target, const spring &spec)
{
    if (!e.initialized || e.kind != 1)
    {
        e.kind = 1;
        e.velocity = 0.0f;
        e.initialized = true;
    }
    e.target = target; // retargets keep current + velocity
    if (c->reduced_motion)
    {
        e.current = target;
        e.velocity = 0.0f;
        e.settled = true;
        return e.current;
    }
    e.settled =
        spring_step(e.current, e.velocity, target, static_cast<f32>(c->dt), spec, 0.001f, 0.001f);
    return e.current;
}

inline f32 smooth_value(context *c, anim_store::entry &e, f32 target, f32 half_life)
{
    if (!e.initialized || e.kind != 2)
    {
        e.kind = 2;
        e.current = target; // start at the target: no jump
        e.initialized = true;
    }
    e.target = target;
    if (c->reduced_motion)
    {
        e.current = target;
        e.settled = true;
        return e.current;
    }
    const f32 dt = static_cast<f32>(c->dt);
    const f32 factor = (half_life > 0.0f) ? std::pow(0.5f, dt / half_life) : 0.0f;
    e.current = target + (e.current - target) * factor;
    e.settled = std::fabs(target - e.current) < 0.001f;
    if (e.settled) e.current = target;
    return e.current;
}

} // namespace detail

f32 ui::animate(uiid key, f32 target, const tween &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::tween_value(ctx, e, target, spec);
}

f32 ui::animate(uiid key, f32 target, const spring &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::spring_value(ctx, e, target, spec);
}

f32 ui::animate_global(uiid key, f32 target, const tween &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, true)];
    e.last_used = ctx->now;
    return detail::tween_value(ctx, e, target, spec);
}

f32 ui::animate_global(uiid key, f32 target, const spring &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, true)];
    e.last_used = ctx->now;
    return detail::spring_value(ctx, e, target, spec);
}

f32 ui::smooth(uiid key, f32 target, f32 half_life)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::smooth_value(ctx, e, target, half_life);
}

f32 ui::appear(uiid key, f32 duration, easing curve)
{
    return animate(key, 1.0f, tween{duration, curve});
}

color ui::animate_color(uiid key, color target, const tween &spec)
{
    color out;
    out.r = static_cast<u8>(
        clampf(animate(id_child(key, 1), static_cast<f32>(target.r), spec) + 0.5f, 0.0f, 255.0f));
    out.g = static_cast<u8>(
        clampf(animate(id_child(key, 2), static_cast<f32>(target.g), spec) + 0.5f, 0.0f, 255.0f));
    out.b = static_cast<u8>(
        clampf(animate(id_child(key, 3), static_cast<f32>(target.b), spec) + 0.5f, 0.0f, 255.0f));
    out.a = static_cast<u8>(
        clampf(animate(id_child(key, 4), static_cast<f32>(target.a), spec) + 0.5f, 0.0f, 255.0f));
    return out;
}

// ---- rect motion ----
namespace detail
{
// Settling thresholds for rects, in pixels and pixels/second: far below what a
// frame can show, but loose enough that a settled layout stops redrawing.
inline constexpr f32 RECT_EPS_X = 0.05f;
inline constexpr f32 RECT_EPS_V = 0.5f;

inline rect rect_from_axes(const f32 *x, vec2 origin)
{
    return rect::make(origin.x + x[0], origin.y + x[1], max2(0.0f, x[2]), max2(0.0f, x[3]));
}
} // namespace detail

rect ui::animate_rect(uiid key, rect target, const rect_motion &m)
{
    context *c = ctx;
    if (!target.is_valid())
    {
        PUFFERUI_CHECK(VIOL_INVALID_RECT, false, "animate_rect with an invalid target");
        return rect{};
    }
    auto &map = c->st->rects.map;
    const uiid k = detail::anim_scoped_id(c, key, false);
    const bool fresh = map.find(k) == map.end();
    rect_store::entry &e = map[k];
    const f32 t[4] = {target.x - m.origin.x, target.y - m.origin.y, target.w, target.h};
    if (fresh)
    {
        // first sight: in place (no fly-in from 0), or from the given rect
        const rect s = m.from.set ? m.from.value : target;
        const f32 s4[4] = {s.x - m.origin.x, s.y - m.origin.y, s.w, s.h};
        for (i32 i = 0; i < 4; ++i)
        {
            e.x[i] = s4[i];
            e.v[i] = 0.0f;
        }
    }
    e.target = rect::make(t[0], t[1], t[2], t[3]);
    e.origin = m.origin;
    e.last_used = c->now;
    if (!fresh && e.stepped_frame == c->frame)
    {
        // a second step this frame would double the speed: report, don't step
        PUFFERUI_CHECK(VIOL_DUP_MOTION_KEY, false,
                       "animate_rect called twice for the same key in one frame");
        return detail::rect_from_axes(e.x, m.origin);
    }
    e.stepped_frame = c->frame;
    if (m.snap || c->reduced_motion)
    {
        for (i32 i = 0; i < 4; ++i)
        {
            e.x[i] = t[i];
            e.v[i] = 0.0f;
        }
        e.settled = true;
    }
    else
    {
        const f32 dt = static_cast<f32>(c->dt);
        bool settled = true;
        for (i32 i = 0; i < 4; ++i)
            settled = detail::spring_step(e.x[i], e.v[i], t[i], dt, m.spec, detail::RECT_EPS_X,
                                          detail::RECT_EPS_V) &&
                      settled;
        e.settled = settled;
    }
    return detail::rect_from_axes(e.x, m.origin);
}

rect_motion_info ui::motion_info(uiid key) const
{
    rect_motion_info out;
    const auto &map = ctx->st->rects.map;
    const auto it = map.find(detail::anim_scoped_id(ctx, key, false));
    if (it == map.end()) return out;
    const rect_store::entry &e = it->second;
    out.known = true;
    out.settled = e.settled;
    out.current = detail::rect_from_axes(e.x, e.origin);
    out.target =
        rect::make(e.origin.x + e.target.x, e.origin.y + e.target.y, e.target.w, e.target.h);
    out.velocity = vec2{e.v[0] + e.v[2] * 0.5f, e.v[1] + e.v[3] * 0.5f}; // the center's
    return out;
}

bool ui::animations_active() const
{
    for (const auto &kv : ctx->st->anim.map)
        if (!kv.second.settled) return true;
    for (const auto &kv : ctx->st->rects.map)
        if (!kv.second.settled) return true;
    return false;
}

// ---- region ----
region::region(ui &u, uiid key, rect area, bool push_clip)
{
    u_ = &u;
    context *c = u.ctx;

    parent_ = c->current_region;
    id_ = id_child(parent_, key);
    area_ = area;
    content_ = area;

    rect parent_clip =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
    clip_ = rect::intersect(area, parent_clip);

    PUFFERUI_CHECK(VIOL_INVALID_AREA, area.is_valid(), "region area is invalid");

    c->current_region = id_;

    if (push_clip && c->clip_depth < MAX_CLIP_DEPTH)
    {
        c->clip_stack[c->clip_depth++] = clip_;
        pushed_clip_ = true;
    }

    // Duplicate region ids are an O(1) set membership test (per-frame
    // flat_map, cleared in begin_frame) — complete at any frame scale, no
    // cap to silently outgrow.
    {
        flat_map<u8> *ids = &c->st->frame_ids;
        if (ids->find(id_))
        {
            PUFFERUI_CHECK(VIOL_DUP_REGION_ID, false, "duplicate region id among siblings");
        }
        else
        {
            ids->at(id_) = 1;
        }
    }
}

region::~region()
{
    if (!u_ || !u_->ctx) return;
    context *c = u_->ctx;
    c->current_region = parent_;
    if (pushed_clip_ && c->clip_depth > 0) --c->clip_depth;
}

region ui::region(rect area, uiid key, bool push_clip)
{
    return pui::region(*this, key, area, push_clip);
}

id_scope::id_scope(ui &u, uiid key)
{
    u_ = &u;
    context *c = u.ctx;
    parent_ = c->current_region;
    c->current_region = id_child(parent_, key);
    // Two scopes with the same key in one frame is the same bug the region
    // check catches — same set, same report.
    {
        flat_map<u8> *ids = &c->st->frame_ids;
        if (ids->find(c->current_region))
            PUFFERUI_CHECK(VIOL_DUP_REGION_ID, false, "duplicate region id among siblings");
        else
            ids->at(c->current_region) = 1;
    }
}

id_scope::~id_scope()
{
    if (!u_ || !u_->ctx) return;
    u_->ctx->current_region = parent_;
}

rect region::corner(i32 n, f32 w, f32 h, f32 margin) const
{
    n = (n % 4 + 4) % 4; // tolerate any winding; -1 -> BL, 4 -> TL, ...
    w = clampf(w, 0.0f, max2(0.0f, content_.w - margin * 2.0f));
    h = clampf(h, 0.0f, max2(0.0f, content_.h - margin * 2.0f));
    rect r = content_;
    switch (n)
    {
    case 0: // top-left
        r = rect::make(content_.x + margin, content_.y + margin, w, h);
        break;
    case 1: // top-right
        r = rect::make(content_.right() - margin - w, content_.y + margin, w, h);
        break;
    case 2: // bottom-right
        r = rect::make(content_.right() - margin - w, content_.bottom() - margin - h, w, h);
        break;
    default: // bottom-left
        r = rect::make(content_.x + margin, content_.bottom() - margin - h, w, h);
        break;
    }
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "region corner produced an invalid rect");
    return r;
}

// ---- interaction ----
interaction ui::interact(uiid id, rect area, bool enabled, bool focusable)
{
    context *c = ctx;
    interaction in;
    in.disabled = !enabled;

    if (!enabled) return in;

    // A popup opened last frame captures all input. Base UI is blocked outright;
    // only popup content inside the top popup's rect may interact. "Click
    // outside" is recorded only for a click truly outside the popup.
    if (c->prev_popup_depth > 0)
    {
        const popup_entry &top = c->prev_popups[c->prev_popup_depth - 1];
        const bool inside = top.area.contains(c->mouse_x, c->mouse_y);
        if (!inside)
        {
            if (c->mouse_pressed)
            {
                c->popup_click_outside = true;
                c->popup_click_id = top.id;
            }
            in.blocked = true;
            return in;
        }
        if (!c->in_popup)
        {
            in.blocked = true;
            return in; // base UI under the popup: blocked, no close
        }
    }

    // A panel opened last frame blocks base UI under it. Popup content above it
    // was already allowed by the popup check, so only base UI reaches here.
    if (c->prev_panel_depth > 0)
    {
        const panel_entry *under = nullptr;
        for (i32 i = c->prev_panel_depth - 1; i >= 0; --i)
        {
            if (c->prev_panels[i].area.contains(c->mouse_x, c->mouse_y))
            {
                under = &c->prev_panels[i];
                break;
            }
        }
        if (under)
        {
            const bool inside =
                c->panel_stack_depth > 0 && c->panel_stack[c->panel_stack_depth - 1] == under->id;
            if (!inside)
            {
                in.blocked = true;
                return in; // blocked by the floating panel
            }
        }
    }

    rect clip =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
    rect visible = rect::intersect(area, clip);
    bool over = visible.w > 0.0f && visible.h > 0.0f && visible.contains(c->mouse_x, c->mouse_y);
    in.hovered = over;

    if (over && c->active == 0) c->hot = id;

    if (over && c->mouse_pressed)
    {
        // Topmost wins: the last submitted enabled rect containing the press
        // position owns it. A press claimed earlier this frame by an
        // overlapping rect is superseded here (that call already reported
        // `activated`; the release lands its click on the topmost, so the
        // lower rect never acts).
        const rect claim_hit = rect::intersect(c->press_claim_rect, area);
        const bool supersedes = (c->active != 0 && c->active == c->press_claim &&
                                 claim_hit.w > 0.0f && claim_hit.h > 0.0f);
        if (c->active == 0 || supersedes)
        {
            c->active = id;
            in.activated = true;
            c->press_claim = id;
            c->press_claim_rect = area;
        }
    }

    // Right-press edge over the widget (context menus open on right-press).
    if (over && c->right_pressed) in.right_clicked = true;

    if (c->active == id)
    {
        in.captured = true;
        if (c->mouse_down) in.held = true;
        if (c->mouse_released)
        {
            if (over) in.clicked = true;
            c->active = 0;
        }
    }

    if (in.clicked)
    {
        if (c->last_click_id == id && (c->now - c->last_click_time) < 0.35)
            in.double_clicked = true;
        c->last_click_id = id;
        c->last_click_time = c->now;
    }

    in.pressed = (c->active == id) && c->mouse_down;
    in.focused = (c->focus == id);
    // Every enabled interact joins the keyboard focus ring, in submission
    // order — the same order widgets paint. Disabled widgets yield early and
    // never take focus; blocked widgets return before this point.
    if (enabled && focusable)
    {
        if (!c->focus_store) c->focus_store = new focus_list();
        focus_list *fs = c->focus_store;
        if (fs->current.empty() || fs->current.back() != id) fs->current.push_back(id);
#if !defined(NDEBUG)
        // Debug duplicate-widget detection: the same id interacted twice in
        // one frame silently shares press/focus/animation state (only region
        // ids are dup-checked). Identical rects are tolerated (a widget
        // re-submitting itself in place); different rects are the bug.
        {
            flat_map<rect> *seen = &c->st->widget_ids;
            if (rect *first = seen->find(id))
            {
                if (!(first->x == area.x && first->y == area.y))
                    PUFFERUI_CHECK(VIOL_DUP_WIDGET_ID, false,
                                   "duplicate widget id among siblings (the same id interacted at "
                                   "a different rect)");
            }
            else
            {
                seen->at(id) = area;
            }
        }
#endif
    }
    return in;
}

// ---- cursors ----
namespace detail
{
// Non-fatal truncation note (see set_report_layout_overflow). Called only when
// the requested slice does not fit the remaining space.
inline void report_slice_clamp(context *c, const char *what, f32 requested, f32 granted)
{
    if (!c || !c->report_layout_overflow || requested <= granted + 0.001f) return;
    c->overflow_count += 1;
    char message[160];
    std::snprintf(message, sizeof(message),
                  "%s slice clamped: requested %.0f px > remaining %.0f px", what,
                  static_cast<double>(requested), static_cast<double>(granted));
    report_violation(VIOL_LAYOUT_CLAMPED, "clamped", message, "", 0, false);
}
} // namespace detail

rect column::next(f32 height)
{
    f32 h = height > 0.0f ? height : 28.0f;
    const f32 avail = max2(0.0f, bounds_.h);
    detail::report_slice_clamp("column", h, avail);
    if (h > avail) h = avail; // clamp: a column never overflows
    rect r{bounds_.x, bounds_.y, bounds_.w, h};
    bounds_.y += h + gap_;
    bounds_.h = max2(0.0f, bounds_.h - (h + gap_));
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "column slice is invalid");
    return r;
}

rect row::next(f32 width)
{
    f32 w = width > 0.0f ? width : 80.0f;
    const f32 avail = max2(0.0f, bounds_.w);
    detail::report_slice_clamp("row", w, avail);
    if (w > avail) w = avail; // clamp: a row never overflows
    rect r{bounds_.x, bounds_.y, w, bounds_.h};
    bounds_.x += w + gap_;
    bounds_.w = max2(0.0f, bounds_.w - (w + gap_));
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "row slice is invalid");
    return r;
}

void column::space(f32 amount)
{
    bounds_.y += amount;
    bounds_.h = max2(0.0f, bounds_.h - amount);
}
void row::space(f32 amount)
{
    bounds_.x += amount;
    bounds_.w = max2(0.0f, bounds_.w - amount);
}
