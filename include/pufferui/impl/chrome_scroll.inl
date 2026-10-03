// pufferui/impl/chrome_scroll.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- custom window chrome ---------------------------------------------------
namespace detail
{
inline constexpr f32 TITLEBAR_H = 34.0f;
inline constexpr f32 TITLE_BTN_W = 36.0f;
inline constexpr f32 CLOSE_BTN_W = 46.0f;

// Aero-style edge snap applied on titlebar-drag release: left/right = the
// work-area half, top = maximize. The zone is re-derived from the release
// point against the display the pointer is on, so a drag near a shared edge
// between two monitors snaps to the screen the user aimed at, not the one the
// window happens to be counted on. No-op without a host or a known work area.
// (Defined with the other snap-assistant helpers below, outside `detail`.)

// A flat glyph button on the titlebar (hover highlight + hand cursor).
inline bool title_button(ui &u, uiid id, rect r, u8 glyph, bool maximized)
{
    interaction in = u.interact(id, r);
    if (in.hovered) u.ctx->want_cursor = CURSOR_HAND;
    if (in.hovered) u.draw_rect(r, u.th().widget_hover);
    const theme &t = u.th();
    const color ic = in.hovered ? t.text : t.text_dim;
    const f32 cx = r.center_x(), cy = r.center_y();
    if (glyph == 2) // close: an X
    {
        detail::thick_line(u, {cx - 4.5f, cy - 4.5f}, {cx + 4.5f, cy + 4.5f}, 2.0f, ic);
        detail::thick_line(u, {cx + 4.5f, cy - 4.5f}, {cx - 4.5f, cy + 4.5f}, 2.0f, ic);
    }
    else if (glyph == 1) // maximize / restore
    {
        if (maximized)
        {
            const rect back{cx - 4.0f, cy - 1.5f, 9.0f, 6.5f};
            const rect front{cx - 2.5f, cy - 4.0f, 9.0f, 6.5f};
            detail::rounded_ring(u, back, ic, 0.0f, 1.0f);
            detail::rounded_ring(u, front, ic, 0.0f, 1.0f);
        }
        else
        {
            detail::rounded_ring(u, rect{cx - 4.5f, cy - 4.5f, 9.0f, 9.0f}, ic, 0.0f, 1.0f);
        }
    }
    else
    {
        u.draw_rect(rect{cx - 4.5f, cy + 3.0f, 9.0f, 1.5f}, ic);
    }
    return in.clicked;
}
} // namespace detail

titlebar_result ui::titlebar(window &w, rect &bounds, std::string_view title)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    titlebar_result out;
    const rect bar = bounds.cut_top(detail::TITLEBAR_H);

    // The drag strip is the bar minus the buttons: the strips never overlap
    // the buttons' rects (the stolen-press guardrail).
    const f32 buttons_w = detail::CLOSE_BTN_W + detail::TITLE_BTN_W * 2.0f;
    const rect drag = rect::make(bar.x, bar.y, max2(0.0f, bar.w - buttons_w), bar.h);
    const uiid root = w.root; // window-scoped ids: two windows never collide
    interaction in = interact(id_child(root, "titlebar"_id), drag);
    const bool maximized = w.maximized;

    draw_rect(bar, t.panel_bg);
    if (in.hovered) draw_rect(drag, t.widget_hover);
    draw_rect(rect::make(bar.x, bar.bottom() - 1.0f, bar.w, 1.0f), t.border);
    if (!title.empty())
    {
        const rect label = drag.pad(12.0f, 0.0f);
        text_scope ts = text_style(13.0f, t.font);
        text_ellipsis(label, title, in.hovered ? t.text : t.text_dim, ALIGN_LEFT);
    }

    // The three glyph buttons, flush with the bar's right edge.
    const rect close_r =
        rect::make(bar.right() - detail::CLOSE_BTN_W, bar.y, detail::CLOSE_BTN_W, bar.h);
    const rect max_r =
        rect::make(close_r.x - detail::TITLE_BTN_W, bar.y, detail::TITLE_BTN_W, bar.h);
    const rect min_r = rect::make(max_r.x - detail::TITLE_BTN_W, bar.y, detail::TITLE_BTN_W, bar.h);
    if (detail::title_button(*this, id_child(root, "tb_min"_id), min_r, 0, maximized))
    {
        out.minimize_clicked = true;
        if (c->host) c->host->minimize_window(w);
    }
    if (detail::title_button(*this, id_child(root, "tb_max"_id), max_r, 1, maximized))
    {
        out.maximize_clicked = true;
        if (c->host)
        {
            c->host->toggle_maximize(w);
        }
    }
    if (detail::title_button(*this, id_child(root, "tb_close"_id), close_r, 2, maximized))
    {
        out.close_clicked = true;
        w.close_requested = true;
    }
    return out;
}

bool ui::slider_float(rect r, std::string_view label, f32 &value, f32 min_value, f32 max_value,
                      uiid id, const char *fmt)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    interaction in = interact(id, r);
    if (in.hovered || in.held) c->want_cursor = CURSOR_HRESIZE;

    const f32 lo = min_value, hi = max_value;
    const f32 range = max2(hi - lo, 0.0001f);
    bool changed = false;
    // Keyboard: a focused slider adjusts with Left/Right (5% of the range per
    // press, Shift = 1%). Consumed so a later widget never sees the press.
    if (in.focused && (ctx->key_pressed[static_cast<i32>(key::LEFT)] ||
                       ctx->key_pressed[static_cast<i32>(key::RIGHT)]))
    {
        const f32 step = range * (c->shift_down ? 0.01f : 0.05f);
        const f32 dir = ctx->key_pressed[static_cast<i32>(key::RIGHT)] ? step : -step;
        ctx->key_pressed[static_cast<i32>(key::LEFT)] = false;
        ctx->key_pressed[static_cast<i32>(key::RIGHT)] = false;
        const f32 v = clampf(value + dir, lo, hi);
        if (v != value)
        {
            value = v;
            changed = true;
        }
    }
    // Jump on the press edge too: a fast click (press+release inside one frame)
    // clears `active` within interact, so testing `active` alone would silently
    // drop the click-to-jump (the drag-anchor guardrail).
    if (in.activated || (c->active == id && c->mouse_down))
    {
        const f32 v = clampf(lo + (c->mouse_x - r.x) / max2(r.w, 1.0f) * range, lo, hi);
        if (v != value)
        {
            value = v;
            changed = true;
        }
    }

    const f32 frac = clampf((value - lo) / range, 0.0f, 1.0f);
    draw_rounded_rect(r, t.widget_bg, t.radius);
    if (frac > 0.0f)
        draw_rounded_rect(rect::make(r.x, r.y, r.w * frac, r.h),
                          in.held ? t.accent_hover : t.accent, t.radius);

    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.1f", static_cast<double>(value));
    const rect inner = r.pad(6.0f, 0.0f);
    if (label.empty())
        text(inner, buf, t.text, ALIGN_CENTER);
    else
    {
        text(inner, label, t.text, ALIGN_LEFT);
        text(inner, buf, t.text, ALIGN_RIGHT);
    }
    if (in.focused) detail::focus_ring(*this, r, t.radius);
    return changed;
}

void ui::progress_bar(rect r, f32 fraction, color fill, color bg)
{
    draw_rounded_rect(r, bg, ctx->active_theme.radius);
    const f32 f = clampf(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        draw_rounded_rect(rect::make(r.x, r.y, r.w * f, r.h), fill, ctx->active_theme.radius);
}

// ---- combo / tooltip / context menu ----------------------------------------
namespace detail
{
// The shared menu-drawing used by the combo dropdown and context menus: a
// rounded panel with one hit rect per item, hover highlight, and mouse hover
// moving the keyboard highlight. `hot` is advanced in place; returns the picked
// index or -1. Called while the enclosing popup layer is open, so its items are
// allowed to interact.
inline i32 menu_items(ui &u, uiid id, rect area, const char *const *labels, i32 count, i32 &hot,
                      const theme &t, bool fresh_open)
{
    const f32 pad = 6.0f;
    const f32 row_h = 26.0f;

    // The viewport clips: with the height clamped (a combo dropdown capped by
    // max_popup_height, or a menu near the screen edge), rows beyond the fold
    // are neither drawn nor hoverable. SCROLL_OVERLAY keeps the bar floating
    // over the clipped rows instead of re-reserving width, and the bar draws
    // after the rows so a row can never steal its press (hit-rect ordering).
    scroll_view sv = u.scroll(area.pad(pad), id, scroll_options{2.0f, SCROLL_OVERLAY});
    if (fresh_open) sv.scroll_to(0.0f); // reopening starts at the top
    // Pre-set the content height before laying out: the rows column then sees
    // the full list height on the very first frame (the r17 first-frame fix)
    // instead of clamping against an empty viewport.
    sv.set_content_height(static_cast<f32>(count) * row_h + static_cast<f32>(count - 1) * 2.0f);
    u.draw_rounded_rect(area, t.panel_bg, 6.0f);
    u.draw_rect(rect::make(area.x, area.y, area.w, 2.0f), t.accent);

    i32 picked = -1;
    column rows(sv.content(), 2.0f);
    for (i32 i = 0; i < count && picked < 0; ++i)
    {
        const uiid item_id = id_child(id, static_cast<uiid>(0xC0DE + static_cast<uiid>(i)));
        const rect row = rows.next(row_h);
        interaction in = u.interact(item_id, row);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        if (in.hovered) hot = i; // mouse and keyboard share one highlight
        if (hot == i && count > 1) u.draw_rounded_rect(row, t.widget_hover, 4.0f);
        u.text(row.pad(8.0f, 0.0f), labels[i], t.text, ALIGN_LEFT);
        if (in.clicked) picked = i;
    }
    return picked;
}
} // namespace detail

bool ui::combo(rect r, std::string_view label, const char *const *items, i32 item_count,
               i32 &selected, uiid id, f32 max_popup_height)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    const uiid drop_id = id_child(id, "dropdown"_id);

    // Was the dropdown open last frame? (popup entries carry over per window)
    bool was_open = false;
    for (i32 i = 0; i < c->prev_popup_depth; ++i)
        if (c->prev_popups[i].id == drop_id) was_open = true;

    interaction in = interact(id, r);
    if (in.hovered) c->want_cursor = t.button_cursor;

    // Closed state: the current item, left-aligned, chevron at the right.
    draw_rounded_rect(r,
                      in.held      ? t.widget_active
                      : in.hovered ? t.widget_hover
                                   : t.widget_bg,
                      t.radius);
    if (t.border_thickness > 0.0f) detail::rounded_ring(*this, r, t.border, t.radius, 1.0f);
    if (in.focused) detail::focus_ring(*this, r, t.radius);
    const char *shown = (selected >= 0 && selected < item_count) ? items[selected] : "";
    const f32 chev_w = 22.0f;
    text(rect::make(r.x + 10.0f, r.y, max2(0.0f, r.w - chev_w - 10.0f), r.h), shown, t.text,
         ALIGN_LEFT);
    if (!label.empty())
        text(rect::make(r.x, r.y, max2(0.0f, r.w - chev_w - 10.0f), r.h), label, t.text_dim,
             ALIGN_RIGHT);
    {
        // Hairpin chevron at the right edge.
        const f32 cx = r.right() - chev_w * 0.5f, cy = r.center_y();
        const color cc = in.hovered ? t.text : t.text_dim;
        detail::thick_line(*this, {cx - 4.0f, cy - 1.5f}, {cx, cy + 2.5f}, 3.0f, cc);
        detail::thick_line(*this, {cx, cy + 2.5f}, {cx + 4.0f, cy - 1.5f}, 3.0f, cc);
    }
    PUFFERUI_CHECK(VIOL_COMBO_EMPTY, item_count > 0, "combo needs at least one item");
    if (item_count <= 0) return false;
    if (selected < 0) selected = 0;
    if (selected >= item_count) selected = item_count - 1;

    // Open on header click — or on Enter when the header owns keyboard focus
    // (the dropdown's own Up/Down/Enter navigation takes over once open).
    // Closing happens elsewhere: a press on the header while open lands
    // *outside* the dropdown rect, so the click-outside rule marks it next
    // frame and the deferred pass closes it - no toggle branch needed here.
    // Keyboard events ring while the dropdown is open.
    bool keyboard_open = false;
    if (in.focused && ctx->key_pressed[static_cast<i32>(key::ENTER)])
    {
        consume_key(key::ENTER);
        keyboard_open = true;
    }
    bool open = was_open || in.clicked || keyboard_open;
    if (in.clicked || keyboard_open) c->combo_hot = selected; // keyboard starts at the current item

    // A pick resolved in the *previous* frame's deferred pass reports here
    // and lands in the model atomically with the `changed` report — no
    // write-back pointer into caller storage exists anymore.
    bool changed = (c->defer_result_id == drop_id);
    if (c->defer_result_id == drop_id)
    {
        selected = c->defer_result_pick;
        c->defer_result_id = 0; // consumed
    }

    // Keyboard on the dropdown: Up/Down move the *highlight* (Enter commits),
    // Escape closes. A header click that opened *this* frame skips this: the
    // same press must not double as a pick or a close.
    if (open && was_open)
    {
        if (c->key_pressed[static_cast<i32>(key::DOWN)])
            c->combo_hot = (c->combo_hot + 1) % item_count;
        if (c->key_pressed[static_cast<i32>(key::UP)])
            c->combo_hot = (c->combo_hot + item_count - 1) % item_count;
        if (c->key_pressed[static_cast<i32>(key::ENTER)])
        {
            if (c->combo_hot >= 0 && c->combo_hot < item_count && c->combo_hot != selected)
            {
                selected = c->combo_hot;
                changed = true;
            }
            open = false;
        }
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)]) open = false;
        // An outside press (anywhere but the dropdown) closes.
        if (c->popup_click_outside && c->popup_click_id == drop_id) open = false;
    }

    // Dropdown pass: the surface is deferred to end_frame, where it draws above
    // every base widget and resolves mouse picks (immediate mode paints in call
    // order; a popup surface drawn here would sit under anything after it).
    if (open)
    {
        // Size from content: every item must fit inside the panel. Width is the
        // widest label (at least the header), height is the clamped list.
        const f32 pad = 6.0f;
        const f32 row_h = 26.0f;
        const f32 gap = 2.0f;
        f32 widest = text_width(shown) + chev_w; // never narrower than the header
        for (i32 i = 0; i < item_count; ++i) widest = max2(widest, text_width(items[i]) + 16.0f);
        const f32 list_h = item_count * row_h + static_cast<f32>(item_count - 1) * gap;
        const f32 h =
            clampf(list_h + pad * 2.0f, row_h + pad * 2.0f, max2(row_h, max_popup_height));
        const rect screen = c->screen;
        const f32 w = clampf(widest, r.w, screen.w);
        // Below the header by default; flip above when there is no room below.
        f32 x = clampf(r.x, screen.x, max2(screen.x, screen.right() - w));
        f32 y = r.bottom() + 4.0f;
        if (y + h > screen.bottom() && r.y - h - 4.0f >= screen.y)
            y = r.y - h - 4.0f; // above the header
        y = min2(y, max2(screen.y, screen.bottom() - h));

        // One menu surface at a time: taking the slot closes whatever held it.
        if (c->defer_menu_id != 0 && c->defer_menu_id != drop_id)
        {
            c->defer_menu_id = 0;
            c->defer_count = 0;
        }
        c->defer_menu_id = drop_id;
        c->defer_menu = rect::make(x, y, w, h);
        defer_set_labels(c, items, item_count);
        c->defer_fresh = !was_open;
    }
    return changed;
}

rect ui::tooltip(rect anchor, uiid id, std::string_view tip)
{
    context *c = ctx;
    if (c->hot != id) return rect{}; // only while the anchor widget is hovered

    // Delay queue: the first tooltip waits 0.5 s; another tooltip seen within
    // 1.2 s of the last one opens instantly (moving between toolbar buttons).
    const f64 now = c->now;
    static constexpr f64 TOOLTIP_DELAY = 0.5;
    const bool recent = (c->tip_last_shown >= 0.0) && (now - c->tip_last_shown) < 1.2;
    if (!recent && c->tip_last_id != id)
    {
        // Wait the delay out on first hover of this widget (stateless per
        // widget: the wait clock restarts whenever the queue turns over).
        if (c->tip_stuck_id != id)
        {
            c->tip_stuck_id = id;
            c->tip_wait_start = now;
        }
        if (now - c->tip_wait_start < TOOLTIP_DELAY) return rect{};
    }
    c->tip_last_id = id;
    c->tip_last_shown = now;

    const theme &t = c->active_theme;
    const f32 tw = text_width(tip) + 16.0f;
    const f32 th = line_height() + 10.0f;
    // Under the anchor by default; flip above when there is no room below.
    f32 x = anchor.center_x() - tw * 0.5f;
    f32 y = anchor.bottom() + 9.0f;
    const rect screen = c->screen;
    x = clampf(x, screen.x + 4.0f, max2(screen.x + 4.0f, screen.right() - tw - 4.0f));
    if (y + th > screen.bottom()) y = max2(screen.y + 4.0f, anchor.y - th - 9.0f);
    const rect tip_rect = rect::make(x, y, tw, th);
    if (!tip_rect.is_valid() || tip_rect.w < 8.0f) return rect{}; // degenerate anchor

    // The paint defers to end_frame: drawn after every base widget, a tooltip
    // can never be covered by later-drawn content (only one at a time; the
    // last call this frame wins).
    c->defer_tip = tip_rect;
    const usize copy = min2(tip.size(), sizeof(c->defer_tip_text) - 1);
    std::memcpy(c->defer_tip_text, tip.data(), copy);
    c->defer_tip_text[copy] = '\0';
    return tip_rect;
}

i32 ui::context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    const uiid menu_id = id_child(id, "menu"_id);

    // Was the menu open last frame?
    bool open = false;
    for (i32 i = 0; i < c->prev_popup_depth; ++i)
        if (c->prev_popups[i].id == menu_id) open = true;
    const bool fresh_open = !open; // opening this frame: reset the item scroll

    // Open on right-press over the anchor. The press that opened must not also
    // be seen by the menu's items: menu_items only reads clicks, and the press/
    // release pair completes before the menu paints, so a same-frame open is
    // safe (the click needs press+release on the same item).
    if (!open && c->right_pressed)
    {
        // Right-press opens only when it lands over the anchor; test the
        // position directly so an in-progress drag is never disturbed.
        if (anchor.contains(c->mouse_x, c->mouse_y) && c->active == 0)
        {
            open = true;
            c->context_menu_x = c->mouse_x;
            c->context_menu_y = c->mouse_y;
        }
    }

    i32 picked = -1;
    // A pick resolved in the previous frame's deferred pass reports here.
    if (c->defer_result_id == menu_id)
    {
        picked = c->defer_result_pick;
        c->defer_result_id = 0; // consumed
    }
    if (open)
    {
        // Close on escape or outside press (recorded last frame).
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)])
            open = false;
        else if (c->popup_click_outside && c->popup_click_id == menu_id)
            open = false;
    }

    if (open)
    {
        // Measure the menu: widest label, one row each; goes under the pointer.
        f32 tw = 0.0f;
        f32 rows_h = 0.0f;
        for (i32 i = 0; i < item_count; ++i)
        {
            tw = max2(tw, text_width(item_labels[i]));
            rows_h += 26.0f;
        }
        if (item_count > 1) rows_h += static_cast<f32>(item_count - 1) * 2.0f;
        const f32 pad = 6.0f;
        const f32 menu_w = tw + pad * 2.0f + 16.0f;
        const f32 menu_h = rows_h + pad * 2.0f;
        const rect screen = c->screen;
        f32 x = c->context_menu_x;
        f32 y = c->context_menu_y;
        x = clampf(x, screen.x + 2.0f, max2(screen.x + 2.0f, screen.right() - menu_w - 2.0f));
        y = clampf(y, screen.y + 2.0f, max2(screen.y + 2.0f, screen.bottom() - menu_h - 2.0f));

        // One menu surface at a time: taking the slot closes whatever held it.
        if (c->defer_menu_id != 0 && c->defer_menu_id != menu_id)
        {
            c->defer_menu_id = 0;
            c->defer_count = 0;
        }
        c->defer_menu_id = menu_id;
        c->defer_menu = rect::make(x, y, menu_w, menu_h);
        defer_set_labels(c, item_labels, item_count);
        c->defer_fresh = fresh_open;
    }
    return picked;
}

panel_scope ui::panel(std::string_view title, rect &bounds, const panel_opts &opts)
{
    return panel(title, bounds, opts.flags, opts.dock_panel, opts.dock_name, opts.style);
}

panel_scope ui::panel(std::string_view title, rect &bounds, u32 flags, uiid dock_panel,
                      const char *dock_name, panel_override ov)
{
    const panel_style s = merge_style(ctx->active_theme.panel, ov);
    const uiid id = hash(title);
    const bool no_titlebar = (flags & PANEL_NO_TITLEBAR) != 0;
    const bool no_controls = (flags & PANEL_NO_CONTROLS) != 0;
    const bool no_drag = (flags & PANEL_NO_DRAG) != 0;
    const bool no_shadow = (flags & PANEL_NO_SHADOW) != 0;
    const f32 titlebar_h = no_titlebar ? 0.0f : s.titlebar_h;

    // Enter this panel's input layer *before* any of its own widgets (titlebar,
    // close) are tested, so the panel does not block itself.
    bool layer_pushed = false;
    detail::ensure_panel_stack_capacity(ctx, ctx->panel_stack_depth + 1);
    ctx->panel_stack[ctx->panel_stack_depth++] = id;
    layer_pushed = true;

    // Control hit rect (hit-testing only) so the titlebar drag does not steal a
    // press that landed on the close dot. The titlebar keeps its full width.
    rect close_hit{};
    if (!no_controls && titlebar_h > 0.0f)
    {
        const rect inner0 = bounds.pad(s.border_thickness);
        const rect tb0 = {inner0.x, inner0.y, inner0.w, titlebar_h};
        close_hit = {tb0.right() - 10.0f - 12.0f, tb0.y + (tb0.h - 12.0f) * 0.5f, 12.0f, 12.0f};
    }
    const bool over_close =
        (!no_controls && titlebar_h > 0.0f) && close_hit.contains(ctx->mouse_x, ctx->mouse_y);

    // dragging (titlebar), clamped so a strip always stays reachable
    if (!no_drag && titlebar_h > 0.0f)
    {
        const rect title_rect = {bounds.x, bounds.y, bounds.w, titlebar_h};
        interaction inh = interact(id_child(id, 1), title_rect, !over_close);
        if (inh.hovered) ctx->want_cursor = CURSOR_HAND;
        if (ctx->dragging_panel == id)
        {
            if (ctx->mouse_down)
            {
                bounds.x = ctx->mouse_x - ctx->drag_dx;
                bounds.y = ctx->mouse_y - ctx->drag_dy;
                // Clamp to the virtual desktop (in this window's local space) so a
                // floating panel can be dragged from one window to another.
                const rect desk = detail::desktop_local(ctx);
                if (desk.w > 1.0f && desk.h > 1.0f)
                {
                    const f32 min_vis = min2(60.0f, bounds.w);
                    bounds.x =
                        clampf(bounds.x, desk.x - bounds.w + min_vis, desk.right() - min_vis);
                    bounds.y = clampf(bounds.y, desk.y, desk.bottom() - min_vis);
                }
                // A dockable floating panel participates in the dock drag, so it
                // can be dropped onto the dock space like a tab.
                if (dock_panel != 0)
                {
                    if (ctx->dock_panel == 0)
                    {
                        ctx->dock_panel = dock_panel;
                        ctx->dock_panel_name = detail::intern_dock_name(ctx, dock_name);
                        ctx->dock_press_x = ctx->mouse_x;
                        ctx->dock_press_y = ctx->mouse_y;
                        ctx->dock_dragging = false;
                    }
                    else
                    {
                        const f32 dx = ctx->mouse_x - ctx->dock_press_x;
                        const f32 dy = ctx->mouse_y - ctx->dock_press_y;
                        if (dx * dx + dy * dy > 16.0f) ctx->dock_dragging = true;
                    }
                }
            }
            else
            {
                ctx->dragging_panel = 0;
            }
        }
        else if (inh.activated)
        {
            ctx->dragging_panel = id;
            ctx->drag_dx = ctx->mouse_x - bounds.x;
            ctx->drag_dy = ctx->mouse_y - bounds.y;
            if (dock_panel != 0)
            {
                ctx->dock_panel = dock_panel;
                ctx->dock_panel_name = detail::intern_dock_name(ctx, dock_name);
                ctx->dock_press_x = ctx->mouse_x;
                ctx->dock_press_y = ctx->mouse_y;
                ctx->dock_dragging = false;
            }
        }
    }

    // Register this panel with the final bounds: next frame these rects block
    // interaction with base UI underneath (one-frame model, like popups).
    detail::ensure_panel_capacity(ctx, ctx->panel_depth + 1);
    ctx->panels[ctx->panel_depth++] =
        panel_entry{id, bounds, ctx->current_window ? ctx->current_window->index : 0};

    // Opaque backgrounds keep the classic two-rect outline; translucent ones use
    // the ring path so the backdrop shows through (frosted glass). A filled
    // shadow rect only reads as a shadow behind an opaque background.
    const bool opaque_bg = s.bg.a >= 250;
    if (s.shadow && !no_shadow && opaque_bg)
        draw_rounded_rect(rect::make(bounds.x + 3.0f, bounds.y + 3.0f, bounds.w, bounds.h),
                          color{0, 0, 0, 70}, s.radius);

    const rect inner = bounds.pad(s.border_thickness);
    if (s.border_thickness > 0.0f && s.border.a > 0 && opaque_bg)
    {
        draw_rounded_rect(bounds, s.border, s.radius);
        draw_rounded_rect(inner, s.bg, max2(0.0f, s.radius - s.border_thickness));
    }
    else
    {
        if (s.bg.a > 0) draw_rounded_rect(bounds, s.bg, s.radius);
        detail::rounded_ring(*this, bounds, s.border, s.radius, s.border_thickness);
    }

    bool close_clicked = false;
    if (titlebar_h > 0.0f)
    {
        const rect tb = {inner.x, inner.y, inner.w, titlebar_h};
        draw_rounded_rect(tb, s.titlebar_bg, max2(0.0f, s.radius - s.border_thickness));
        text(tb.pad(s.padding, 0.0f), title, s.titlebar_text, ALIGN_LEFT);

        if (!no_controls)
        {
            const rect close = {tb.right() - 10.0f - 12.0f, tb.y + (tb.h - 12.0f) * 0.5f, 12.0f,
                                12.0f};
            interaction inx = interact(id_child(id, 2), close);
            if (inx.hovered) ctx->want_cursor = CURSOR_HAND; // the close dot is clickable
            color cc = inx.hovered ? s.close : color{s.close.r, s.close.g, s.close.b, 200};
            draw_rounded_rect(close, cc, 6.0f);
            if (inx.clicked) close_clicked = true;
        }
    }

    const rect client = {inner.x, inner.y + titlebar_h, inner.w, inner.h - titlebar_h};
    const rect content = client.pad(s.padding);
    return panel_scope(*this, id, bounds, client, content, close_clicked, layer_pushed);
}

panel_scope::~panel_scope()
{
    if (!u_ || !layer_pushed) return;
    context *c = u_->ctx;
    if (c->panel_stack_depth > 0) c->panel_stack_depth -= 1;
}

// ---- scroll views ----------------------------------------------------------
namespace detail
{
// The layer checks `interact` performs (popups/panels swallowing input), minus
// side effects; used to decide whether wheel input reaches a scroll view.
inline bool pointer_blocked(context *c, f32 x, f32 y)
{
    if (c->prev_popup_depth > 0)
    {
        const popup_entry &top = c->prev_popups[c->prev_popup_depth - 1];
        if (!top.area.contains(x, y)) return true;
        if (!c->in_popup) return true;
    }
    if (c->prev_panel_depth > 0)
    {
        for (i32 i = c->prev_panel_depth - 1; i >= 0; --i)
        {
            if (c->prev_panels[i].area.contains(x, y))
            {
                const bool inside =
                    c->panel_stack_depth > 0 &&
                    c->panel_stack[c->panel_stack_depth - 1] == c->prev_panels[i].id;
                if (!inside) return true;
                break;
            }
        }
    }
    return false;
}
} // namespace detail

scroll_view ui::scroll(rect viewport, uiid id, scroll_options ov)
{
    return scroll_view(*this, id, viewport, ov);
}

scroll_view::scroll_view(ui &u, uiid id, rect viewport, scroll_options ov)
{
    u_ = &u;
    id_ = id;
    viewport_ = viewport;
    padding_ = ov.padding;
    flags_ = ov.flags;

    context *c = u.ctx;
    PUFFERUI_CHECK(VIOL_INVALID_AREA, viewport.is_valid(), "scroll viewport is invalid");

    scroll_store::entry &st = ensure_scrolls(c)->map[id];
    st.last_used = c->now;

    const scroll_style &sb = c->active_theme.scrollbar;
    const f32 gutter = sb.thickness + sb.margin * 2.0f;

    // Overflow comes from the previous frame's content height; the current one is
    // only known after the caller has laid out.
    overflows_ = st.content_h > viewport.h + 0.5f;
    const bool reserve =
        (flags_ & SCROLL_ALWAYS_RESERVE_BAR) != 0 || (!(flags_ & SCROLL_OVERLAY) && overflows_);
    const f32 gutter_w = reserve ? gutter : 0.0f;

    const f32 max_scroll = max2(0.0f, st.content_h - viewport.h);
    st.offset = clampf(st.offset, 0.0f, max_scroll);

    // Scrollbar geometry and the thumb press happen before content widgets, so an
    // overlay bar cannot be stolen by a widget underneath it.
    if (overflows_)
    {
        const f32 track_h = max2(0.0f, viewport.h - sb.margin * 2.0f);
        const f32 thumb_h = max2(sb.min_thumb, track_h * (viewport.h / max2(st.content_h, 1.0f)));
        const f32 travel = max2(0.0f, track_h - thumb_h);
        const f32 ratio = (max_scroll > 0.0f) ? (st.offset / max_scroll) : 0.0f;
        const f32 track_x = viewport.right() - sb.thickness - sb.margin;
        bar_track_ = rect::make(track_x, viewport.y + sb.margin, sb.thickness, track_h);
        bar_thumb_ = rect::make(track_x, bar_track_.y + ratio * travel, sb.thickness, thumb_h);

        const uiid thumb_id = id_child(id_, 0x5C);
        interaction in = u.interact(thumb_id, bar_thumb_);
        thumb_hovered_ = in.hovered || in.held;
        if (thumb_hovered_) c->want_cursor = CURSOR_HAND;
        if (in.activated)
        {
            st.dragging = true;
            st.drag_anchor = c->mouse_y;
            st.drag_offset = st.offset;
        }
        if (st.dragging && c->active == thumb_id && c->mouse_down)
        {
            const f32 delta = c->mouse_y - st.drag_anchor;
            st.offset =
                clampf(st.drag_offset + (travel > 0.0f ? (delta / travel) * max_scroll : 0.0f),
                       0.0f, max_scroll);
        }
        else if (!c->mouse_down)
        {
            st.dragging = false;
        }
    }
    else
    {
        st.dragging = false;
        bar_track_ = {};
        bar_thumb_ = {};
        thumb_hovered_ = false;
    }

    // Wheel claim: the innermost hovered view wins (constructors run outer->inner).
    if (viewport.contains(c->mouse_x, c->mouse_y) &&
        !detail::pointer_blocked(c, c->mouse_x, c->mouse_y))
        c->wheel_target = id_;

    if (!(flags_ & SCROLL_NO_CLIP) && c->clip_depth < MAX_CLIP_DEPTH)
    {
        const rect parent_clip = (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1]
                                                     : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
        c->clip_stack[c->clip_depth++] = rect::intersect(viewport_, parent_clip);
        clip_pushed_ = true;
    }

    content_h_ = st.content_h;
    content_ = rect::make(viewport.x + padding_, viewport.y - st.offset + padding_,
                          max2(0.0f, viewport.w - padding_ * 2.0f - gutter_w), content_h_);
}

scroll_view::~scroll_view()
{
    if (!u_) return;
    context *c = u_->ctx;
    scroll_store::entry &st = ensure_scrolls(c)->map[id_];
    st.last_used = c->now;

    // Wheel input lands here so nested views consume it inner-first.
    if (c->wheel_target == id_)
    {
        const f32 speed = 30.0f;
        st.offset =
            clampf(st.offset - c->wheel_y * speed, 0.0f, max2(0.0f, st.content_h - viewport_.h));
        c->wheel_target = 0;
    }

    if (overflows_)
    {
        const scroll_style &sb = c->active_theme.scrollbar;
        u_->draw_rounded_rect(bar_track_, sb.track, sb.radius);
        color tc = thumb_hovered_ ? sb.thumb_hover : sb.thumb;
        if (st.dragging) tc = sb.thumb_active;
        u_->draw_rounded_rect(bar_thumb_, tc, sb.radius);
    }

    if (clip_pushed_ && c->clip_depth > 0)
    {
        c->clip_depth -= 1;
        clip_pushed_ = false;
    }
}

f32 scroll_view::offset() const
{
    if (!u_) return 0.0f;
    scroll_store *ss = &u_->ctx->st->scrolls;
    auto it = ss->map.find(id_);
    return (it == ss->map.end()) ? 0.0f : it->second.offset;
}

void scroll_view::set_content_height(f32 height)
{
    content_h_ = max2(0.0f, height);
    if (!u_) return;
    context *c = u_->ctx;
    scroll_store::entry &st = ensure_scrolls(c)->map[id_];
    st.content_h = content_h_;
    st.viewport_h = viewport_.h;
    st.offset = clampf(st.offset, 0.0f, max2(0.0f, st.content_h - viewport_.h));
    content_.h = content_h_;
    content_.y = viewport_.y - st.offset + padding_;
}

void scroll_view::scroll_to(f32 offset)
{
    if (!u_) return;
    scroll_store::entry &st = ensure_scrolls(u_->ctx)->map[id_];
    st.offset = clampf(offset, 0.0f, max2(0.0f, st.content_h - viewport_.h));
    content_.y = viewport_.y - st.offset + padding_;
}

void scroll_view::scroll_by(f32 delta)
{
    scroll_to(offset() + delta);
}

void scroll_view::ensure_visible(rect r, f32 margin)
{
    if (!u_) return;
    const f32 off = offset();
    const f32 top = viewport_.y + margin;
    const f32 bottom = viewport_.bottom() - margin;
    if (r.y < top)
        scroll_to(off - (top - r.y));
    else if (r.bottom() > bottom)
        scroll_to(off + (r.bottom() - bottom));
}

void scroll_view::virtual_list(i32 count, f32 item_height, function_ref<void(ui &, i32, rect)> fn)
{
    if (!u_) return;
    PUFFERUI_CHECK(VIOL_INVALID_AREA, item_height > 0.0f,
                   "virtual_list item height must be positive");
    if (count <= 0 || item_height <= 0.0f)
    {
        set_content_height(0.0f);
        return;
    }

    // The content height is derived: the scroll extent and the gutter both
    // follow from the list, so callers never repeat count * item_height.
    set_content_height(static_cast<f32>(count) * item_height);

    // Visible slice on the item grid: the first row straddling the viewport
    // top (its top edge sits above it by off % item_height), then enough rows
    // to cover the viewport plus the bottom straddler. Clamped to the list —
    // no callback ever runs for an index past the end.
    const f32 off = offset();
    i32 first = (item_height > 0.0f) ? static_cast<i32>(off / item_height) : 0;
    if (first < 0) first = 0;
    if (first > count) first = count;
    i32 visible = static_cast<i32>(viewport_.h / item_height) + 2;
    if (first + visible > count) visible = count - first;
    if (visible < 0) visible = 0;

    for (i32 i = 0; i < visible; ++i)
    {
        const i32 index = first + i;
        const rect row = rect::make(content_.x, content_.y + static_cast<f32>(index) * item_height,
                                    content_.w, item_height);
        fn(*u_, index, row);
    }
}

// ---- interactive splitters ----
namespace detail
{

inline f32 &split_offset(context *c, uiid id)
{
    return c->st->splits.offsets[id];
}

} // namespace detail

std::pair<rect, rect> ui::split_horizontal_interactive(uiid id, rect bounds, f32 *position,
                                                       f32 min_left, f32 max_left, f32 thickness,
                                                       f32 gap)
{
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, bounds.is_valid(),
                   "split_horizontal_interactive invalid bounds");
    if (!position) return {bounds, rect{}};

    const f32 max_allowed =
        max2(min_left, min2(max_left, bounds.w - min_left - thickness - gap * 2.0f));
    *position = clampf(*position, min_left, max_allowed);

    const rect handle = {bounds.x + *position + gap, bounds.y, thickness, bounds.h};
    interaction in = interact(id, handle);
    if (in.hovered || in.held) ctx->want_cursor = CURSOR_HRESIZE;

    f32 &offset = detail::split_offset(ctx, id);
    if (in.activated) offset = ctx->mouse_x - (bounds.x + *position);
    if (ctx->active == id && ctx->mouse_down)
        *position = clampf(ctx->mouse_x - offset - bounds.x, min_left, max_allowed);

    const rect left = {bounds.x, bounds.y, *position, bounds.h};
    const f32 right_x = bounds.x + *position + gap + thickness + gap;
    const rect right = {right_x, bounds.y, max2(0.0f, bounds.right() - right_x), bounds.h};

    const color col = (ctx->active == id)
                          ? ctx->active_theme.accent
                          : (in.hovered ? ctx->active_theme.accent_hover : color{45, 53, 64, 160});
    draw_rounded_rect(handle, col, 2.0f);
    return {left, right};
}

std::pair<rect, rect> ui::split_vertical_interactive(uiid id, rect bounds, f32 *position,
                                                     f32 min_top, f32 max_top, f32 thickness,
                                                     f32 gap)
{
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, bounds.is_valid(),
                   "split_vertical_interactive invalid bounds");
    if (!position) return {bounds, rect{}};

    const f32 max_allowed =
        max2(min_top, min2(max_top, bounds.h - min_top - thickness - gap * 2.0f));
    *position = clampf(*position, min_top, max_allowed);

    const rect handle = {bounds.x, bounds.y + *position + gap, bounds.w, thickness};
    interaction in = interact(id, handle);
    if (in.hovered || in.held) ctx->want_cursor = CURSOR_VRESIZE;

    f32 &offset = detail::split_offset(ctx, id);
    if (in.activated) offset = ctx->mouse_y - (bounds.y + *position);
    if (ctx->active == id && ctx->mouse_down)
        *position = clampf(ctx->mouse_y - offset - bounds.y, min_top, max_allowed);

    const rect top = {bounds.x, bounds.y, bounds.w, *position};
    const f32 bottom_y = bounds.y + *position + gap + thickness + gap;
    const rect bottom = {bounds.x, bottom_y, bounds.w, max2(0.0f, bounds.bottom() - bottom_y)};

    const color col = (ctx->active == id)
                          ? ctx->active_theme.accent
                          : (in.hovered ? ctx->active_theme.accent_hover : color{45, 53, 64, 160});
    draw_rounded_rect(handle, col, 2.0f);
    return {top, bottom};
}

// ---- docking ----
namespace detail
{

struct dock_area
{
    dock_node *node;
    rect area;
};

inline i32 dock_zone_for(const rect &area, f32 mx, f32 my)
{
    const f32 ex = area.w * 0.25f, ey = area.h * 0.25f;
    if (mx < area.x + ex) return DOCK_ZONE_LEFT;
    if (mx > area.right() - ex) return DOCK_ZONE_RIGHT;
    if (my < area.y + ey) return DOCK_ZONE_TOP;
    if (my > area.bottom() - ey) return DOCK_ZONE_BOTTOM;
    return DOCK_ZONE_CENTER;
}

inline void dock_draw_node(ui &u, uiid node_id, dock_node &n, rect area, i32 depth,
                           function_ref<void(uiid, rect, bool)> draw_panel, dock_area *areas,
                           i32 &area_count, context *c)
{
    if (depth > MAX_DOCK_DEPTH) return;
    if (area.w <= 1.0f || area.h <= 1.0f) return;

    if (n.kind == DOCK_SPLIT_H && n.a && n.b)
    {
        f32 pos = area.w * n.ratio;
        const auto halves =
            u.split_horizontal_interactive(node_id, area, &pos, 40.0f, area.w - 80.0f, 6.0f, 2.0f);
        n.ratio = (area.w > 0.0f) ? clampf(pos / area.w, 0.0f, 1.0f) : 0.5f;
        dock_draw_node(u, id_child(node_id, 1), *n.a, halves.first, depth + 1, draw_panel, areas,
                       area_count, c);
        dock_draw_node(u, id_child(node_id, 2), *n.b, halves.second, depth + 1, draw_panel, areas,
                       area_count, c);
        return;
    }
    if (n.kind == DOCK_SPLIT_V && n.a && n.b)
    {
        f32 pos = area.h * n.ratio;
        const auto halves =
            u.split_vertical_interactive(node_id, area, &pos, 40.0f, area.h - 80.0f, 6.0f, 2.0f);
        n.ratio = (area.h > 0.0f) ? clampf(pos / area.h, 0.0f, 1.0f) : 0.5f;
        dock_draw_node(u, id_child(node_id, 1), *n.a, halves.first, depth + 1, draw_panel, areas,
                       area_count, c);
        dock_draw_node(u, id_child(node_id, 2), *n.b, halves.second, depth + 1, draw_panel, areas,
                       area_count, c);
        return;
    }

    // leaf / tabs: tab strip + active panel content
    rect content = area;
    if (n.panel_count > 0)
    {
        const f32 tab_h = 26.0f;
        rect bar = content.cut_top(tab_h);
        u.draw_rect(bar, u.th().bg);

        // Clip the strip: tabs never draw outside the node, and a tab that would
        // not fit is simply not emitted (instead of overlapping its neighbours).
        {
            region bar_clip(u, id_child(node_id, 500ull), bar, true);
            row tabs(bar, 4.0f);
            for (i32 i = 0; i < n.panel_count; ++i)
            {
                const f32 avail = tabs.remaining().w;
                if (avail <= 8.0f) break;
                const f32 tw = min2(110.0f, avail);
                rect tr = tabs.next(tw);
                if (tr.w <= 8.0f) break;

                const uiid tid = id_child(node_id, 1000ull + static_cast<u64>(i));
                interaction in = u.interact(tid, tr);
                if (in.hovered || in.held) c->want_cursor = CURSOR_HAND; // draggable tab
                const bool sel = (i == n.active);
                const color col =
                    sel ? u.th().panel_bg : (in.hovered ? u.th().widget_hover : u.th().bg);
                u.draw_rect(tr, col);
                if (const char *nm = n.panel_names[i] ? n.panel_names[i] : "panel")
                    if (u.text_width(nm) <= tr.w - 8.0f) u.text(tr, nm, u.th().text, ALIGN_CENTER);

                if (in.clicked) n.active = i;
                if (in.activated)
                {
                    c->dock_panel = n.panels[i];
                    c->dock_source = &n;
                    c->dock_panel_name = n.panel_names[i];
                    c->dock_dragging = false;
                    c->dock_press_x = c->mouse_x;
                    c->dock_press_y = c->mouse_y;
                }
                if (c->dock_panel == n.panels[i] && c->mouse_down)
                {
                    const f32 dx = c->mouse_x - c->dock_press_x;
                    const f32 dy = c->mouse_y - c->dock_press_y;
                    if (dx * dx + dy * dy > 16.0f) c->dock_dragging = true;
                }
            }
        }
    }

    if (n.panel_count > 0)
    {
        if (n.active < 0 || n.active >= n.panel_count) n.active = 0;
        if (area_count < 32) areas[area_count++] = dock_area{&n, area};
        if (draw_panel)
        {
            // Clip panel content to its node so shrinking a split never lets
            // content spill into the neighbour.
            region content_clip(u, id_child(node_id, 700ull), content, true);
            draw_panel(n.panels[n.active], content, true);
        }
    }
}

} // namespace detail

dock_action ui::dock_space(uiid id, rect area, dock_node &root,
                           function_ref<void(uiid, rect, bool)> draw_panel)
{
    context *c = ctx;
    detail::dock_area areas[32];
    i32 area_count = 0;
    detail::dock_draw_node(*this, id, root, area, 0, draw_panel, areas, area_count, c);

    dock_action action;

    // highlight the drop zone while dragging a tab
    if (c->dock_dragging)
    {
        for (i32 i = area_count - 1; i >= 0; --i)
        {
            if (!areas[i].area.contains(c->mouse_x, c->mouse_y)) continue;
            const i32 zone = detail::dock_zone_for(areas[i].area, c->mouse_x, c->mouse_y);
            rect hl = areas[i].area;
            const f32 w4 = hl.w * 0.25f, h4 = hl.h * 0.25f;
            switch (zone)
            {
            case DOCK_ZONE_LEFT:
                hl.w = w4;
                break;
            case DOCK_ZONE_RIGHT:
                hl.x = hl.right() - w4;
                hl.w = w4;
                break;
            case DOCK_ZONE_TOP:
                hl.h = h4;
                break;
            case DOCK_ZONE_BOTTOM:
                hl.y = hl.bottom() - h4;
                hl.h = h4;
                break;
            default:
                break;
            }
            draw_rect(hl, color{60, 180, 255, 90});
            break;
        }

        // drag ghost so the drag is visible
        if (c->dock_panel_name)
        {
            rect ghost = {c->mouse_x + 14.0f, c->mouse_y + 10.0f, 110.0f, 22.0f};
            draw_rounded_rect(ghost, color{30, 40, 58, 220}, 4.0f);
            text(ghost, c->dock_panel_name, ctx->active_theme.text, ALIGN_CENTER);
        }
    }

    // release ends a drag: report the move; the app applies it
    if (c->mouse_released && c->dock_panel != 0)
    {
        if (c->dock_dragging)
        {
            action.active = true;
            action.panel = c->dock_panel;
            action.target = nullptr; // nullptr == undock/floating
            action.zone = DOCK_ZONE_CENTER;
            for (i32 i = area_count - 1; i >= 0; --i)
            {
                if (areas[i].area.contains(c->mouse_x, c->mouse_y))
                {
                    action.target = areas[i].node;
                    action.zone = detail::dock_zone_for(areas[i].area, c->mouse_x, c->mouse_y);
                    break;
                }
            }
        }
        c->dock_panel = 0;
        c->dock_source = nullptr;
        c->dock_panel_name = nullptr;
        c->dock_dragging = false;
    }

    return action;
}
