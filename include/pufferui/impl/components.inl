// pufferui/impl/components.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- component library (pui::comp) ------------------------------------------

namespace detail
{
// defined later with the clip helpers (RAII scopes in the components below)
bool clip_push(context *c, rect r);
void clip_pop(context *c);
} // namespace detail

namespace comp
{

switch_result switch_toggle(ui &u, rect area, std::string_view label, bool &value,
                            const switch_props &p)
{
    switch_result out{};
    switch_style s = p.style ? *p.style : u.th().switch_ctrl;

    const detail::activation act = detail::widget_activate(u, p.id, area, p.enabled);
    out.in = act.in;
    if (act.activated)
    {
        value = !value;
        out.changed = true;
    }

    const rect track{area.x, area.y + (area.h - s.height) * 0.5f, s.width, s.height};
    const tween tw{s.anim.duration, s.anim.curve, true};
    const color bg = s.animate ? u.animate_color(id_child(p.id, "track"_id),
                                                 value ? s.track_on : s.track_off, tw)
                               : (value ? s.track_on : s.track_off);
    const f32 t = s.animate ? u.animate(id_child(p.id, "knob"_id), value ? 1.0f : 0.0f, tw)
                            : (value ? 1.0f : 0.0f);
    u.draw_rounded_rect(track, bg, s.height * 0.5f);
    const f32 knob_d = s.height - s.knob_pad * 2.0f;
    const f32 travel = max2(0.0f, s.width - knob_d - s.knob_pad * 2.0f);
    const rect knob{track.x + s.knob_pad + travel * t, track.y + s.knob_pad, knob_d, knob_d};
    u.draw_rounded_rect(knob, s.knob, knob_d * 0.5f);
    if (out.in.focused) detail::focus_ring(u, track, s.height * 0.5f);
    if (!label.empty())
        u.text(rect::make(track.right() + u.spacing(), area.y,
                          max2(0.0f, area.right() - track.right() - u.spacing()), area.h),
               label, u.th().text, ALIGN_LEFT);
    u.a11y(p.id, area,
           {.role = a11y_role::SWITCH, .name = label, .state = value ? A11Y_CHECKED : 0u});
    return out;
}

vec2 switch_size(ui &u, std::string_view label)
{
    const switch_style &s = u.th().switch_ctrl;
    const f32 label_w = label.empty() ? 0.0f : u.text_size(label).x + u.spacing();
    return vec2{s.width + label_w, s.height};
}

radio_result radio_group(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                         const radio_props &p)
{
    radio_result out{};
    radio_style st = p.style ? *p.style : u.th().radio;

    const f32 row_h = p.row_h > 0.0f ? p.row_h : u.control_h();
    const i32 n = static_cast<i32>(labels.size());
    column col(area, 0.0f);
    for (i32 i = 0; i < n; ++i)
    {
        const rect row = col.next(row_h);
        if (row.h <= 0.0f) break;
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const detail::activation act = detail::widget_activate(u, item_id, row, p.enabled);
        const interaction &in = act.in;
        {
            if (act.activated && selected != i)
            {
                selected = i;
                out.changed = true;
            }
        }
        out.selected = selected;

        const f32 d = st.size;
        const rect dot{row.x, row.y + (row.h - d) * 0.5f, d, d};
        const f32 radius = d * 0.5f;
        const bool on = (selected == i);
        // A ring, not a fill-then-inset (translucent backgrounds stay real).
        detail::rounded_ring(u, dot, on ? st.fill : st.ring, radius, st.ring_w);
        if (on) u.draw_rounded_rect(dot.pad(st.ring_w + 2.0f), st.fill, radius - st.ring_w - 2.0f);
        if (in.focused) detail::focus_ring(u, dot.pad(-2.0f), radius + 2.0f);
        u.text(rect::make(dot.right() + st.gap, row.y, max2(0.0f, row.w - d - st.gap), row.h),
               labels[i] ? labels[i] : "", u.th().text, ALIGN_LEFT);
        u.a11y(item_id, row,
               {.role = a11y_role::RADIO,
                .name = labels[i] ? labels[i] : "",
                .state = on ? A11Y_CHECKED : 0u});
    }
    return out;
}

segmented_result segmented(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                           const segmented_props &p)
{
    segmented_result out{};
    segmented_style st = p.style ? *p.style : u.th().segmented;

    const i32 n = static_cast<i32>(labels.size());
    if (n <= 0) return out;
    const corner_radii outer = p.radii.set ? p.radii.value : corner_radii::all(st.radius);
    u.draw_rounded_rect(area, st.bg, outer);
    const rect inner = area.pad(st.pad);
    const f32 seg_w = inner.w / static_cast<f32>(n);
    const f32 inner_r = max2(0.0f, st.radius - st.pad); // corners between segments
    for (i32 i = 0; i < n; ++i)
    {
        const f32 x = inner.x + seg_w * static_cast<f32>(i);
        const f32 w = (i == n - 1) ? (inner.right() - x) : seg_w;
        const rect seg = rect::make(x, inner.y, w, inner.h);
        // the end segments follow the control's outer corners (concentric: inset by
        // the padding); the corners where segments meet keep the small radius
        const bool first = i == 0, last = i == n - 1;
        const corner_radii rr{first ? max2(0.0f, outer.tl - st.pad) : inner_r,
                              last ? max2(0.0f, outer.tr - st.pad) : inner_r,
                              last ? max2(0.0f, outer.br - st.pad) : inner_r,
                              first ? max2(0.0f, outer.bl - st.pad) : inner_r};
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const detail::activation act = detail::widget_activate(u, item_id, seg, p.enabled);
        const interaction &in = act.in;
        {
            if (act.activated && selected != i)
            {
                selected = i;
                out.changed = true;
            }
        }
        out.selected = selected;

        const bool on = (selected == i);
        const color fill = on ? u.animate_color(id_child(item_id, "bg"_id), st.selected,
                                                tween{st.anim.duration, st.anim.curve, true})
                              : color{0, 0, 0, 0};
        if (on) u.draw_rounded_rect(seg, fill, rr);
        // hover: a highlight that eases in over the segment, selected or not
        const f32 hover =
            u.smooth(id_child(item_id, "hover"_id), (in.hovered && p.enabled) ? 1.0f : 0.0f, 0.05f);
        if (hover > 0.01f)
        {
            color h = st.hover;
            h.a = static_cast<u8>(static_cast<f32>(h.a) * hover);
            u.draw_rounded_rect(seg, h, rr);
        }
        if (in.focused) detail::focus_ring(u, seg, rr);
        u.text(seg, labels[i] ? labels[i] : "", on ? st.text_selected : st.text, ALIGN_CENTER);
        u.a11y(item_id, seg,
               {.role = a11y_role::RADIO,
                .name = labels[i] ? labels[i] : "",
                .state = on ? A11Y_CHECKED : 0u});
    }
    return out;
}

tabs_result tab_bar(ui &u, rect area, std::span<const char *const> labels, i32 &active,
                    const tabs_props &p)
{
    tabs_result out{};
    tabs_style st = p.style ? *p.style : u.th().tabs;

    const i32 n = static_cast<i32>(labels.size());
    row tabs(area, st.gap);
    for (i32 i = 0; i < n; ++i)
    {
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const f32 w = u.text_size(labels[i] ? labels[i] : "").x + u.padding();
        const rect tab = tabs.next(w);
        if (tab.w <= 0.0f) break;
        const detail::activation act = detail::widget_activate(u, item_id, tab, p.enabled);
        const interaction &in = act.in;
        if (act.activated && active != i)
        {
            active = i;
            out.changed = true;
        }
        out.active = active;
        out.in = in;

        const bool on = (active == i);
        if (in.hovered && !on) u.draw_rounded_rect(tab, st.hover_bg, st.radius);
        u.text(tab, labels[i] ? labels[i] : "", on ? st.text_active : st.text, ALIGN_CENTER);
        const color ul =
            u.animate_color(id_child(item_id, "ul"_id), on ? st.underline : color{0, 0, 0, 0},
                            tween{st.anim.duration, st.anim.curve, true});
        u.draw_rect(rect::make(tab.x, tab.bottom() - st.underline_h, tab.w, st.underline_h), ul);
        if (in.focused) detail::focus_ring(u, tab, st.radius);
        u.a11y(item_id, tab,
               {.role = a11y_role::TAB,
                .name = labels[i] ? labels[i] : "",
                .state = on ? A11Y_SELECTED : 0u});
    }
    return out;
}

accordion_scope::accordion_scope(ui &u, rect area, std::string_view title, bool &open,
                                 const accordion_props &p)
{
    u_ = &u;
    id_ = p.id;
    open_ = open;
    context *c = u.ctx;
    accordion_style st = p.style ? *p.style : c->active_theme.accordion;
    const f32 header_h = p.header_h > 0.0f ? p.header_h : st.header_h;

    rect body = area;
    const rect header = body.cut_top(header_h);
    const detail::activation act = detail::widget_activate(u, id_, header, p.enabled);
    const interaction &in = act.in;
    if (act.activated)
    {
        open = !open;
        open_ = open;
        toggled_ = true;
    }
    u.draw_rounded_rect(header, in.hovered ? st.header_hover : st.header_bg, st.radius);
    u.text(rect::make(header.x + u.padding(), header.y, max2(0.0f, header.w - u.padding() * 2.0f),
                      header.h),
           title, st.text, ALIGN_LEFT);
    // chevron: down when open, right when closed (rotates with the animation)
    const f32 t = u.animate(id_child(p.id, "t"_id), open ? 1.0f : 0.0f,
                            tween{st.anim.duration, st.anim.curve, true});
    {
        const f32 cx = header.right() - u.padding();
        const f32 cy = header.center_y();
        const vec2 a{cx - 5.0f, cy - 2.0f + 3.0f * t};
        const vec2 b{cx, cy + 3.0f - 3.0f * t};
        const vec2 dd{cx + 5.0f, cy - 2.0f + 3.0f * t};
        const vec2 tri[3] = {a, b, dd};
        u.draw_polygon(std::span<const vec2>(tri, 3), st.chevron);
    }
    if (in.focused) detail::focus_ring(u, header, st.radius);

    // the content area animates its height and clips
    const f32 ch = p.content_h * t;
    content_ = rect::make(body.x, header.bottom(), body.w, ch);
    // Clip to the content inside the given area. (Intersecting with the header
    // instead gave an empty clip: nothing inside could be hovered or clicked.)
    if (ch > 0.0f && detail::clip_push(c, rect::intersect(content_, area))) pushed_clip_ = true;
}

accordion_scope::~accordion_scope()
{
    if (pushed_clip_ && u_ && u_->ctx) detail::clip_pop(u_->ctx);
}

drawer_scope::drawer_scope(ui &u, rect host, bool &open, const drawer_props &p)
{
    u_ = &u;
    id_ = p.id;
    open_ = open;
    context *c = u.ctx;
    drawer_style st = p.style ? *p.style : c->active_theme.drawer;

    const f32 t = u.animate(id_child(p.id, "t"_id), open ? 1.0f : 0.0f,
                            tween{st.anim.duration, st.anim.curve, true});
    const f32 w = p.width;
    const f32 slide = (p.edge == drawer_edge::LEFT) ? -w * (1.0f - t) : w * (1.0f - t);
    const f32 x = (p.edge == drawer_edge::LEFT) ? host.x + slide : host.right() - w + slide;
    const rect panel = rect::make(x, host.y, w, host.h);

    // the scrim captures clicks behind the drawer (never in the Tab ring); it
    // spans the whole host, so a click on the drawer's own empty area is not
    // a click "outside" and must not close it
    if (t > 0.0f && p.scrim)
    {
        const interaction scrim = u.interact(id_child(p.id, "scrim"_id), host, true, false);
        u.draw_rect(host,
                    color{st.scrim.r, st.scrim.g, st.scrim.b, static_cast<u8>(st.scrim.a * t)});
        if (scrim.clicked && p.close_on_scrim_click && open &&
            !panel.contains(c->mouse_x, c->mouse_y))
        {
            open = false;
            open_ = false;
            toggled_ = true;
        }
    }
    u.draw_rounded_rect(panel, st.bg, st.radius);
    if (st.border.a > 0) detail::rounded_ring(u, panel, st.border, st.radius, 1.0f);
    content_ = panel.pad(c->active_theme.padding);
    if (t > 0.0f && detail::clip_push(c, rect::intersect(content_, host))) pushed_clip_ = true;
}

drawer_scope::~drawer_scope()
{
    if (pushed_clip_ && u_ && u_->ctx) detail::clip_pop(u_->ctx);
}

// ---- toast host -------------------------------------------------------------

void toast_host::push(const char *text, u32 kind)
{
    // Reuse a free slot; if full, drop the OLDEST (smallest serial).
    toast_item *slot = nullptr;
    for (toast_item &it : items)
        if (!it.used)
        {
            slot = &it;
            break;
        }
    if (!slot)
    {
        slot = &items[0];
        for (toast_item &it : items)
            if (it.serial < slot->serial) slot = &it;
    }
    std::snprintf(slot->text, sizeof(slot->text), "%s", text ? text : "");
    slot->kind = kind;
    slot->serial = next_serial++;
    slot->born = 0.0;
    slot->stamped = false;
    slot->used = true;
}

i32 toast_host::alive() const
{
    i32 n = 0;
    for (const toast_item &it : items)
        if (it.used) ++n;
    return n;
}

void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p)
{
    toast_style st = p.style ? *p.style : u.th().toast;

    const f64 now = u.ctx->now;
    f32 y = anchor.y;
    // newest first: walk descending serials, each item once
    u64 after = ~static_cast<u64>(0);
    for (i32 n = 0; n < toast_host::MAX_TOASTS; ++n)
    {
        toast_item *newest = nullptr;
        for (toast_item &it : host.items)
            if (it.used && it.serial < after && (newest == nullptr || it.serial > newest->serial))
                newest = &it;
        if (!newest) break;
        after = newest->serial;
        if (!newest->stamped)
        {
            newest->born = now;
            newest->stamped = true;
        }
        const f64 age = now - newest->born;
        if (age > static_cast<f64>(st.lifetime))
        {
            newest->used = false;
            continue;
        }
        const f32 t_in = clampf(static_cast<f32>(age) / st.anim.duration, 0.0f, 1.0f);
        const f32 remaining = static_cast<f32>(static_cast<f64>(st.lifetime) - age);
        const f32 t_out = clampf(remaining / max2(0.01f, st.fade), 0.0f, 1.0f);
        const f32 vis = min2(t_in, t_out);

        const rect box =
            rect::make(anchor.right() - st.width + (1.0f - t_in) * 24.0f, y, st.width, st.height);
        const f32 a = vis;
        u.draw_rounded_rect(box, color{st.bg.r, st.bg.g, st.bg.b, static_cast<u8>(st.bg.a * a)},
                            st.radius);
        const color stripe = newest->kind == 1   ? st.success
                             : newest->kind == 2 ? st.danger
                                                 : st.info;
        u.draw_rounded_rect(rect::make(box.x, box.y, 3.0f, box.h),
                            color{stripe.r, stripe.g, stripe.b, static_cast<u8>(stripe.a * a)},
                            st.radius);
        u.text(
            rect::make(box.x + u.padding(), box.y, max2(0.0f, box.w - u.padding() * 2.0f), box.h),
            newest->text, color{st.text.r, st.text.g, st.text.b, static_cast<u8>(st.text.a * a)},
            ALIGN_LEFT);
        y += st.height + st.gap;
    }
}

// ---- table ------------------------------------------------------------------

table_result table(ui &u, rect area, std::span<const char *const> headers, i32 row_count,
                   function_ref<void(ui &, rect, i32, i32)> cell_draw, const table_props &p)
{
    table_result out{};
    auto disabled = u.disabled(!p.enabled);
    context *c = u.ctx;
    table_style st = p.style ? *p.style : u.th().table;

    const f32 row_h = p.row_h > 0.0f ? p.row_h : st.row_h;
    const f32 header_h = p.header_h > 0.0f ? p.header_h : st.header_h;
    const i32 cols = static_cast<i32>(headers.size());
    const auto col_of = [&](i32 col) -> table_column *
    { return col < static_cast<i32>(p.columns.size()) ? &p.columns[col] : nullptr; };

    // The shown columns in display order.
    const bool ordered = static_cast<i32>(p.order.size()) == cols;
    detail::table_scratch_scope scratch(*c->st);
    auto &shown = scratch.value.shown;
    shown.clear();
    for (i32 k = 0; k < cols; ++k)
    {
        const i32 col = ordered ? p.order[k] : k;
        if (col < 0 || col >= cols) continue;
        if (const table_column *tc = col_of(col); tc && tc->hidden) continue;
        shown.push_back(col);
    }

    // Widths: set ones as given, the rest share what is left (scrolling
    // tables keep every column at least its minimum).
    auto &widths = scratch.value.widths;
    widths.assign(static_cast<usize>(cols), 0.0f);
    f32 fixed = 0.0f;
    i32 flexible = 0;
    for (i32 col : shown)
    {
        const f32 w = col_of(col) ? col_of(col)->width : 0.0f;
        if (w > 0.0f)
        {
            fixed += w;
            widths[col] = w;
        }
        else
            ++flexible;
    }
    for (i32 col : shown)
        if (widths[col] == 0.0f)
        {
            const f32 share = flexible ? max2(0.0f, area.w - fixed) / flexible : 0.0f;
            const f32 floor_w =
                p.horizontal ? (col_of(col) ? col_of(col)->min_width : 40.0f) : 0.0f;
            widths[col] = max2(share, floor_w);
        }
    f32 total = 0.0f;
    for (i32 col : shown) total += widths[col];

    // Sideways scrolling: Shift+wheel (or a horizontal wheel) over the table,
    // or the bar under it.
    const f32 max_x = (p.horizontal && total > area.w) ? total - area.w : 0.0f;
    f32 &xoff = c->st->splits.offsets[id_child(p.id, "hscroll"_id)];
    if (max_x > 0.0f && area.contains(c->mouse_x, c->mouse_y))
    {
        f32 dx = c->wheel_x;
        if (c->shift_down) dx += c->wheel_y;
        if (dx != 0.0f)
        {
            xoff -= dx * 40.0f;
            c->wheel_x = 0.0f;
            if (c->shift_down) c->wheel_y = 0.0f; // not a vertical scroll too
        }
    }
    xoff = clampf(xoff, 0.0f, max_x);
    const bool sticky = p.sticky_first && max_x > 0.0f && !shown.empty();
    const f32 sticky_w = sticky ? widths[shown[0]] : 0.0f;
    auto &xs = scratch.value.xs;
    xs.resize(shown.size());
    {
        f32 run = 0.0f;
        for (usize k = 0; k < shown.size(); ++k)
        {
            xs[k] = (k == 0 && sticky) ? area.x : area.x + run - xoff;
            run += widths[shown[k]];
        }
    }
    // Where a column's cells may show: the sticky one its own place, the
    // others the area right of it.
    const auto visible_part = [&](usize k, rect cell)
    {
        if (k == 0 && sticky) return cell;
        return rect::intersect(
            cell, rect::make(area.x + sticky_w, cell.y, max2(0.0f, area.w - sticky_w), cell.h));
    };

    rect body = area;
    const rect head = body.cut_top(header_h);
    u.draw_rect(head, st.header_bg);
    ctx_stores::comp_state &drag = c->st->comps[id_child(p.id, "hdrag"_id)];
    for (usize k = 0; k < shown.size(); ++k)
    {
        const i32 col = shown[k];
        const rect cell = rect::make(xs[k], head.y, widths[col], head.h);
        const rect vis = visible_part(k, rect::intersect(cell, head));
        if (vis.w <= 0.0f) continue;
        const uiid hid = id_child(p.id, static_cast<uiid>(col) ^ "header"_id);
        const bool sortable = p.state && (!col_of(col) || col_of(col)->sortable);
        const auto act = detail::widget_activate(u, hid, vis.pad(3.0f, 0.0f), p.enabled);
        if (act.in.activated && ordered) // a press may become a column drag
        {
            drag.open = true;
            drag.flag = false;
            drag.a = col;
            drag.x = c->mouse_x;
        }
        if (act.activated && sortable && !drag.flag)
        {
            p.state->descending = p.state->sort_column == col && !p.state->descending;
            p.state->sort_column = col;
            out.sort_changed = true;
        }
        {
            region clip = u.region(vis, hid ^ "clip"_id);
            u.text_ellipsis(rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
                            headers[col] ? headers[col] : "", st.header_text, ALIGN_LEFT);
        }
        u.a11y(hid, vis, {.role = a11y_role::BUTTON, .name = headers[col] ? headers[col] : ""});
        if (col_of(col) && k + 1 < shown.size())
        {
            const uiid resize_id = id_child(p.id, static_cast<uiid>(col) ^ "resize"_id);
            const rect grip{cell.right() - 3.0f, cell.y, 6.0f, cell.h};
            const auto in = u.interact(resize_id, grip, p.enabled, false);
            if (in.hovered || in.held) u.set_cursor(CURSOR_HRESIZE);
            f32 &anchor = c->st->splits.offsets[resize_id];
            if (in.activated) anchor = c->mouse_x - widths[col];
            if (in.held)
                col_of(col)->width = max2(max2(1.0f, col_of(col)->min_width), c->mouse_x - anchor);
        }
    }
    if (st.border.a > 0)
        u.draw_rect(rect::make(head.x, head.bottom() - 1.0f, head.w, 1.0f), st.border);

    // Dragging a header: an insertion mark while it moves, the order changes
    // on release.
    if (drag.open)
    {
        usize target = shown.size();
        for (usize k = 0; k < shown.size(); ++k)
            if (c->mouse_x < xs[k] + widths[shown[k]] * 0.5f)
            {
                target = k;
                break;
            }
        if (c->mouse_down)
        {
            if (std::fabs(c->mouse_x - drag.x) > 6.0f) drag.flag = true;
            if (drag.flag)
            {
                const f32 mx = target < shown.size() ? xs[target]
                               : xs.empty()          ? area.x
                                                     : xs.back() + widths[shown.back()];
                u.draw_rect(rect::make(mx - 1.0f, head.y, 2.0f, head.h), u.th().accent);
            }
        }
        else
        {
            if (drag.flag && ordered)
            {
                const i32 moving = drag.a;
                const i32 before = target < shown.size() ? shown[target] : -1;
                if (moving != before)
                {
                    std::vector<i32> next;
                    for (i32 v : p.order)
                        if (v != moving) next.push_back(v);
                    auto at = before < 0 ? next.end() : std::find(next.begin(), next.end(), before);
                    next.insert(at, moving);
                    for (i32 k = 0; k < cols; ++k) p.order[k] = next[static_cast<usize>(k)];
                    out.columns_changed = true;
                }
            }
            drag.open = false;
            drag.flag = false;
        }
    }

    // Right-click on the header: show or hide columns (one stays shown).
    if (p.column_menu && !p.columns.empty())
    {
        std::vector<std::string> labels;
        std::vector<const char *> ptrs;
        for (i32 col = 0; col < cols; ++col)
        {
            const bool hidden = col_of(col) && col_of(col)->hidden;
            labels.push_back(std::string(hidden ? "    " : "\xE2\x9C\x93 ") +
                             (headers[col] ? headers[col] : ""));
        }
        for (const std::string &l : labels) ptrs.push_back(l.c_str());
        const i32 pick = u.context_menu(id_child(p.id, "columns"_id), head, ptrs.data(), cols);
        if (pick >= 0 && col_of(pick))
        {
            table_column &tc = *col_of(pick);
            if (tc.hidden || shown.size() > 1)
            {
                tc.hidden = !tc.hidden;
                out.columns_changed = true;
            }
        }
    }

    // The sideways bar.
    if (max_x > 0.0f)
    {
        const scroll_style &sb = u.th().scrollbar;
        const rect track = body.cut_bottom(sb.thickness + sb.margin * 2.0f).pad(sb.margin);
        const f32 thumb_w = max2(sb.min_thumb, track.w * (area.w / total));
        const f32 travel = max2(0.0f, track.w - thumb_w);
        const rect thumb = rect::make(track.x + travel * (xoff / max_x), track.y, thumb_w, track.h);
        const uiid tid = id_child(p.id, "hthumb"_id);
        const interaction in = u.interact(tid, thumb, p.enabled, false);
        if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);
        ctx_stores::comp_state &ts = c->st->comps[tid];
        if (in.activated) ts.x = c->mouse_x - thumb.x;
        if (in.held && travel > 0.0f)
            xoff = clampf((c->mouse_x - ts.x - track.x) / travel * max_x, 0.0f, max_x);
        u.draw_rounded_rect(track, sb.track, sb.radius);
        u.draw_rounded_rect(thumb,
                            in.held      ? sb.thumb_active
                            : in.hovered ? sb.thumb_hover
                                         : sb.thumb,
                            sb.radius);
    }

    scroll_view sv =
        u.scroll(body, id_child(p.id, "body"_id), scroll_options{0.0f, SCROLL_OVERLAY});
    sv.virtual_list(
        row_count, row_h,
        [&](ui &uu, i32 row, rect row_rect)
        {
            const uiid record = p.row_key ? p.row_key(row) : static_cast<uiid>(row) + 1;
            const uiid row_id = id_child(p.id, record);
            auto row_scope = uu.scope(row_id);
            const detail::activation act = detail::widget_activate(uu, row_id, row_rect, p.enabled);
            const interaction &in = act.in;
            if (act.activated)
            {
                out.clicked_row = row;
                out.clicked_key = record;
                out.extend_selection = uu.ctx->shift_down;
                out.toggle_selection = uu.ctx->ctrl_down;
                if (p.state)
                {
                    out.selection_changed = p.state->selected != record;
                    p.state->selected = record;
                }
                // the column comes from the click position (keyboard: the first shown)
                out.clicked_col = shown.empty() ? 0 : shown[0];
                if (in.clicked)
                    for (usize k = 0; k < shown.size(); ++k)
                    {
                        const rect cv = visible_part(
                            k, rect::make(xs[k], row_rect.y, widths[shown[k]], row_rect.h));
                        if (cv.w > 0.0f && cv.contains(uu.ctx->mouse_x, uu.ctx->mouse_y))
                        {
                            out.clicked_col = shown[k];
                            break;
                        }
                    }
            }
            if (in.focused) out.focused_row = row;

            const color bg = (in.hovered || (p.state && p.state->selected == record))
                                 ? st.row_hover
                                 : (row % 2 ? st.row_alt : st.row_bg);
            uu.draw_rect(row_rect, bg);
            for (usize k = 0; k < shown.size(); ++k)
            {
                const i32 col = shown[k];
                const rect cell = rect::make(xs[k], row_rect.y, widths[col], row_rect.h);
                const rect vis = visible_part(k, rect::intersect(cell, row_rect));
                if (vis.w <= 0.0f) continue;
                auto clip = uu.region(vis, static_cast<uiid>(col) + 1);
                if (cell_draw)
                    cell_draw(uu,
                              rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
                              row, col);
            }
        });
    sv.set_content_height(static_cast<f32>(row_count) * row_h);
    return out;
}

selectable_result selectable(ui &u, rect area, std::string_view label, const selectable_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    const auto act = detail::widget_activate(u, p.id, area, p.enabled);
    const button_style &s = p.style ? *p.style : u.th().button;
    u.draw_rounded_rect(area,
                        p.selected       ? s.active_bg
                        : act.in.hovered ? s.hover_bg
                                         : s.bg,
                        s.radius);
    u.text_ellipsis(area.pad(s.pad_x, 0), label, s.text);
    if (act.in.focused) detail::focus_ring(u, area, s.radius);
    u.a11y(p.id, area,
           {.role = a11y_role::LIST_ITEM, .name = label, .state = p.selected ? A11Y_SELECTED : 0u});
    return {act.activated, act.in};
}

tree_result tree_node(ui &u, rect area, std::string_view label, bool &open, const tree_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    const f32 indent = max2(0.0f, static_cast<f32>(p.depth)) * u.padding();
    (void)area.cut_left(indent);
    rect arrow = area.cut_left(u.control_h());
    tree_result out;
    if (!p.leaf)
    {
        auto toggle =
            selectable(u, arrow, open ? "-" : "+",
                       {.id = id_child(p.id, "toggle"_id), .enabled = p.enabled, .style = p.style});
        if (toggle)
        {
            open = !open;
            out.toggled = true;
        }
    }
    auto item =
        selectable(u, area, label,
                   {.id = p.id, .enabled = p.enabled, .selected = p.selected, .style = p.style});
    out.selected = item.activated;
    out.in = item.in;
    if (!p.leaf && item.in.focused)
    {
        const bool left = u.shortcut({key::LEFT}, {.focus = p.id});
        const bool right = u.shortcut({key::RIGHT}, {.focus = p.id});
        if ((left && open) || (right && !open))
        {
            open = right;
            out.toggled = true;
        }
    }
    return out;
}

edit_result drag_value(ui &u, rect area, std::string_view label, f64 &value,
                       const drag_value_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    const interaction in = u.interact(p.id, area, p.enabled);
    f32 &last_x = u.ctx->st->splits.offsets[p.id];
    if (in.activated) last_x = u.ctx->mouse_x;
    f64 next = value;
    bool editing = false;
    if (in.held)
    {
        editing = u.ctx->mouse_x != last_x;
        next += static_cast<f64>(u.ctx->mouse_x - last_x) * p.speed;
        last_x = u.ctx->mouse_x;
    }
    if (in.focused)
    {
        if (u.shortcut({key::LEFT}, {.focus = p.id}))
        {
            next -= p.speed;
            editing = true;
        }
        if (u.shortcut({key::RIGHT}, {.focus = p.id}))
        {
            next += p.speed;
            editing = true;
        }
    }
    if (next < p.min) next = p.min;
    if (next > p.max) next = p.max;
    const bool changed = editing && !in.disabled && std::isfinite(next) && next != value;
    if (changed) value = next;
    if (in.hovered || in.held) u.set_cursor(CURSOR_HRESIZE);
    const button_style &s = p.style ? *p.style : u.th().button;
    u.draw_rounded_rect(area, in.held ? s.active_bg : in.hovered ? s.hover_bg : s.bg, s.radius);
    rect body = area.pad(s.pad_x, 0);
    u.text(body, label, s.text);
    u.textf(body, s.text, ALIGN_RIGHT, "%.6g", value);
    if (in.focused) detail::focus_ring(u, area, s.radius);
    return {changed, in};
}

edit_result color_edit(ui &u, rect area, color &value, const color_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    auto scope = u.scope(p.id);
    const i32 count = p.alpha ? 4 : 3;
    const char *labels[] = {"R", "G", "B", "A"};
    u8 *channels[] = {&value.r, &value.g, &value.b, &value.a};
    row rows(area, u.spacing());
    const f32 width = max2(0.0f, (area.w - u.spacing() * static_cast<f32>(count - 1)) / count);
    edit_result out;
    for (i32 i = 0; i < count; ++i)
    {
        f32 v = *channels[i];
        if (u.slider_float(rows.next(width), labels[i], v, 0, 255, u.local(labels[i]), "%.0f"))
        {
            *channels[i] = static_cast<u8>(v + 0.5f);
            out.changed = true;
        }
    }
    return out;
}

namespace
{
// Vertex-colored quad: c0..c3 at the corners TL, TR, BR, BL.
void gradient_quad(ui &u, rect r, color c0, color c1, color c2, color c3)
{
    vertex v[4] = {
        {r.x, r.y, 0, 0, c0},
        {r.right(), r.y, 0, 0, c1},
        {r.right(), r.bottom(), 0, 0, c2},
        {r.x, r.bottom(), 0, 0, c3},
    };
    const i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

void checker(ui &u, rect r, f32 cell)
{
    u.draw_rect(r, color{200, 200, 200, 255});
    for (f32 y = r.y; y < r.bottom(); y += cell)
        for (f32 x = r.x + (static_cast<i32>((y - r.y) / cell) % 2 ? cell : 0.0f); x < r.right();
             x += cell * 2.0f)
            u.draw_rect(rect::make(x, y, min2(cell, r.right() - x), min2(cell, r.bottom() - y)),
                        color{150, 150, 150, 255});
}

// A vertical bar or a square the pointer drags (and the keys nudge).
interaction drag_surface(ui &u, uiid id, rect r, const char *name)
{
    const interaction in = u.interact(id, r);
    if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);
    if (in.focused) detail::focus_ring(u, r, 2.0f);
    u.a11y(id, r, {.role = a11y_role::SLIDER, .name = name});
    return in;
}
} // namespace

edit_result color_picker(ui &u, rect area, color &value, const color_picker_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    context *c = u.ctx;
    const theme &th = u.th();
    edit_result out;
    ctx_stores::comp_state &st = c->st->comps[p.id];

    // HSV of the value; hue (and saturation at black) remembered when lost.
    hsv_color h = to_hsv(value);
    if (st.x >= 0.0f && (h.s <= 0.0f || h.v <= 0.0f)) h.h = st.x;
    if (st.y >= 0.0f && h.v <= 0.0f) h.s = st.y;
    const auto apply = [&](hsv_color nh, u8 a)
    {
        const color nc = from_hsv(nh, a);
        st.x = nh.h;
        st.y = nh.s;
        h = nh;
        if (nc.r != value.r || nc.g != value.g || nc.b != value.b || nc.a != value.a)
        {
            value = nc;
            out.changed = true;
        }
    };

    const f32 gap = u.spacing();
    const f32 bar_w = max2(12.0f, u.control_h() * 0.6f);
    const f32 side =
        max2(0.0f, min2(area.h, area.w - (bar_w + gap) * (p.alpha ? 2.0f : 1.0f) - gap - 90.0f));
    rect a = rect::make(area.x, area.y, area.w, side); // a true square; bars match its height
    const rect sq = a.cut_left(side);
    (void)a.cut_left(gap);
    const rect hue = a.cut_left(bar_w);
    (void)a.cut_left(gap);
    rect alpha_r{};
    if (p.alpha)
    {
        alpha_r = a.cut_left(bar_w);
        (void)a.cut_left(gap);
    }
    rect info = a;

    // saturation (x) / value (y)
    {
        const uiid sid = id_child(p.id, "sv"_id);
        const interaction in = drag_surface(u, sid, sq, "Saturation and value");
        if (in.held && sq.w > 0.0f && sq.h > 0.0f)
            apply({h.h, clampf((c->mouse_x - sq.x) / sq.w, 0.0f, 1.0f),
                   1.0f - clampf((c->mouse_y - sq.y) / sq.h, 0.0f, 1.0f)},
                  value.a);
        if (in.focused)
        {
            hsv_color k = h;
            if (u.key_pressed(key::LEFT)) k.s -= 0.02f;
            if (u.key_pressed(key::RIGHT)) k.s += 0.02f;
            if (u.key_pressed(key::UP)) k.v += 0.02f;
            if (u.key_pressed(key::DOWN)) k.v -= 0.02f;
            k.s = clampf(k.s, 0.0f, 1.0f);
            k.v = clampf(k.v, 0.0f, 1.0f);
            if (k.s != h.s || k.v != h.v)
            {
                for (key kk : {key::LEFT, key::RIGHT, key::UP, key::DOWN}) u.consume_key(kk);
                apply(k, value.a);
            }
        }
        const color pure = from_hsv({h.h, 1.0f, 1.0f});
        gradient_quad(u, sq, color{255, 255, 255, 255}, pure, pure, color{255, 255, 255, 255});
        gradient_quad(u, sq, color{0, 0, 0, 0}, color{0, 0, 0, 0}, color{0, 0, 0, 255},
                      color{0, 0, 0, 255});
        const f32 mx = sq.x + h.s * sq.w, my = sq.y + (1.0f - h.v) * sq.h;
        detail::rounded_ring(u, rect::make(mx - 6.0f, my - 6.0f, 12.0f, 12.0f),
                             h.v > 0.5f ? color{0, 0, 0, 255} : color{255, 255, 255, 255}, 6.0f,
                             2.0f);
    }

    // hue (top 0 .. bottom 360)
    {
        const uiid hid = id_child(p.id, "hue"_id);
        const interaction in = drag_surface(u, hid, hue, "Hue");
        if (in.held && hue.h > 0.0f)
            apply({clampf((c->mouse_y - hue.y) / hue.h, 0.0f, 1.0f) * 359.9f, h.s, h.v}, value.a);
        if (in.focused && (u.key_pressed(key::UP) || u.key_pressed(key::DOWN)))
        {
            const f32 step = u.key_pressed(key::UP) ? -2.0f : 2.0f;
            u.consume_key(key::UP);
            u.consume_key(key::DOWN);
            apply({std::fmod(h.h + step + 360.0f, 360.0f), h.s, h.v}, value.a);
        }
        for (i32 i = 0; i < 6; ++i)
        {
            const rect seg = rect::make(hue.x, hue.y + hue.h * i / 6.0f, hue.w, hue.h / 6.0f);
            const color top = from_hsv({60.0f * i, 1.0f, 1.0f});
            const color bottom = from_hsv({60.0f * (i + 1), 1.0f, 1.0f});
            gradient_quad(u, seg, top, top, bottom, bottom);
        }
        const f32 y = hue.y + h.h / 360.0f * hue.h;
        u.draw_rect(rect::make(hue.x - 2.0f, y - 1.5f, hue.w + 4.0f, 3.0f), th.text);
    }

    // alpha (top opaque .. bottom transparent)
    if (p.alpha)
    {
        const uiid aid = id_child(p.id, "alpha"_id);
        const interaction in = drag_surface(u, aid, alpha_r, "Alpha");
        if (in.held && alpha_r.h > 0.0f)
            apply(h,
                  static_cast<u8>(
                      (1.0f - clampf((c->mouse_y - alpha_r.y) / alpha_r.h, 0.0f, 1.0f)) * 255.0f +
                      0.5f));
        if (in.focused && (u.key_pressed(key::UP) || u.key_pressed(key::DOWN)))
        {
            const i32 step = u.key_pressed(key::UP) ? 5 : -5;
            u.consume_key(key::UP);
            u.consume_key(key::DOWN);
            apply(h, static_cast<u8>(clampf(value.a + static_cast<f32>(step), 0.0f, 255.0f)));
        }
        checker(u, alpha_r, 6.0f);
        color top = value, bottom = value;
        top.a = 255;
        bottom.a = 0;
        gradient_quad(u, alpha_r, top, top, bottom, bottom);
        const f32 y = alpha_r.y + (1.0f - value.a / 255.0f) * alpha_r.h;
        u.draw_rect(rect::make(alpha_r.x - 2.0f, y - 1.5f, alpha_r.w + 4.0f, 3.0f), th.text);
    }

    // preview, hex, eyedropper
    {
        column col(info, gap);
        const rect swatch = col.next(u.control_h() * 1.5f);
        checker(u, swatch, 6.0f);
        u.draw_rect(swatch, value);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", value.r, value.g, value.b, value.a);
        u.text(col.next(u.line_height()), hex, th.text, ALIGN_LEFT);
        const uiid pick_id = id_child(p.id, "pick"_id);
        color sampled{};
        if (u.take_sampled_pixel(pick_id, sampled))
        {
            sampled.a = value.a;
            apply(to_hsv(sampled), value.a);
            if (to_hsv(sampled).s > 0.0f) st.x = to_hsv(sampled).h;
        }
        const bool can_pick =
            p.eyedropper && c->device && has_cap(c->device->caps(), backend_caps::READ_PIXELS);
        if (can_pick)
        {
            if (u.button(col.next(u.control_h()), st.flag ? "Click a pixel..." : "Eyedropper",
                         id_child(p.id, "eyedropper"_id)))
                st.flag = !st.flag;
            if (st.flag)
            {
                // While picking, a window-wide popup layer takes the next click
                // so nothing under the pointer reacts to it.
                popup_scope pick =
                    u.popup(id_child(p.id, "pickmode"_id), c->screen, POPUP_CLOSE_ON_ESCAPE);
                const interaction any = u.interact(id_child(p.id, "pickarea"_id), c->screen);
                if (any.hovered) u.set_cursor(CURSOR_HAND);
                if (pick.close_requested) st.flag = false;
                if (any.clicked)
                {
                    u.sample_pixel(pick_id, u.mouse());
                    st.flag = false;
                }
            }
        }
    }
    return out;
}

edit_result date_picker(ui &u, rect area, date &value, const date_picker_props &p)
{
    auto disabled = u.disabled(!p.enabled);
    context *c = u.ctx;
    const theme &th = u.th();
    edit_result out;
    ctx_stores::comp_state &st = c->st->comps[p.id];
    if (st.a == 0) // the month shown starts at the value's
    {
        st.a = value.year;
        st.b = value.month;
    }
    const auto set = [&](date d)
    {
        if (d.day < 1 || d.month < 1) return;
        if (!(d == value))
        {
            value = d;
            out.changed = true;
        }
        st.a = d.year;
        st.b = d.month;
    };

    // the field
    const detail::activation act = detail::widget_activate(u, p.id, area, p.enabled);
    out.in = act.in;
    if (act.activated) st.open = !st.open;
    u.draw_rounded_rect(area, act.in.hovered ? th.widget_hover : th.widget_bg, th.radius);
    char label[32];
    std::snprintf(label, sizeof(label), "%04d-%02d-%02d", value.year, value.month, value.day);
    u.text(area.pad(u.padding() * 0.5f, 0.0f), label, th.text, ALIGN_LEFT);
    u.text(area.pad(u.padding() * 0.5f, 0.0f), st.open ? "\xE2\x96\xB4" : "\xE2\x96\xBE",
           th.text_dim, ALIGN_RIGHT);
    if (act.in.focused) detail::focus_ring(u, area, th.radius);
    u.a11y(p.id, area,
           {.role = a11y_role::COMBO,
            .name = "Date",
            .value = label,
            .state = st.open ? A11Y_EXPANDED : 0u});
    if (!st.open || !p.enabled) return out;

    // the month grid
    const f32 cell = max2(u.control_h(), 26.0f);
    const f32 pad = u.spacing();
    const f32 w = cell * 7.0f + pad * 2.0f, h = cell * 8.0f + pad * 2.0f;
    const rect space = detail::popup_space(c);
    f32 y = area.bottom() + 4.0f;
    if (y + h > space.bottom() && area.y - h - 4.0f >= space.y) y = area.y - h - 4.0f;
    const f32 x = clampf(area.x, space.x, max2(space.x, space.right() - w));
    const rect box = rect::make(x, y, w, h);
    popup_scope pop = u.popup(id_child(p.id, "grid"_id), box,
                              POPUP_CLOSE_ON_CLICK_OUTSIDE | POPUP_CLOSE_ON_ESCAPE);
    if (pop.close_requested)
    {
        st.open = false;
        return out;
    }
    // keyboard: the selection moves, the view follows
    if (u.key_pressed(key::LEFT)) set(add_days(value, -1));
    if (u.key_pressed(key::RIGHT)) set(add_days(value, 1));
    if (u.key_pressed(key::UP)) set(add_days(value, -7));
    if (u.key_pressed(key::DOWN)) set(add_days(value, 7));
    if (u.key_pressed(key::PAGE_UP)) set(add_months(value, -1));
    if (u.key_pressed(key::PAGE_DOWN)) set(add_months(value, 1));
    for (key k : {key::LEFT, key::RIGHT, key::UP, key::DOWN, key::PAGE_UP, key::PAGE_DOWN})
        u.consume_key(k);
    if (u.key_pressed(key::ENTER))
    {
        u.consume_key(key::ENTER);
        st.open = false;
    }

    u.draw_rounded_rect(box, th.panel_bg, th.radius);
    detail::rounded_ring(u, box, th.border, th.radius, 1.0f);
    column col(box.pad(pad), 0.0f);
    rect head = col.next(cell);
    const date shown{st.a, st.b, 1};
    if (u.button(head.cut_left(cell), "\xE2\x80\xB9", id_child(p.id, "prev"_id)))
    {
        const date m = add_months(shown, -1);
        st.a = m.year;
        st.b = m.month;
    }
    if (u.button(head.cut_right(cell), "\xE2\x80\xBA", id_child(p.id, "next"_id)))
    {
        const date m = add_months(shown, 1);
        st.a = m.year;
        st.b = m.month;
    }
    static const char *const MONTHS[12] = {"January",   "February", "March",    "April",
                                           "May",       "June",     "July",     "August",
                                           "September", "October",  "November", "December"};
    char title[32];
    std::snprintf(title, sizeof(title), "%s %d", MONTHS[(st.b - 1 + 12) % 12], st.a);
    u.text(head, title, th.text, ALIGN_CENTER);
    static const char *const DAYS[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    const i32 first = ((p.first_weekday % 7) + 7) % 7;
    {
        row names(col.next(cell), 0.0f);
        for (i32 i = 0; i < 7; ++i)
            u.text(names.next(cell), DAYS[(first + i) % 7], th.text_dim, ALIGN_CENTER);
    }
    // the first cell: the week of the 1st, starting at `first_weekday`
    const date d1{st.a, st.b, 1};
    date day = add_days(d1, -(((weekday(d1) - first) % 7 + 7) % 7));
    for (i32 r = 0; r < 6; ++r)
    {
        row week(col.next(cell), 0.0f);
        for (i32 i = 0; i < 7; ++i, day = add_days(day, 1))
        {
            const rect dc = week.next(cell).pad(1.0f);
            const uiid did =
                id_child(p.id, static_cast<uiid>(day.year * 10000 + day.month * 100 + day.day));
            const detail::activation da = detail::widget_activate(u, did, dc);
            const bool in_month = day.month == st.b;
            const bool selected = day == value;
            if (selected)
                u.draw_rounded_rect(dc, th.accent, th.radius);
            else if (da.in.hovered)
                u.draw_rounded_rect(dc, th.widget_hover, th.radius);
            char num[4];
            std::snprintf(num, sizeof(num), "%d", day.day);
            u.text(dc, num, selected ? th.bg : in_month ? th.text : th.text_dim, ALIGN_CENTER);
            u.a11y(
                did, dc,
                {.role = a11y_role::BUTTON, .name = num, .state = selected ? A11Y_SELECTED : 0u});
            if (da.activated)
            {
                set(day);
                st.open = false;
            }
        }
    }
    return out;
}

rect form_field(ui &u, rect area, std::string_view label, const form_props &p)
{
    if (!p.message.empty())
    {
        rect message = area.cut_bottom(u.line_height());
        u.text_ellipsis(message, p.message, p.error ? u.th().tokens.danger : u.th().text_dim);
        (void)area.cut_bottom(u.spacing());
    }
    const f32 width = p.label_width > 0 ? p.label_width : u.text_width(label) + u.padding();
    u.text_ellipsis(area.cut_left(width), label, u.th().text);
    (void)area.cut_left(u.spacing());
    return area;
}

// ---- command palette --------------------------------------------------------

static inline bool palette_match(const std::string &query, const char *name)
{
    if (query.empty() || !name) return true;
    // case-insensitive substring
    const usize n = query.size();
    if (n == 0) return true;
    for (const char *p = name; *p; ++p)
    {
        usize i = 0;
        while (i < n && p[i])
        {
            const char a =
                query[i] >= 'A' && query[i] <= 'Z' ? static_cast<char>(query[i] + 32) : query[i];
            const char b = p[i] >= 'A' && p[i] <= 'Z' ? static_cast<char>(p[i] + 32) : p[i];
            if (a != b) break;
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

palette_result command_palette(ui &u, rect screen, bool &open, palette_state &st,
                               std::span<const palette_command> commands, const palette_props &p)
{
    palette_result out{};
    if (!open) return out;
    palette_style sty = p.style ? *p.style : u.th().palette;

    // the scrim (never in the Tab ring) + the panel
    const i32 shown_max = 8;
    const f32 panel_h = 52.0f + sty.item_h * static_cast<f32>(shown_max) + 8.0f;
    const rect panel = rect::make(screen.center_x() - sty.width * 0.5f, screen.y + screen.h * 0.18f,
                                  sty.width, min2(panel_h, screen.h * 0.7f));
    const interaction scrim = u.interact(id_child(p.id, "scrim"_id), screen, true, false);
    u.draw_rect(screen, sty.scrim);
    // the scrim spans the screen, panel included: only a click outside closes
    if (scrim.clicked && !panel.contains(u.ctx->mouse_x, u.ctx->mouse_y)) open = false;
    u.draw_rounded_rect(panel, sty.bg, sty.radius);
    u.draw_rounded_rect(rect::make(panel.x, panel.y, 3.0f, panel.h), sty.accent, sty.radius);
    if (sty.border.a > 0) detail::rounded_ring(u, panel, sty.border, sty.radius, 1.0f);

    rect body = panel.pad(u.padding() * 0.75f);
    const rect field = body.cut_top(u.control_h());
    body.cut_top(6.0f);

    const uiid field_id = id_child(p.id, "query"_id);
    // keep the query field focused while the palette is open
    if (u.ctx->focus != field_id) u.ctx->focus_request = field_id;
    if (u.text_field(field, st.query, field_id))
    {
        st.active = 0; // typing resets the highlight and the scroll
        st.first = 0;
    }

    // Escape closes, Up/Down move, Enter chooses
    if (u.key_pressed(key::ESCAPE))
    {
        u.ctx->key_pressed[static_cast<i32>(key::ESCAPE)] = false;
        open = false;
    }
    const i32 total = static_cast<i32>(commands.size());
    i32 shown = 0;
    for (i32 i = 0; i < total; ++i)
        if (palette_match(st.query, commands[static_cast<usize>(i)].name)) ++shown;
    out.shown = shown;
    bool kb_moved = false;
    if (shown > 0)
    {
        if (u.key_pressed(key::DOWN))
        {
            u.ctx->key_pressed[static_cast<i32>(key::DOWN)] = false;
            st.active = (st.active + 1) % shown;
            kb_moved = true;
        }
        if (u.key_pressed(key::UP))
        {
            u.ctx->key_pressed[static_cast<i32>(key::UP)] = false;
            st.active = (st.active + shown - 1) % shown;
            kb_moved = true;
        }
        if (st.active >= shown) st.active = shown - 1;
    }
    else
    {
        st.active = 0;
    }

    // The visible window of `shown_max` entries is persistent state. It moves
    // only to bring a KEYBOARD highlight into view (minimally) or by the wheel.
    // Never follow the hover: hovering highlights a row, and re-centering on
    // that row would slide another entry under a resting pointer, every frame.
    const i32 max_first = shown > shown_max ? shown - shown_max : 0;
    if (shown > shown_max && panel.contains(u.ctx->mouse_x, u.ctx->mouse_y) &&
        u.ctx->wheel_y != 0.0f)
    {
        const f32 rows = -u.ctx->wheel_y; // wheel down (negative) scrolls down
        st.first += static_cast<i32>(rows > 0.0f ? std::ceil(rows) : std::floor(rows));
        u.ctx->wheel_y = 0.0f; // consumed: nothing under the palette scrolls too
    }
    if (kb_moved)
    {
        if (st.active < st.first) st.first = st.active;
        if (st.active >= st.first + shown_max) st.first = st.active - shown_max + 1;
    }
    st.first = st.first < 0 ? 0 : (st.first > max_first ? max_first : st.first);
    const i32 first = st.first;

    // Hover moves the highlight only while the pointer moves: a pointer resting
    // over a row must not fight the Up/Down keys every frame.
    const bool pointer_moved = u.ctx->mouse_x != st.pointer_x || u.ctx->mouse_y != st.pointer_y;
    st.pointer_x = u.ctx->mouse_x;
    st.pointer_y = u.ctx->mouse_y;

    i32 rank = -1;
    i32 drawn = 0;
    for (i32 i = 0; i < total && drawn < shown_max; ++i)
    {
        const palette_command &cmd = commands[static_cast<usize>(i)];
        if (!palette_match(st.query, cmd.name)) continue;
        ++rank;
        if (rank < first) continue;
        const rect item = body.cut_top(sty.item_h);
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const interaction in = u.interact(item_id, item, true, false);
        if (in.hovered)
        {
            if (pointer_moved && !kb_moved) st.active = rank;
            u.set_cursor(u.th().button_cursor);
        }
        const bool hl = (rank == st.active);
        if (hl) u.draw_rounded_rect(item.pad(0.0f, 1.0f), sty.selected, sty.radius * 0.6f);
        u.text(item.pad(10.0f, 0.0f), cmd.name ? cmd.name : "", sty.text, ALIGN_LEFT);
        if (cmd.hint && cmd.hint[0]) u.text(item.pad(10.0f, 0.0f), cmd.hint, sty.hint, ALIGN_RIGHT);
        if (in.clicked)
        {
            out.chosen = i;
            open = false;
        }
        ++drawn;
    }
    if (u.key_pressed(key::ENTER) && shown > 0)
    {
        u.consume_key(key::ENTER);
        // the active entry's ORIGINAL index
        i32 rank2 = -1;
        for (i32 i = 0; i < total; ++i)
        {
            if (!palette_match(st.query, commands[static_cast<usize>(i)].name)) continue;
            ++rank2;
            if (rank2 == st.active)
            {
                out.chosen = i;
                open = false;
                break;
            }
        }
    }
    out.active = shown > 0 ? st.active : -1;
    return out;
}

rect section(ui &u, rect area, std::string_view title, std::string_view caption,
             const section_props &p)
{
    const section_style st = p.style ? *p.style : u.th().section;
    const theme &th = u.th();
    u.card(area);
    rect inner = area.pad(th.card.padding);
    const bool has_caption = !caption.empty();
    const f32 head_h =
        st.title_h + (has_caption ? st.caption_h + st.caption_gap : st.no_caption_gap);
    rect head = inner.cut_top(head_h);
    {
        text_scope ts = u.text_style(st.title_size, p.title_font);
        u.text(head.cut_top(st.title_h), title, th.text, ALIGN_LEFT);
    }
    if (has_caption) u.text(head.cut_top(st.caption_h), caption, th.text_dim, ALIGN_LEFT);
    const f32 line_y = head.bottom() + st.divider_gap;
    u.draw_line(inner.left(), line_y, inner.right(), line_y, th.border, 1.0f);
    (void)inner.cut_top(st.body_gap);
    return inner;
}

} // namespace comp

void ui::card(rect r, card_override ov)
{
    const card_style s = merge_style(ctx->active_theme.card, ov);
    if (s.border_thickness > 0.0f && s.border.a > 0 && s.bg.a >= 250)
    {
        draw_rounded_rect(r, s.border, s.radius);
        draw_rounded_rect(r.pad(s.border_thickness), s.bg,
                          max2(0.0f, s.radius - s.border_thickness));
    }
    else
    {
        if (s.bg.a > 0) draw_rounded_rect(r, s.bg, s.radius);
        detail::rounded_ring(*this, r, s.border, s.radius, s.border_thickness);
    }
}

namespace detail
{
inline void thick_line(ui &u, vec2 a, vec2 b, f32 thickness, color c)
{
    vec2 d{b.x - a.x, b.y - a.y};
    const f32 l = max2(std::sqrt(d.x * d.x + d.y * d.y), 0.001f);
    const vec2 n{-d.y / l * thickness * 0.5f, d.x / l * thickness * 0.5f};
    const vec2 pts[4] = {{a.x + n.x, a.y + n.y},
                         {b.x + n.x, b.y + n.y},
                         {b.x - n.x, b.y - n.y},
                         {a.x - n.x, a.y - n.y}};
    u.draw_polygon(std::span<const vec2>(pts, 4), c);
}
} // namespace detail

bool ui::checkbox(rect r, std::string_view label, bool &value, uiid id)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    interaction in = interact(id, r);
    if (in.hovered) c->want_cursor = t.button_cursor;

    const f32 box = min2(18.0f, r.h);
    const rect box_r{r.x, r.y + (r.h - box) * 0.5f, box, box};
    const color bg = value ? t.accent : (in.hovered ? t.widget_hover : t.widget_bg);
    draw_rounded_rect(box_r, bg, max2(0.0f, t.radius * 0.75f));
    if (value)
    {
        detail::thick_line(*this, {box_r.x + box * 0.22f, box_r.y + box * 0.52f},
                           {box_r.x + box * 0.44f, box_r.y + box * 0.74f}, 2.0f, t.bg);
        detail::thick_line(*this, {box_r.x + box * 0.44f, box_r.y + box * 0.74f},
                           {box_r.x + box * 0.80f, box_r.y + box * 0.26f}, 2.0f, t.bg);
    }
    if (!label.empty())
        text(rect::make(box_r.right() + 8.0f, r.y, max2(0.0f, r.w - box - 8.0f), r.h), label,
             t.text, ALIGN_LEFT);

    // Keyboard activation: Enter or Space toggles the focused checkbox.
    if (in.focused) detail::focus_ring(*this, box_r, max2(0.0f, t.radius * 0.75f));

    const bool toggled = detail::key_activate(*this, in) || in.clicked;
    if (toggled) value = !value;
    a11y(id, r, {.role = a11y_role::CHECKBOX, .name = label, .state = value ? A11Y_CHECKED : 0u});
    return toggled;
}
