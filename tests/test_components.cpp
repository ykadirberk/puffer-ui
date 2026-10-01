// components: the pui::comp toggle library (r91 split).
// Each component: behavior + keyboard + zero violations.
#include "test_util.h"

void test_switch_toggle()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    bool on = false;
    const rect sw_r = rect::make(10, 10, 160, 24);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)comp::switch_toggle(u, sw_r, "Wi-Fi", on, {.id = "sw"_id});
        }
        end_frame(c);
    };

    // click toggles
    frame(0.0);
    mouse_move(c, 30.0f, 22.0f);
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(on);

    // a f32 wheel... no: a second click toggles back
    mouse_button(c, true);
    frame(0.048);
    mouse_button(c, false);
    frame(0.064);
    CHECK(!on);

    // keyboard: tab to it, Space toggles
    key_event(c, key::TAB, true);
    frame(0.08);
    key_event(c, key::TAB, false);
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)comp::switch_toggle(u, sw_r, "Wi-Fi", on, {.id = "sw"_id});
        CHECK(u.ctx->focus == "sw"_id); // the switch is in the Tab ring
    }
    end_frame(c);
    key_event(c, key::SPACE, true);
    frame(0.112);
    key_event(c, key::SPACE, false);
    CHECK(on); // Space toggled it

    // size helper: track + gap + label
    begin_frame(c, 0.128, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        const vec2 sz = comp::switch_size(u, "Wi-Fi");
        CHECK(sz.x > u.th().switch_ctrl.width);
        CHECK(sz.y == u.th().switch_ctrl.height);
    }
    end_frame(c);

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

void test_radio_group()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    static const char *const items[] = {"Low", "Medium", "High"};
    i32 selected = 1;
    const rect group = rect::make(10, 10, 160, 90);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)comp::radio_group(u, group, items, selected, {.id = "radio"_id});
        }
        end_frame(c);
    };

    // click the third row (control_h = 28 by default: rows at 10, 38, 66)
    frame(0.0);
    mouse_move(c, 30.0f, 74.0f);
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(selected == 2);

    // keyboard: Tab to the first row, Space selects it
    key_event(c, key::TAB, true);
    frame(0.048);
    key_event(c, key::TAB, false);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)comp::radio_group(u, group, items, selected, {.id = "radio"_id});
        CHECK(u.ctx->focus == id_child("radio"_id, 0)); // row 0 is in the ring
    }
    end_frame(c);
    key_event(c, key::ENTER, true);
    frame(0.080);
    key_event(c, key::ENTER, false);
    CHECK(selected == 0);

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

void test_segmented()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    static const char *const items[] = {"Day", "Week", "Month"};
    i32 selected = 0;
    const rect seg = rect::make(10, 10, 180, 28);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)comp::segmented(u, seg, items, selected, {.id = "seg"_id});
        }
        end_frame(c);
    };

    // click the middle segment (each 180/3 = 60 wide: 10..70..130..190)
    frame(0.0);
    mouse_move(c, 100.0f, 24.0f);
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(selected == 1);

    // keyboard: Tab once reaches segment 0, Space selects it
    key_event(c, key::TAB, true);
    frame(0.048);
    key_event(c, key::TAB, false);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)comp::segmented(u, seg, items, selected, {.id = "seg"_id});
        CHECK(u.ctx->focus == id_child("seg"_id, 0));
    }
    end_frame(c);
    key_event(c, key::SPACE, true);
    frame(0.080);
    key_event(c, key::SPACE, false);
    CHECK(selected == 0);

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

void test_tab_bar()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    if (tf_needs_font(c)) // tab widths need a font; skip without the bundled one
    {
        destroy_context(c);
        return;
    }

    static const char *const items[] = {"Files", "Search", "Settings"};
    i32 active = 0;
    rect bar = rect::make(10, 10, 240, 30);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)comp::tab_bar(u, bar, items, active, {.id = "tabs"_id});
        }
        end_frame(c);
    };

    frame(0.0);
    // click the second tab (with the font loaded, widths are real)
    mouse_move(c, 90.0f, 25.0f);
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(active == 1);

    // keyboard: Tab reaches tab 0, Enter activates it
    key_event(c, key::TAB, true);
    frame(0.048);
    key_event(c, key::TAB, false);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)comp::tab_bar(u, bar, items, active, {.id = "tabs"_id});
    }
    end_frame(c);
    key_event(c, key::ENTER, true);
    frame(0.080);
    key_event(c, key::ENTER, false);
    CHECK(active == 0);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_accordion()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    bool open = false;
    const rect area = rect::make(10, 10, 200, 100);
    f32 last_content_h = -1.0f;

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            comp::accordion_scope acc(u, area, "Advanced", open,
                                      {.id = "acc"_id, .content_h = 60.0f});
            if (acc) u.draw_rect(acc.content(), color{40, 80, 60, 255});
            last_content_h = acc.content().h;
        }
        end_frame(c);
    };

    frame(0.0);
    CHECK(!open);
    CHECK(last_content_h == 0.0f);

    // click the header toggles it open; the content height animates up
    mouse_move(c, 60.0f, 22.0f);
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(open);
    frame(0.048);                   // one frame for the tween to advance
    CHECK(last_content_h > 0.0f);   // mid-animation
    CHECK(last_content_h <= 60.0f); // never overshoots

    for (i32 i = 0; i < 20; ++i) frame(0.064 + 0.016 * i);
    CHECK(last_content_h > 59.0f); // settled at the content height

    // keyboard: the header is in the ring; Space closes
    key_event(c, key::TAB, true);
    frame(0.4);
    key_event(c, key::TAB, false);
    key_event(c, key::SPACE, true);
    frame(0.416);
    key_event(c, key::SPACE, false);
    CHECK(!open);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_drawer()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    bool open = true;
    const rect host = rect::make(0, 0, 300, 200);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, host);
        {
            ui u(c);
            comp::drawer_scope dr(u, host, open, {.id = "drw"_id, .width = 200.0f});
            if (dr) u.text(dr.content(), "drawer body", u.th().text, ALIGN_LEFT);
        }
        end_frame(c);
    };

    begin_frame(c, 0.0, 0.016, host);
    {
        ui u(c);
        comp::drawer_scope dr(u, host, open, {.id = "drw"_id, .width = 200.0f});
        CHECK(dr.content().right() <= host.right());
    }
    end_frame(c);

    mouse_move(c, 280.0f, 100.0f); // right of the drawer: on the scrim
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(!open); // the scrim click closed it

    // the scrim is never in the Tab ring
    open = true;
    frame(0.048);
    key_event(c, key::TAB, true);
    frame(0.064);
    key_event(c, key::TAB, false);
    CHECK(c->focus != id_child("drw"_id, "scrim"_id));

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_toast_host()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    comp::toast_host host;
    const rect anchor = rect::make(0, 0, 300, 200);

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, anchor);
        {
            ui u(c);
            comp::toast_draw(u, anchor, host, {.id = "toasts"_id});
        }
        end_frame(c);
    };

    host.push("Saved", 1);
    host.push("Disconnected", 2);
    CHECK(host.alive() == 2);
    frame(0.0); // stamps `born`
    frame(0.5);
    CHECK(host.alive() == 2); // still within the lifetime
    frame(4.0);               // past the 3.5 s lifetime
    CHECK(host.alive() == 0); // aged out

    // the ring overflows by dropping the oldest
    for (i32 i = 0; i < comp::toast_host::MAX_TOASTS + 3; ++i) host.push("t", 0);
    CHECK(host.alive() == comp::toast_host::MAX_TOASTS);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_table()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    static const char *const headers[] = {"Name", "Size", "Kind"};
    const rect area = rect::make(10, 10, 240, 100);

    struct env
    {
        context *c;
        i32 last_row = -1, last_col = -1;
    } e{c, -1, -1};
    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            comp::table_result res =
                comp::table(u, area, headers, 100,
                            [](ui &uu, rect cell, i32 row, i32 col)
                            {
                                char label[32];
                                std::snprintf(label, sizeof(label), "r%d c%d", row, col);
                                uu.text(cell, label, uu.th().text, ALIGN_LEFT);
                            },
                            {.id = "table"_id});
            if (res.clicked_row >= 0)
            {
                e.last_row = res.clicked_row;
                e.last_col = res.clicked_col;
            }
        }
        end_frame(c);
    };

    frame(0.0);
    // click the first row, third column (header 28 tall; row 0 at ~38; col width = 80)
    mouse_move(c, 10.0f + 200.0f, 42.0f); // x within col 2 (160..240)
    mouse_button(c, true);
    frame(0.016);
    mouse_button(c, false);
    frame(0.032);
    CHECK(e.last_row == 0);
    CHECK(e.last_col == 2);

    // keyboard: Tab reaches row 0, Enter activates it
    key_event(c, key::TAB, true);
    frame(0.048);
    key_event(c, key::TAB, false);
    key_event(c, key::ENTER, true);
    frame(0.064);
    key_event(c, key::ENTER, false);
    CHECK(e.last_row == 0);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_command_palette()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    if (tf_needs_font(c))
    {
        destroy_context(c);
        return;
    }

    static const comp::palette_command cmds[] = {
        {"Open file", "Ctrl+O"}, {"Save file", "Ctrl+S"}, {"Close window", ""}};
    bool open = true;
    comp::palette_state st;
    const rect screen = rect::make(0, 0, 300, 200);
    i32 chosen = -1;
    i32 shown = -1;

    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, screen);
        {
            ui u(c);
            comp::palette_result r = comp::command_palette(
                u, screen, open, st, std::span<const comp::palette_command>(cmds),
                {.id = "pal"_id});
            if (r.chosen >= 0) chosen = r.chosen;
            shown = r.shown;
        }
        end_frame(c);
    };

    frame(0.0);
    frame(0.016); // the query field takes focus
    CHECK(shown == 3);
    CHECK(c->focus == id_child("pal"_id, "query"_id)); // the field owns focus

    // Down moves the highlight; Enter chooses that command
    key_event(c, key::DOWN, true);
    frame(0.032);
    key_event(c, key::DOWN, false);
    CHECK(st.active == 1);
    key_event(c, key::ENTER, true);
    frame(0.048);
    key_event(c, key::ENTER, false);
    CHECK(chosen == 1); // "Save file"
    CHECK(!open);

    // filtering: reopen, type "win" -> "Close window" is the only match
    open = true;
    st.query.clear();
    st.active = 0;
    frame(0.064);
    frame(0.080);
    text_input_event(c, "win");
    frame(0.096);
    CHECK(shown == 1);
    key_event(c, key::ENTER, true);
    frame(0.112);
    key_event(c, key::ENTER, false);
    CHECK(chosen == 2); // "Close window"

    // Escape closes
    open = true;
    st.query.clear();
    frame(0.128);
    key_event(c, key::ESCAPE, true);
    frame(0.144);
    key_event(c, key::ESCAPE, false);
    CHECK(!open);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_theme_tokens()
{
    // tokens ride along the theme and interpolate
    theme a = default_dark();
    theme b = a;
    b.bg = {255, 255, 255, 255};
    b.tokens.primary = {255, 0, 0, 255};
    const theme mid = theme_lerp(a, b, 0.5f);
    CHECK(mid.bg.r > a.bg.r && mid.bg.r < b.bg.r);
    CHECK(mid.tokens.primary.r > a.tokens.primary.r);
    CHECK(mid.tokens.primary.b < a.tokens.primary.b);
    CHECK(a.tokens.primary.r == a.accent.r); // default_dark wires them
}