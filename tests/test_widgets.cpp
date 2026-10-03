// widgets: styles, panels, popups, combos, scroll.
#include "test_util.h"

PUI_TEST(test_style_cascade)
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    set_button_role(t, "primary"_id, button_override{.bg = some(color{10, 20, 30, 255})});
    set_theme(c, t);
    ui u(c);
    button_style s1 = u.resolve_button_style("primary"_id, {});
    CHECK(s1.bg.r == 10 && s1.bg.g == 20 && s1.bg.b == 30);

    {
        style_scope sc(u, button_override{.bg = some(color{1, 2, 3, 255})});
        button_style s2 = u.resolve_button_style("primary"_id, {});
        CHECK(s2.bg.r == 1 && s2.bg.g == 2 && s2.bg.b == 3);
    }
    button_style s3 = u.resolve_button_style("primary"_id, {});
    // scope popped
    button_style s4 =
        u.resolve_button_style("primary"_id, button_override{.bg = some(color{9, 9, 9, 255})});
    // button_override{.bg = some(color{9, 9, 9, 255})});
    CHECK(s4.bg.r == 9);
    // instance override wins
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_popups)
{
    context *c = create_context(nullptr);
    const rect menu = {0, 0, 100, 100};
    // frame 1: open the popup (nothing prior -> no input blocking yet)
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        CHECK(p.open);
        CHECK(!p.close_requested);
        CHECK(c->popup_depth == 1);
    }
    // the popup entry persists for the frame (consumed next frame for input)
    CHECK(c->popup_depth == 1);
    end_frame(c);
    // frame 2: a click outside the previously-open popup is captured
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 300, 300);
    mouse_button(c, true);

    {
        ui u(c);
        interaction bg = u.interact("bg"_id, rect{0, 0, 400, 400});
        CHECK(!bg.hovered);
        // blocked by the popup
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        CHECK(p.close_requested);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_panel_flags)
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        const panel_style &ps = u.th().panel;
        rect b = {10, 10, 200, 120};

        {
            panel_scope p = u.panel(
                "NoTitle", b, {.id = "notitle"_id, .flags = PANEL_NO_TITLEBAR | PANEL_NO_SHADOW});
            CHECK(p.open);
            // without a titlebar the client starts right below the
            // border
            CHECK(p.content().y == b.y + ps.border_thickness + ps.padding);
        }
        CHECK(c->clip_depth == 0);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_state_view_update)
{
    context *c = create_context(nullptr);
    sv_state s;
    // update_view derives; the model is untouched
    s.update_view();
    CHECK(s.view.count == 0 && !s.view.open);
    CHECK(std::strcmp(s.view.label, "count 0") == 0);
    CHECK(s.count == 0 && !s.open);
    const rect btn = {0, 30, 100, 24};
    // frame 1: press (captures)
    mouse_move(c, 50, 42);
    mouse_button(c, true);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        u.text(rect::make(0, 0, 100, 20), s.view.label, color::white());
        (void)u.button(btn, "add", "sv_add_btn"_id);
    }
    end_frame(c);
    CHECK(s.count == 0);
    // frame 2: release inside -> the component writes the model
    // directly
    mouse_button(c, false);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        if (u.button(btn, "add", "sv_add_btn"_id)) s.count += 2;
        CHECK(s.view.count == 0);
        // the view snapshot is still stale
    }
    end_frame(c);
    CHECK(s.count == 2);
    // the model changed immediately      // frame 3: update_view
    // frame 3: update_view reflects the new data
    s.update_view();
    CHECK(s.view.count == 2);
    CHECK(std::strcmp(s.view.label, "count 2") == 0);
    // frames 4-5: a second click writes again
    mouse_move(c, 50, 42);
    mouse_button(c, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(btn, "add", "sv_add_btn"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        if (u.button(btn, "add", "sv_add_btn"_id)) s.count += 3;
    }
    end_frame(c);
    s.update_view();
    CHECK(s.count == 5 && s.view.count == 5);
    // any other field can be written the same way
    s.open = true;
    s.update_view();
    CHECK(s.view.open);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_widget_outline)
{
    tf_env env;
    context *c = env.c;
    const rect r = {0, 0, 120, 28};
    // flat by default: just the fill
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "flat", "outline_a"_id);
    }
    end_frame(c);
    const i32 flat_verts = env.nd.vertices;
    CHECK(flat_verts > 0);
    // border_thickness > 0 adds a full outline ring around the
    // fill. Compare     // against an explicitly flat button so the
    // default theme is free to draw one.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "line", "outline_b"_id, static_cast<uiid>(0),
                       button_override{.border_thickness = some(1.0f)});
    }
    end_frame(c);
    const i32 outlined_verts = env.nd.vertices;
    begin_frame(c, 0.024, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "none", "outline_c"_id, static_cast<uiid>(0),
                       button_override{.border_thickness = some(0.0f)});
    }
    end_frame(c);
    const i32 borderless_verts = env.nd.vertices;
    CHECK(outlined_verts > borderless_verts);
    // A fully transparent border is "no border": the fill must stay
    // full-size     // and nothing extra may be drawn, so it
    // matches the borderless button.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(
            r, "clear", "outline_d"_id, static_cast<uiid>(0),
            button_override{.border = some(color{0, 0, 0, 0}), .border_thickness = some(1.0f)});
    }
    end_frame(c);
    // Ring-path extremes must stay valid: a stadium (radius = half
    // the short     // side) and a hairline ring (thickness =
    // radius, so the inner hole is     // degenerate and the arcs
    // skip their inner feather). Both must emit     // vertices and
    // no violations
    // - the ring bands and the arcs shade the same     // 1px
    // feathers, so the junctions cannot step.
    begin_frame(c, 0.040, 0.016, rect::make(0, 0, 400, 100));

    {
        ui u(c);
        (void)u.button(rect::make(10, 10, 120, 30), "stadium", "outline_e"_id, static_cast<uiid>(0),
                       button_override{.bg = some(color{0, 0, 0, 0}),
                                       .border = some(color{90, 160, 255, 255}),
                                       .radius = some(15.0f),
                                       .border_thickness = some(1.5f)});
    }
    end_frame(c);
    CHECK(env.nd.vertices > 0);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 100));

    {
        ui u(c);
        // hairline: thickness = radius -> r_in = 0, inner feather
        // skipped
        (void)u.button(rect::make(240, 10, 60, 24), "thin", "outline_f"_id, static_cast<uiid>(0),
                       button_override{.bg = some(color{0, 0, 0, 0}),
                                       .border = some(color{90, 160, 255, 255}),
                                       .radius = some(6.0f),
                                       .border_thickness = some(5.0f)});
    }
    end_frame(c);
    CHECK(env.nd.vertices > 0);
    // text fields: no outline when idle...
    std::string value = "x";
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    const i32 idle_verts = env.nd.vertices;
    // ...and a focused outline after a click
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    CHECK(c->focus == "outline_field"_id);
    const i32 focused_verts = env.nd.vertices;
    CHECK(focused_verts > idle_verts);
    // outline (+ caret) drawn      // the focus outline is
    // customizable: thickness 0 drops it
    theme t = default_dark();
    t.focus_border_thickness = 0.0f;
    set_theme(c, t);
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    CHECK(env.nd.vertices < focused_verts);
    // no outline
    CHECK(env.nd.vertices >= idle_verts);
    // caret only
}

PUI_TEST(test_popup_focus_trap)
{
    context *c = create_context(nullptr);
    std::string base = "base", pa = "a", pb = "b";
    const rect base_r = {0, 0, 100, 24};
    const rect pa_r = {0, 40, 100, 24};
    const rect pb_r = {0, 70, 100, 24};
    const rect menu = {0, 30, 140, 80};
    // frame 1: base field + popup (input capture starts
    // next frame)
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        (void)u.text_field(base_r, base, "base"_id);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    // frame 2: a press on the base field is blocked (popup
    // captures input)
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        interaction blocked = u.interact("base"_id, base_r);
        CHECK(blocked.blocked);
        CHECK(!blocked.hovered);
        (void)u.text_field(base_r, base, "base"_id);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    CHECK(c->focus != "base"_id);
    // focus popup field A (press + release inside the
    // popup)
    mouse_move(c, 50, 52);
    mouse_button(c, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        // The pointer is inside the popup, but base UI
        // drawn before it stays blocked.
        interaction base_blocked = u.interact("base"_id, base_r);
        CHECK(base_blocked.blocked);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    CHECK(c->focus == "pa"_id);
    // Tab cycles within the popup, never back to the base
    // field
    key_event(c, key::TAB, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "pb"_id);
    CHECK(c->focus != "base"_id);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_widget_extras)
{
    tf_env env;
    context *c = env.c;
    const rect frame = rect::make(0, 0, 200, 100);
    bool checked = false;
    f32 value = 2.0f;
    const rect cb = {0, 0, 120, 24};
    const rect sl = {0, 30, 120, 24};
    // click the checkbox
    begin_frame(c, 0.0, 0.016, frame);

    {
        ui u(c);
        CHECK(!u.checkbox(cb, "On", checked, "cb"_id));
    }
    end_frame(c);
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.016, 0.016, frame);

    {
        ui u(c);
        (void)u.checkbox(cb, "On", checked, "cb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.032, 0.016, frame);

    {
        ui u(c);
        CHECK(u.checkbox(cb, "On", checked, "cb"_id));
    }
    end_frame(c);
    CHECK(checked);
    // slider: press at the middle, drag past the end
    // (clamped to max)
    mouse_move(c, 60, 42);
    mouse_button(c, true);
    begin_frame(c, 0.048, 0.016, frame);

    {
        ui u(c);
        CHECK(u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id));
    }
    end_frame(c);
    CHECK(std::fabs(value - 5.0f) < 0.2f);
    mouse_move(c, 300, 42);
    begin_frame(c, 0.064, 0.016, frame);

    {
        ui u(c);
        (void)u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id);
    }
    end_frame(c);
    CHECK(value == 10.0f);
    mouse_button(c, false);
    begin_frame(c, 0.080, 0.016, frame);

    {
        ui u(c);
        CHECK(!u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id));
    }
    end_frame(c);
    // progress bar emits background + fill
    begin_frame(c, 0.096, 0.016, frame);

    {
        ui u(c);
        u.progress_bar(rect::make(0, 70, 120, 10), 0.5f, color::white(), color{0, 0, 0, 255});
    }
    end_frame(c);
    CHECK(env.nd.vertices >= 8);
}

PUI_TEST(test_scroll_view)
{
    tf_env env;
    context *c = env.c;
    const rect frame = rect::make(0, 0, 200, 150);
    const rect view = rect::make(0, 0, 100, 100);
    f64 t = 0.0;
    auto tick = [&](auto body)
    {
        begin_frame(c, t, 1.0 / 60.0, frame);
        {
            ui u(c);
            body(u);
        }
        end_frame(c);
        t += 1.0 / 60.0;
    };
    // first frame: content height is not known yet, so no overflow / no bar
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(!sv.overflows());
            CHECK(sv.offset() == 0.0f);
            sv.set_content_height(300.0f);
        });
    // wheel over the view scrolls (SDL-style: negative y = down)
    mouse_move(c, 50.0f, 50.0f);
    mouse_wheel(c, 0.0f, -2.0f);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.overflows());
            CHECK(sv.content_height() == 300.0f);
            sv.set_content_height(300.0f);
        });
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() == 60.0f);     // 2 notches * 30 px
            CHECK(sv.content().y == -56.0f); // viewport.y - offset + padding
            // the bar gutter is reserved while overflowing (auto mode)
            const f32 gutter =
                c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
            CHECK(sv.content().w == 100.0f - 8.0f - gutter);
            sv.set_content_height(300.0f);
        });
    // wheel clamps to content height - viewport height
    mouse_wheel(c, 0.0f, -100.0f);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() == 200.0f);
            sv.set_content_height(300.0f);
        });
    // scrollbar thumb drag: track h = 96, thumb h = 32, travel = 64
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.scroll_to(0.0f);
            sv.set_content_height(300.0f);
        });
    mouse_move(c, 94.0f, 8.0f); // on the thumb near its top
    mouse_button(c, true);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    mouse_move(c, 94.0f, 40.0f); // +32 px of travel
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() > 90.0f && sv.offset() < 110.0f);
            sv.set_content_height(300.0f);
        });
    mouse_button(c, false);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    // nested views: the innermost hovered view wins the wheel
    const rect outer_r = rect::make(0, 0, 150, 120);
    const rect inner_r = rect::make(10, 10, 80, 60);
    auto nested = [&](bool check)
    {
        tick(
            [&](ui &u)
            {
                scroll_view outer = u.scroll(outer_r, "outer"_id);
                scroll_view inner = u.scroll(inner_r, "inner"_id);
                if (check)
                {
                    CHECK(inner.offset() == 30.0f);
                    CHECK(outer.offset() == 0.0f);
                }
                inner.set_content_height(300.0f);
                outer.set_content_height(300.0f);
            });
    };
    nested(false);
    mouse_move(c, 50.0f, 40.0f); // inside both viewports
    mouse_wheel(c, 0.0f, -1.0f);
    nested(false);
    nested(true);
    // wheel outside any view is ignored
    mouse_move(c, 190.0f, 140.0f);
    mouse_wheel(c, 0.0f, -1.0f);
    nested(true);
    // content height shrinking back clamps the offset
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() > 0.0f);
            sv.set_content_height(50.0f); // no overflow anymore
            CHECK(sv.offset() == 0.0f);
        });
}

PUI_TEST(test_combo)
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[3] = {"Alpha", "Beta", "Gamma"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 160, 28);
    const uiid id = "cb"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Frame 1+2: press then release over the header opens the dropdown.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 30);
    mouse_button(c, true);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        CHECK(sel == 0);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        // opening is not a change
        CHECK(sel == 0);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    CHECK(popup_present(c, drop_id, false)); // staged at end_frame
    // Frame 3 (open): Down moves the highlight only - the selection
    // commits on Enter, so no change is reported yet.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
    key_event(c, key::DOWN, true);
    key_event(c, key::DOWN, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        CHECK(c->combo_hot == 1);
        CHECK(sel == 0);
    }
    end_frame(c);
    // Frame 4: Enter picks "Beta" (keyboard picks resolve in-frame),
    // reports // the change, and the dropdown closes: the frame stages
    // no popup entry.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
    key_event(c, key::ENTER, true);
    key_event(c, key::ENTER, false);

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 3, sel, id));
        CHECK(sel == 1);
        CHECK(!popup_present(c, drop_id, false));
        // closed, nothing staged     }
        end_frame(c);
        CHECK(!popup_present(c, drop_id, false));
        // closed, nothing staged      // Frame 5: closed - base UI
        // unblocked, and the popup table is clean.
        begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

        {
            ui u(c);
            CHECK(!u.combo(r, "", items, 3, sel, id));
            CHECK(!popup_present(c, drop_id, true));
            // the table followed the close
            const interaction probe = u.interact("base_probe"_id, rect::make(300, 300, 50, 50));
            CHECK(!probe.blocked);
        }
        end_frame(c);
        // Frame 6+7: reopen, then Escape closes it (the Escape frame
        // stages no
        // // entry; the next frame's prev table is clean).
        begin_frame(c, 0.080, 0.016, rect::make(0, 0, 400, 400));
        mouse_button(c, true);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
        }
        end_frame(c);
        begin_frame(c, 0.096, 0.016, rect::make(0, 0, 400, 400));
        mouse_button(c, false);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
        }
        end_frame(c);
        CHECK(popup_present(c, drop_id, false));
        begin_frame(c, 0.112, 0.016, rect::make(0, 0, 400, 400));
        key_event(c, key::ESCAPE, true);
        key_event(c, key::ESCAPE, false);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
            CHECK(!popup_present(c, drop_id, false));
            // closed, nothing staged     }
            end_frame(c);
            key_event(c, key::ESCAPE, false);
            // Out-of-range selected is clamped into range; zero items
            // is a violation.
            // // (Also proves the Escape left no ghost: the prev table
            // is clean here.)
            begin_frame(c, 0.128, 0.016, rect::make(0, 0, 400, 400));
            set_violation_handler(c, capture_violation, nullptr);
            g_violation_events = 0;

            {
                ui u(c);
                CHECK(!popup_present(c, drop_id, true));
                // no ghost entry from the Escape
                i32 sel2 = 7;
                (void)u.combo(r, "", items, 3, sel2, "cb2"_id);
                CHECK(sel2 == 2);
                // clamped into range
                (void)u.combo(r, "", items, 0, sel, "cb3"_id);
                CHECK(g_violation_events == 1);
                CHECK(violation_was("combo needs at least one item"));
            }
            set_violation_handler(c, nullptr, nullptr);
            end_frame(c);
            CHECK(violation_count(c) == 1);
            // the zero-items guard, reported once
            destroy_context(c);
        }
    }
}

PUI_TEST(test_combo_mouse_pick)
{
    tf_env env;
    context *c = env.c;
    static const char *items[3] = {"Alpha", "Beta", "Gamma"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 160, 28);
    const uiid id = "cbp"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Open: press + release on the header.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 30);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    // Press inside the second row (rows lay out inside the scroll
    // viewport: drop.pad(6) + content pad 2 ->
    // row A spans y=58..84, row B 86..112).
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 92.0f);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    // Release over the same row: the pick resolves at end_frame (labels are
    // copied, the pick travels through defer_result), the dropdown retracts
    // the same frame, and the change + model write are reported by the
    // widget's NEXT call.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        // the pick lands later here
    }
    end_frame(c);
    // The pick resolved at end_frame; with the pointer gone, the model write
    // lands together with the NEXT call's changed report.
    CHECK(!popup_present(c, drop_id, false));
    // retracted: no ghost entry
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 3, sel, id));
        // the next call reports it
        CHECK(sel == 1); // written atomically with that report
        CHECK(!popup_present(c, drop_id, true));
        // and no entry lingers
    }
    end_frame(c);
}

PUI_TEST(test_tooltip_delay)
{
    tf_env env;
    context *c = env.c;
    const rect anchor = rect::make(100, 100, 60, 26);
    const uiid id = "tt_btn"_id;
    // Not hovered: no placement, nothing drawn at end_frame.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
    }
    end_frame(c);
    // Hovered at t=0.1: inside the 0.5 s delay - silent.
    begin_frame(c, 0.1, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 120, 110);

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
    }
    end_frame(c);
    // Hovered at t=0.8: past the delay - placed under the anchor and
    // painted at end_frame (vertex delta through the device).
    begin_frame(c, 0.8, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        const rect placed = u.tooltip(anchor, id, "first tip");
        CHECK(placed.w >= 8.0f);
        CHECK(placed.y >= anchor.bottom() + 8.0f);
        // below the anchor
        CHECK(placed.right() <= 400.0f && placed.bottom() <= 400.0f);
    }
    end_frame(c);
    CHECK(env.nd.vertices > 0);
    // Moving to a second widget inside the 1.2 s window: instant (no
    // wait).
    const rect other = rect::make(200, 100, 60, 26);
    const uiid other_id = "tt_btn2"_id;
    begin_frame(c, 0.9, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 220, 110);

    {
        ui u(c);
        (void)u.button(other, "other", other_id);
        CHECK(u.tooltip(other, other_id, "second tip").w >= 8.0f);
    }
    end_frame(c);
    // Unhovered again: silent.
    begin_frame(c, 1.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 350, 350);

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        (void)u.button(other, "other", other_id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
        CHECK(u.tooltip(other, other_id, "second tip").w == 0.0f);
    }
    end_frame(c);
}

PUI_TEST(test_context_menu)
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[2] = {"Open", "Close"};
    const rect anchor = rect::make(40, 40, 120, 60);
    const uiid id = "cm"_id;
    const uiid menu_id = id_child(id, "menu"_id);
    // A right-press *outside* the anchor never opens the menu.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 300, 300);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        (void)u.context_menu(id, anchor, items, 2);
        CHECK(!popup_present(c, menu_id, false));
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);
    // Right-press over the anchor opens the menu at the pointer; the
    // surface stages at end_frame.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 80, 60);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        CHECK(u.context_menu(id, anchor, items, 2) == -1);
        // nothing picked yet     }
        end_frame(c);
        CHECK(popup_present(c, menu_id, false));
        // staged
        mouse_button(c, pointer_button::RIGHT, false);
        // The menu (open last frame) captures input: base UI is
        // blocked, and a
        // // left press inside the first row starts a click on it. (The
        // menu opened // at the pointer: its 6px pad puts the first row
        // at y=66..92, and rows // start 6px right of the menu edge, so
        // x must be past the pad too.)
        begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
        mouse_move(c, 100, 74.0f);
        mouse_button(c, true);

        {
            ui u(c);
            const interaction in = u.interact("base"_id, rect::make(0, 0, 400, 400));
            CHECK(in.blocked);
            CHECK(u.context_menu(id, anchor, items, 2) == -1);
            // press, not release     }
            end_frame(c);
            // Release over the same row: the pick resolves at
            // end_frame, the menu retracts the same frame, and the NEXT
            // call reports it.
            begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
            mouse_button(c, false);

            {
                ui u(c);
                CHECK(u.context_menu(id, anchor, items, 2) == -1);
                // pick lands later     }
                end_frame(c);
                CHECK(!popup_present(c, menu_id, false));
                // retracted: no ghost entry
                begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

                {
                    ui u(c);
                    CHECK(u.context_menu(id, anchor, items, 2) == 0);
                    // "Open" was picked
                    CHECK(!popup_present(c, menu_id, true));
                    // and nothing lingers     }
                    end_frame(c);
                    // Reopened, then Escape closes it in the frame it
                    // rings (nothing staged).
                    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 400, 400));
                    mouse_move(c, 80, 60);
                    mouse_button(c, pointer_button::RIGHT, true);

                    {
                        ui u(c);
                        (void)u.context_menu(id, anchor, items, 2);
                    }
                    end_frame(c);
                    mouse_button(c, pointer_button::RIGHT, false);
                    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 400, 400));
                    key_event(c, key::ESCAPE, true);
                    key_event(c, key::ESCAPE, false);

                    {
                        ui u(c);
                        (void)u.context_menu(id, anchor, items, 2);
                        CHECK(!popup_present(c, menu_id, false));
                        // closed by Escape     }
                        end_frame(c);
                        // One frame later the table is clean (prev
                        // follows the close).
                        begin_frame(c, 0.112, 0.016, rect::make(0, 0, 400, 400));

                        {
                            ui u(c);
                            (void)u.context_menu(id, anchor, items, 2);
                            CHECK(!popup_present(c, menu_id, true));
                            // no ghost     }
                            end_frame(c);
                            CHECK(violation_count(c) == 0);
                            destroy_context(c);
                        }
                    }
                }
            }
        }
    }
}

PUI_TEST(test_scroll_options_and_limits)
{
    tf_env env;
    context *c = env.c;
    const rect view = rect::make(0, 0, 100, 100);
    // SCROLL_ALWAYS_RESERVE_BAR: the gutter is reserved
    // from the first frame,     // even before any content
    // height is known.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv =
            u.scroll(view, "always"_id, scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        const f32 gutter =
            c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
        CHECK(sv.content().w == view.w - 8.0f - gutter);
        CHECK(!sv.overflows());
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv =
            u.scroll(view, "always"_id, scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        const f32 gutter =
            c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
        CHECK(sv.content().w == view.w - 8.0f - gutter);
        // still reserved, no bar
    }
    end_frame(c);
    // scroll_by moves relative to the current offset
    // and clamps. It clamps     // against the store's
    // *last known* content height, so the height is set
    // // before scrolling each frame (first call after
    // a fresh id clamps to 0).
    bool first = true;
    for (i32 i = 0; i < 3; ++i)
    {
        begin_frame(c, 0.5 + 0.016 * i, 0.016, rect::make(0, 0, 200, 200));

        {
            ui u(c);
            scroll_view sv = u.scroll(view, "by"_id);
            sv.set_content_height(300.0f);
            // height first...
            if (first)
            {
                sv.scroll_to(0.0f);
                // ...and start at the top
                // deterministically
                first = false;
            }
            sv.scroll_by(40.0f);
            // relative to the just-clamped offset
            CHECK(sv.offset() == 40.0f * (i + 1));
        }
        end_frame(c);
    }
    begin_frame(c, 1.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "by"_id);
        sv.set_content_height(300.0f);
        CHECK(sv.offset() == 120.0f);
        // 3 frames x 40 px
        sv.scroll_by(5000.0f);
        // way past the end
        CHECK(sv.offset() == 200.0f);
        // clamped to max offset (300 - 100)
        sv.scroll_by(-5000.0f);
        CHECK(sv.offset() == 0.0f);
        // clamped to the top
    }
    end_frame(c);
    // ensure_visible scrolls a rect into view.
    begin_frame(c, 1.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "ev"_id);
        sv.set_content_height(300.0f);
        sv.ensure_visible(rect::make(20, 250, 50, 20));
        // below the fold
    }
    end_frame(c);
    begin_frame(c, 1.032, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "ev"_id);
        // The rect bottom (270) is inside
        // [offset, offset + height -
        // padding].
        CHECK(sv.offset() >= 270.0f - view.h + 4.0f);
        CHECK(sv.offset() <= 270.0f);
    }
    end_frame(c);
}

PUI_TEST(test_virtual_list)
{
    // `scroll_view::virtual_list` submits only the rows that can paint: the
    // visible slice of a uniform grid, clamped to the list. The content
    // height is derived (count * item_height), so the gutter and the scroll
    // extent both follow from the list itself.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    const rect view = rect::make(0, 0, 120, 100);
    constexpr i32 kCount = 1000;
    constexpr f32 kItem = 28.0f;

    // Collected per frame: which indices the callback saw, where, and what
    // content height the view derived.
    std::vector<i32> seen;
    std::vector<rect> rows;
    f32 derived_h = -1.0f;

    auto frame = [&](f32 t)
    {
        seen.clear();
        rows.clear();
        derived_h = -1.0f;
        begin_frame(c, t, 0.016, rect::make(0, 0, 200, 200));
        {
            ui u(c);
            scroll_view sv = u.scroll(view, "vlist"_id);
            sv.virtual_list(kCount, kItem,
                            [&](ui &, i32 index, rect row)
                            {
                                seen.push_back(index);
                                rows.push_back(row);
                            });
            derived_h = sv.content_height();
        }
        end_frame(c);
    };

    // Top of the list: rows 0..4 cover the 100px viewport (28px rows, so the
    // fifth row straddles the bottom edge).
    frame(0.0);
    CHECK(seen.size() == 5);
    CHECK(seen.front() == 0);
    CHECK(rows.front().y == view.y + 4.0f); // viewport.y - 0 + padding
    CHECK(rows.front().h == kItem);
    CHECK(derived_h == kCount * kItem);

    // Scrolled: offset 700 lands inside row 25 (25*28 = 700 exactly), and the
    // slice runs 25..29.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vlist"_id);
        sv.scroll_to(700.0f);
    }
    end_frame(c);
    frame(0.032);
    CHECK(seen.size() == 5);
    CHECK(seen.front() == 25);
    CHECK(seen.back() == 29);
    CHECK(rows.front().y == view.y - 700.0f + 4.0f + 25.0f * kItem);

    // Near the end: the slice clamps to the last item, never past it.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vlist"_id);
        sv.scroll_to(kCount * kItem - view.h); // max offset: 27900
    }
    end_frame(c);
    frame(0.064);
    CHECK(seen.back() == kCount - 1);
    CHECK(rows.back().y + rows.back().h ==
          view.y - (kCount * kItem - view.h) + 4.0f + kCount * kItem);
    CHECK(derived_h == kCount * kItem);

    // Empty and degenerate lists report no rows and derive no height.
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vempty"_id);
        i32 calls = 0;
        sv.virtual_list(0, kItem, [&](ui &, i32, rect) { ++calls; });
        CHECK(calls == 0);
        CHECK(sv.content_height() == 0.0f);
    }
    end_frame(c);

    // A non-positive item height is a reported violation, not a divide by
    // zero (captured by the handler above).
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vbad"_id);
        sv.virtual_list(10, 0.0f, [](ui &, i32, rect) {});
    }
    end_frame(c);
    CHECK(g_violation_events == 1);
    CHECK(violation_was("virtual_list item height must be positive"));

    CHECK(violation_count(c) == 1);
    destroy_context(c);
}

PUI_TEST(test_tooltip_flip_above)
{
    null_device nd;
    nd.my_surface.w = 400;
    nd.my_surface.h = 120;
    context *c = create_context(&nd, nd.create_surface());
    const rect anchor = rect::make(150, 80, 60, 26);
    // near the bottom edge
    const uiid id = "tt_flip"_id;
    // Warm the delay: t=0.1 waits, t=0.7 draws.
    begin_frame(c, 0.1, 0.016, rect::make(0, 0, 400, 120));
    mouse_move(c, 170, 90);

    {
        ui u(c);
        (void)u.button(anchor, "low", id);
        CHECK(u.tooltip(anchor, id, "flip me").w == 0.0f);
        // waiting     }
        end_frame(c);
        begin_frame(c, 0.7, 0.016, rect::make(0, 0, 400, 120));

        {
            ui u(c);
            (void)u.button(anchor, "low", id);
            const rect placed = u.tooltip(anchor, id, "flip me");
            // Below the anchor would fall off the 120px screen: flipped
            // above.
            CHECK(placed.w >= 8.0f);
            CHECK(placed.bottom() <= 120.0f);
            CHECK(placed.bottom() < anchor.y);
            // above the anchor
            CHECK(placed.y >= 4.0f);
            // still inside the screen     }
            end_frame(c);
            // Right-edge anchor: the x clamp keeps the tooltip inside.
            const rect right_anchor = rect::make(360, 30, 30, 24);
            const uiid rid = "tt_flip_r"_id;
            begin_frame(c, 0.9, 0.016, rect::make(0, 0, 400, 120));
            // within 1.2s: instant
            mouse_move(c, 370, 40);

            {
                ui u(c);
                (void)u.button(right_anchor, "r", rid);
                const rect placed = u.tooltip(right_anchor, rid, "a tip");
                CHECK(placed.w >= 8.0f);
                CHECK(placed.right() <= 400.0f);
                // clamped inside the right edge     }
                end_frame(c);
                CHECK(violation_count(c) == 0);
                destroy_context(c);
            }
        }
    }
}

PUI_TEST(test_combo_popup_clamp)
{
    null_device nd;
    nd.my_surface.w = 240;
    nd.my_surface.h = 300;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[8] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    i32 sel = 0;
    // A combo near the right/bottom
    // edge: the dropdown clamps
    // into the screen.
    const rect r = rect::make(160, 260, 120, 26);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 240, 300));
    mouse_move(c, 180, 270);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 8, sel, "cbcl"_id, 300.0f);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 8, sel, "cbcl"_id, 300.0f);
    }
    end_frame(c);
    // The dropdown is staged; its
    // rect must sit inside the
    // screen.
    {
        bool checked = false;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].id != id_child("cbcl"_id, "dropdown"_id)) continue;
            const rect &drop = c->popups[i].area;
            CHECK(drop.right() <= 240.0f + 0.01f);
            // inside the right edge
            CHECK(drop.bottom() <= 300.0f + 0.01f);
            // inside the bottom
            // edge
            CHECK(drop.h <= 8 * 26.0f + 2.0f * 7.0f + 12.0f + 0.01f);
            checked = true;
        }
        CHECK(checked);
    }
    // max_popup_height clamps a
    // tall list even when the
    // screen has room. The     //
    // press that starts this click
    // first lands while combo 1's
    // dropdown is     // open: it
    // closes combo 1 (click
    // outside) and must not open
    // combo 2 with     // the same
    // press, so combo 2 needs its
    // own press+release afterwards.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 240, 300));
    mouse_move(c, 40, 20);
    mouse_button(c, true);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
    }
    end_frame(c);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, false);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
    }
    end_frame(c);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, true);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
        CHECK(c->popup_depth == 0);
        // the press outside combo 1
        // must not open this     }
        end_frame(c);
        begin_frame(c, 0.080, 0.016, rect::make(0, 0, 240, 300));
        mouse_button(c, false);

        {
            ui u(c);
            i32 sel2 = 0;
            (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
        }
        end_frame(c);

        {
            bool checked = false;
            for (i32 i = 0; i < c->popup_depth; ++i)
            {
                if (c->popups[i].id != id_child("cbcl2"_id, "dropdown"_id)) continue;
                // list = 8*26 + 7*2
                // = 222; + pad =
                // 234 -> clamped
                // to 90.
                CHECK(c->popups[i].area.h == 90.0f);
                checked = true;
            }
            CHECK(checked);
        }
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

PUI_TEST(test_popup_blocks_underlying)
{
    context *c = create_context(nullptr);
    const rect menu = {50, 50, 100, 100};
    // frame 1: open the popup so the next frame captures input
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 300));

    {
        ui u(c);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
    }
    end_frame(c);
    // frame 2: a button whose rect overlaps the popup must NOT respond
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 100, 100);
    // inside both the popup and the underlying widget
    mouse_button(c, true);

    {
        ui u(c);
        interaction under = u.interact("under"_id, rect{0, 0, 300, 300});
        CHECK(!under.hovered);
        CHECK(c->active == 0);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        interaction item = u.interact("item"_id, rect{60, 60, 80, 60});
        CHECK(item.hovered);
        // popup content still works
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_panel_card)
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        rect pb = {50, 50, 200, 120};

        {
            panel_scope p = u.panel("Test Panel", pb, {.id = "test_panel"_id});
            CHECK(p.open);
            CHECK(!p.close_requested);
            CHECK(p.content().w > 0.0f);
            CHECK(c->clip_depth == 1); // client region pushed a clip
        }
        // Per-panel overrides merge over the theme's panel style; a translucent
        // background is what lets a blurred backdrop show through.
        {
            const panel_style merged =
                merge_style(default_dark().panel, panel_override{.bg = some(color{10, 20, 30, 120}),
                                                                 .radius = some(9.0f)});
            CHECK(merged.bg.a == 120);
            CHECK(merged.radius == 9.0f);
            CHECK(default_dark().panel.bg.a != 120);
            // the theme itself is unchanged
        }
        {
            rect pb2 = {270, 50, 110, 90};
            panel_scope p = u.panel("Glass", pb2, PANEL_NO_CONTROLS, "glass_panel"_id, nullptr,
                                    panel_override{.bg = some(color{10, 20, 30, 120})});
            CHECK(p.open);
            CHECK(p.content().w > 0.0f);
        }
        u.card({0, 0, 100, 60}, card_override{.radius = some(0.0f)});
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_panel_blocks_underlying)
{
    context *c = create_context(nullptr);
    const rect pb = {50, 50, 200, 120};
    // frame 1: draw the panel so next frame it participates in input
    // blocking
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        rect b = pb;
        panel_scope p = u.panel("P", b, {.id = "p"_id});
    }
    end_frame(c);
    // frame 2: a widget under the panel must NOT respond; panel content
    // must
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, 100, 100);
    // inside the panel
    mouse_button(c, true);

    {
        ui u(c);
        interaction under = u.interact("under"_id, rect{0, 0, 400, 300});
        CHECK(!under.hovered);
        CHECK(c->active == 0);
        rect b = pb;
        panel_scope p = u.panel("P", b, {.id = "p"_id});
        interaction inside = u.interact("inside"_id, rect{60, 90, 120, 40});
        CHECK(inside.hovered);
        // panel content is allowed
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_panel_close_button)
{
    context *c = create_context(nullptr);
    rect b = {50, 50, 200, 120};
    // close dot center with the default panel style:
    // inner = bounds.pad(1) -> tb.right() = 249, tb.y = 51;
    // close 12x12 at (227, 60)
    const f32 cx = 233.0f, cy = 66.0f;
    bool close_req = false;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, cx, cy);
    mouse_button(c, true);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, {.id = "p"_id});
        close_req = p.close_requested;
    }
    end_frame(c);
    CHECK(!close_req);
    CHECK(c->dragging_panel == 0);
    // pressing close must not start a titlebar drag
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 300));
    mouse_button(c, false);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, {.id = "p"_id});
        close_req = p.close_requested;
    }
    end_frame(c);
    CHECK(close_req);
    // release over the dot closes the panel. The titlebar keeps its full
    // width: pressing just left of the dot must still start a drag.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, 209, 66);
    mouse_button(c, true);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, {.id = "p"_id});
        (void)p;
    }
    end_frame(c);
    CHECK(c->dragging_panel != 0);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 300));
    mouse_button(c, false);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, {.id = "p"_id});
        (void)p;
    }
    end_frame(c);
    CHECK(c->dragging_panel == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_context_menu_clamp)
{
    null_device nd;
    nd.my_surface.w = 200;
    nd.my_surface.h = 120;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[3] = {"Cut", "Copy", "Paste"};
    const rect anchor = rect::make(150, 90, 40, 24);
    // bottom-right corner //
    // Right-press at the very
    // corner: the clamped menu
    // must stay inside.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 120));
    mouse_move(c, 190.0f, 110.0f);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        (void)u.context_menu("cmc"_id, anchor, items, 3);
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);

    {
        bool checked = false;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].id != id_child("cmc"_id, "menu"_id)) continue;
            const rect &menu = c->popups[i].area;
            CHECK(menu.right() <= 200.0f + 0.01f);
            CHECK(menu.bottom() <= 120.0f + 0.01f);
            CHECK(menu.x >= 0.0f && menu.y >= 0.0f);
            checked = true;
        }
        CHECK(checked);
    }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_popup_retract_no_ghost)
{
    // End-to-end ghost-entry
    // check for the same-frame
    // close path: pick a combo item, then verify the next frame sees no popup
    // entry (the same-frame close retracts it; this asserts the observable
    // result).
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[2] = {"A", "B"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 100, 26);
    const uiid id = "ghost_cb"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Open: press + release on
    // the header.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 40, 32);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    // dropdown staged      //
    // Press inside row A (rows
    // at y=58..84 inside the
    // scroll viewport).
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 40, 62.0f);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    // Release: the pick
    // resolves at end_frame and
    // the dropdown retracts.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 300, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    CHECK(sel == 0);
    CHECK(!popup_present(c, drop_id, false));
    // retracted: no ghost entry
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 300));

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 2, sel, id));
        // the next call reports
        // it         // A probe
        // far from the closed
        // dropdown is an
        // ordinary base hit
        // now.
        const interaction in = u.interact("base"_id, rect::make(240, 240, 40, 40));
        CHECK(!in.blocked);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_checkbox_slider_edge)
{
    tf_env env;
    context *c = env.c;
    // checkbox: disabled never
    // hovers; a click (press +
    // release over the box) //
    // toggles even with an
    // empty label.
    bool v = true;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 120));
    mouse_move(c, 10, 40);
    // over the box (y 30..56)
    mouse_button(c, true);

    {
        ui u(c);
        const interaction dis = u.interact("disabled_probe"_id, rect::make(0, 0, 40, 26), false);
        CHECK(dis.disabled && !dis.hovered);
        (void)u.checkbox(rect::make(0, 30, 80, 26), "", v, "cb_empty"_id);
        CHECK(v);
        // press alone does not
        // toggle
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 120));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.checkbox(rect::make(0, 30, 80, 26), "", v, "cb_empty"_id);
        CHECK(!v);
        // the release over
        // the box flips it
        //
    }
    end_frame(c);
    // slider: a fast
    // click (press +
    // release inside
    // one frame) still
    // jumps to     //
    // the click
    // position - the
    // press edge drives
    // the jump, not
    // `active`.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 120));
    f32 val = 5.0f;
    mouse_move(c, 3.0f, 70.0f);
    // far left, inside
    // the track
    // (y 60..86)
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.slider_float(rect::make(0, 60, 120, 26), "", val, 1.0f, 9.0f, "sl_min"_id);
        CHECK(val <= 1.0f + 0.35f);
        // jumped to the
        // click
        // position this
        // frame
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 120));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.slider_float(rect::make(0, 60, 120, 26), "", val, 1.0f, 9.0f, "sl_min"_id);
        CHECK(val <= 1.0f + 0.35f);
        // and it
        // stays
        // after the
        // release
    }
    end_frame(c);
}

namespace
{
// Reports whether any vertex of `want` color lies deeper than 4px inside `box`:
// a filled shape has such vertices, a 1px ring hugging the edge does not.
struct fill_probe_device : null_device
{
    color want{};
    rect box{};
    bool filled = false;
    void draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        const rect deep = box.pad(4.0f);
        for (i32 i = 0; i < vc; ++i)
            if (v[i].c.r == want.r && v[i].c.g == want.g && v[i].c.b == want.b &&
                v[i].c.a == want.a && deep.contains(v[i].x, v[i].y))
                filled = true;
        null_device::draw(tex, v, vc, idx, ic);
    }
};
} // namespace

PUI_TEST(test_text_field_translucent_outline)
{
    // A translucent field background (glass themes) must not show the border
    // color through it: the outline is a ring over the fill, not a fill under it.
    fill_probe_device dev;
    dev.want = {1, 2, 3, 255};
    dev.box = rect::make(20, 20, 200, 40);
    context *c = create_context(&dev, dev.create_surface());
    theme t = default_dark();
    t.border = dev.want;
    t.border_thickness = 1.0f;
    std::string value;
    auto frame = [&](f64 now)
    {
        begin_frame(c, now, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)u.text_field(dev.box, value, "tf"_id);
        }
        end_frame(c);
    };

    t.widget_bg = {255, 255, 255, 30};
    set_theme(c, t);
    frame(0.0);
    CHECK(!dev.filled); // translucent: ring only

    t.widget_bg = {30, 34, 42, 255};
    set_theme(c, t);
    frame(0.016);
    CHECK(dev.filled); // opaque: the classic two-rect outline still fills with the border color
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

// A panel's identity is explicit: the title is never one.
PUI_TEST(test_panel_needs_explicit_id)
{
    tf_env env;
    env.expect_clean = false;
    rect b = rect::make(10, 10, 120, 90);
    env.frame(0.0, [&](ui &u) { panel_scope p = u.panel("Same title", b, {.id = "pa"_id}); });
    CHECK(violation_count(env.c) == 0);

    // two panels with one title but different ids are two panels
    rect b1 = rect::make(10, 10, 100, 80), b2 = rect::make(150, 10, 100, 80);
    env.frame(0.016,
              [&](ui &u)
              {
                  {
                      panel_scope p = u.panel("Same title", b1, {.id = "pa"_id});
                  }
                  {
                      panel_scope p = u.panel("Same title", b2, {.id = "pb"_id});
                  }
              });
    CHECK(violation_count(env.c) == 0);
    CHECK(env.c->panel_depth == 2);
    CHECK(env.c->panels[0].id == "pa"_id && env.c->panels[1].id == "pb"_id);

    // no id (and no dock identity): reported
    env.frame(0.032, [&](ui &u) { panel_scope p = u.panel("Anon", b, PANEL_NONE); });
    CHECK(violation_last_code(env.c) == VIOL_PANEL_NO_ID);
}
