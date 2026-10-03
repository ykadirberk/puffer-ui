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
    table_style st = p.style ? *p.style : u.th().table;

    const f32 row_h = p.row_h > 0.0f ? p.row_h : st.row_h;
    const f32 header_h = p.header_h > 0.0f ? p.header_h : st.header_h;
    const i32 cols = static_cast<i32>(headers.size());
    const f32 col_w = cols > 0 ? area.w / static_cast<f32>(cols) : area.w;

    rect body = area;
    const rect head = body.cut_top(header_h);
    u.draw_rect(head, st.header_bg);
    for (i32 c = 0; c < cols; ++c)
    {
        const rect cell = rect::make(head.x + col_w * static_cast<f32>(c), head.y, col_w, head.h);
        u.text(rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
               headers[c] ? headers[c] : "", st.header_text, ALIGN_LEFT);
    }
    if (st.border.a > 0)
        u.draw_rect(rect::make(head.x, head.bottom() - 1.0f, head.w, 1.0f), st.border);

    scroll_view sv =
        u.scroll(body, id_child(p.id, "body"_id), scroll_options{0.0f, SCROLL_OVERLAY});
    sv.virtual_list(
        row_count, row_h,
        [&](ui &uu, i32 row, rect row_rect)
        {
            const uiid row_id = id_child(p.id, static_cast<uiid>(row));
            const detail::activation act = detail::widget_activate(uu, row_id, row_rect, p.enabled);
            const interaction &in = act.in;
            if (act.activated)
            {
                out.clicked_row = row;
                // the column comes from the click position (keyboard: the first)
                out.clicked_col =
                    in.clicked
                        ? static_cast<i32>((uu.ctx->mouse_x - row_rect.x) / max2(1.0f, col_w))
                        : 0;
                out.clicked_col = out.clicked_col < 0       ? 0
                                  : out.clicked_col >= cols ? cols - 1
                                                            : out.clicked_col;
            }
            if (in.focused) out.focused_row = row;

            const color bg = in.hovered ? st.row_hover : (row % 2 ? st.row_alt : st.row_bg);
            uu.draw_rect(row_rect, bg);
            for (i32 c = 0; c < cols; ++c)
            {
                const rect cell = rect::make(row_rect.x + col_w * static_cast<f32>(c), row_rect.y,
                                             col_w, row_rect.h);
                if (cell_draw)
                    cell_draw(uu,
                              rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
                              row, c);
            }
        });
    sv.set_content_height(static_cast<f32>(row_count) * row_h);
    return out;
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

    if (detail::key_activate(*this, in) || in.clicked)
    {
        value = !value;
        return true;
    }
    return false;
}
