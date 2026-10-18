// components: the pui::comp toggle library.
// Each component: behavior + keyboard + zero violations.
#include "test_util.h"

PUI_TEST(test_switch_toggle)
{
    tf_env env;
    context *c = env.c;

    bool on = false;
    const rect sw_r = rect::make(10, 10, 160, 24);

    auto frame = [&](f64 t)
    {
        env.frame(t,
                  [&](ui &u) { (void)comp::switch_toggle(u, sw_r, "Wi-Fi", on, {.id = "sw"_id}); });
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
}

PUI_TEST(test_radio_group)
{
    tf_env env;
    context *c = env.c;

    static const char *const items[] = {"Low", "Medium", "High"};
    i32 selected = 1;
    const rect group = rect::make(10, 10, 160, 90);

    auto frame = [&](f64 t)
    {
        env.frame(t, [&](ui &u)
                  { (void)comp::radio_group(u, group, items, selected, {.id = "radio"_id}); });
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
}

PUI_TEST(test_segmented)
{
    tf_env env;
    context *c = env.c;

    static const char *const items[] = {"Day", "Week", "Month"};
    i32 selected = 0;
    const rect seg = rect::make(10, 10, 180, 28);

    auto frame = [&](f64 t)
    {
        env.frame(t,
                  [&](ui &u) { (void)comp::segmented(u, seg, items, selected, {.id = "seg"_id}); });
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
}

PUI_TEST(test_tab_bar)
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

PUI_TEST(test_accordion)
{
    tf_env env;
    context *c = env.c;

    bool open = false;
    const rect area = rect::make(10, 10, 200, 100);
    f32 last_content_h = -1.0f;

    auto frame = [&](f64 t)
    {
        env.frame(t,
                  [&](ui &u)
                  {
                      comp::accordion_scope acc(u, area, "Advanced", open,
                                                {.id = "acc"_id, .content_h = 60.0f});
                      if (acc) u.draw_rect(acc.content(), color{40, 80, 60, 255});
                      last_content_h = acc.content().h;
                  });
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
}

// Widgets inside an open accordion can be hovered and clicked: the content is
// clipped to the content area (not to the header).
PUI_TEST(test_accordion_content_interactive)
{
    tf_env env;
    context *c = env.c;

    bool open = true;
    const rect area = rect::make(10, 10, 200, 100); // header 30 + content 60
    i32 clicks = 0;
    rect clip_seen{};

    auto frame = [&](f64 t)
    {
        env.frame(t,
                  [&](ui &u)
                  {
                      comp::accordion_scope acc(u, area, "Advanced", open,
                                                {.id = "acc"_id, .content_h = 60.0f});
                      if (acc)
                      {
                          clip_seen = c->clip_stack[c->clip_depth - 1];
                          const rect b = rect::make(acc.content().x + 10.0f,
                                                    acc.content().y + 10.0f, 80.0f, 24.0f);
                          if (u.button(b, "Inside", "acc_btn"_id)) ++clicks;
                      }
                  });
    };

    for (i32 i = 0; i < 20; ++i) frame(0.016 * i); // settle open
    CHECK(clip_seen.h > 59.0f);                    // the clip is the content, not empty
    CHECK(clip_seen.y >= 40.0f);                   // ... and below the header

    mouse_move(c, 50.0f, 60.0f); // over the button inside the content
    mouse_button(c, true);
    frame(0.4);
    mouse_button(c, false);
    frame(0.416);
    CHECK(clicks == 1);
}

PUI_TEST(test_drawer)
{
    tf_env env;
    context *c = env.c;

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
}

PUI_TEST(test_toast_host)
{
    tf_env env;
    context *c = env.c;

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
}

PUI_TEST(test_table)
{
    tf_env env;
    context *c = env.c;

    static const char *const headers[] = {"Name", "Size", "Kind"};
    const rect area = rect::make(10, 10, 240, 100);

    struct env
    {
        context *c;
        i32 last_row = -1, last_col = -1;
    } e{c, -1, -1};
    auto frame = [&](f64 t)
    {
        env.frame(t,
                  [&](ui &u)
                  {
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
                  });
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
}

PUI_TEST(test_command_palette)
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

// A resting pointer leaves the palette list still: hovering a row highlights it
// but never scrolls; only keyboard moves (minimally) and the wheel scroll it.
PUI_TEST(test_command_palette_hover_is_stable)
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

    static char names[20][16];
    static comp::palette_command cmds[20];
    for (i32 i = 0; i < 20; ++i)
    {
        std::snprintf(names[i], sizeof(names[i]), "Command %02d", i);
        cmds[i] = {names[i], ""};
    }
    bool open = true;
    comp::palette_state st;
    const rect screen = rect::make(0, 0, 600, 600);
    i32 active = -1;
    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, screen);
        {
            ui u(c);
            active = comp::command_palette(u, screen, open, st,
                                           std::span<const comp::palette_command>(cmds),
                                           {.id = "pal"_id})
                         .active;
        }
        end_frame(c);
    };
    frame(0.0);
    frame(0.016);

    // the panel's rows (the same layout the component cuts)
    const theme &th = c->active_theme;
    const f32 items_y = screen.h * 0.18f + th.padding * 0.75f + th.control_h +
                        6.0f; // panel top + pad + field + gap
    const f32 row_h = th.palette.item_h;
    mouse_move(c, 300.0f, items_y + row_h * 7.5f); // the 8th (last visible) row
    f64 t = 0.032;
    for (i32 i = 0; i < 8; ++i, t += 0.016)
    {
        frame(t);
        CHECK(active == 7);   // the pointer's row, every frame
        CHECK(st.first == 0); // and the list never moved under it
    }

    // the keyboard scrolls just enough to keep the highlight visible
    mouse_move(c, 5.0f, 5.0f); // off the panel (the scrim)
    key_event(c, key::DOWN, true);
    frame(t);
    t += 0.016;
    key_event(c, key::DOWN, false);
    CHECK(st.active == 8);
    CHECK(st.first == 1);

    // the wheel scrolls the list (and clamps at the end)
    mouse_move(c, 300.0f, items_y + row_h * 2.5f);
    mouse_wheel(c, 0.0f, -40.0f);
    frame(t);
    CHECK(st.first == 20 - 8);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_theme_tokens)
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

PUI_TEST(test_theme_lerp_slots)
{
    // every component slot interpolates, not just the top-level palette
    theme a = default_dark();
    theme b = a;
    b.switch_ctrl.track_on = {255, 0, 0, 255};
    b.tabs.underline = {0, 255, 0, 255};
    b.palette.accent = {0, 0, 255, 255};
    b.button.bg = {255, 255, 255, 255};
    b.panel.radius = a.panel.radius + 10.0f;
    b.table.row_h = a.table.row_h + 20.0f;
    b.toast.width = a.toast.width + 100.0f;
    b.menu.bg = {255, 255, 255, 255};
    b.menu.blur = a.menu.blur + 20.0f;
    b.titlebar.height = a.titlebar.height + 10.0f;
    b.text_gamma = a.text_gamma + 1.0f;
    a.titlebar.bg = some(color{0, 0, 0, 255});
    b.titlebar.bg = some(color{200, 0, 0, 255});

    const theme mid = theme_lerp(a, b, 0.5f);
    CHECK(mid.titlebar.height > a.titlebar.height && mid.titlebar.height < b.titlebar.height);
    CHECK(mid.text_gamma > a.text_gamma && mid.text_gamma < b.text_gamma);
    CHECK(mid.titlebar.bg.set && mid.titlebar.bg.value.r > 0 && mid.titlebar.bg.value.r < 200);
    CHECK(mid.switch_ctrl.track_on.r > a.switch_ctrl.track_on.r &&
          mid.switch_ctrl.track_on.r < 255);
    CHECK(mid.tabs.underline.g > a.tabs.underline.g && mid.tabs.underline.g < 255);
    CHECK(mid.palette.accent.b > 0 && mid.palette.accent.b < a.palette.accent.b + 1);
    CHECK(mid.button.bg.r > a.button.bg.r && mid.button.bg.r < 255);
    CHECK(mid.panel.radius > a.panel.radius && mid.panel.radius < b.panel.radius);
    CHECK(mid.table.row_h > a.table.row_h && mid.table.row_h < b.table.row_h);
    CHECK(mid.toast.width > a.toast.width && mid.toast.width < b.toast.width);
    CHECK(mid.menu.bg.r > a.menu.bg.r && mid.menu.bg.r < 255);
    CHECK(mid.menu.blur > a.menu.blur && mid.menu.blur < b.menu.blur);

    const theme end = theme_lerp(a, b, 1.0f);
    CHECK(end.switch_ctrl.track_on.r == 255 && end.switch_ctrl.track_on.g == 0);
    CHECK(end.tabs.underline.g == 255 && end.tabs.underline.r == 0);
    CHECK(end.palette.accent.b == 255 && end.palette.accent.r == 0);
    const theme start = theme_lerp(a, b, 0.0f);
    CHECK(start.switch_ctrl.track_on.r == a.switch_ctrl.track_on.r);
}

namespace
{
// Records whether any submitted vertex carries `want` (a style probe).
struct color_probe_device : null_device
{
    color want{};
    bool seen = false;
    void draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        for (i32 i = 0; i < vc; ++i)
            if (v[i].c.r == want.r && v[i].c.g == want.g && v[i].c.b == want.b &&
                v[i].c.a == want.a)
                seen = true;
        null_device::draw(tex, v, vc, idx, ic);
    }
};
} // namespace

PUI_TEST(test_component_style_pointer)
{
    // `props.style` is a plain style struct: copy the theme's, edit, pass the pointer
    color_probe_device dev;
    dev.want = {1, 2, 3, 255};
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    bool on = true;
    auto frame = [&](f64 t, const switch_style *style)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)comp::switch_toggle(u, rect::make(10, 10, 160, 24), "x", on,
                                      {.id = "sw"_id, .style = style});
        }
        end_frame(c);
    };

    frame(0.0, nullptr);
    CHECK(!dev.seen); // the theme slot does not use the probe color

    switch_style mine = c->active_theme.switch_ctrl;
    mine.track_on = dev.want;
    mine.animate = false;
    frame(0.016, &mine);
    CHECK(dev.seen); // the pointer's style drew the track

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

PUI_TEST(test_section)
{
    // comp::section: card + title row + (caption row) + divider, body below
    tf_env env;
    const rect area = rect::make(0, 0, 300, 200);
    const f32 pad = default_dark().card.padding; // 12
    rect body{};

    env.frame(0.0, [&](ui &u) { body = comp::section(u, area, "Title", "caption"); });
    CHECK(body.x == pad && body.w == 300.0f - pad * 2.0f);
    CHECK(body.y == pad + 38.0f + 10.0f); // title 20 + caption 16 + 2, then the 10 body gap
    CHECK(body.h == 200.0f - pad * 2.0f - 38.0f - 10.0f);

    env.frame(0.016, [&](ui &u) { body = comp::section(u, area, "Title"); });
    CHECK(body.y == pad + 24.0f + 10.0f); // title 20 + 4, then the body gap

    section_style mine = env.c->active_theme.section;
    mine.body_gap = 20.0f;
    env.frame(0.032,
              [&](ui &u) { body = comp::section(u, area, "Title", "caption", {.style = &mine}); });
    CHECK(body.y == pad + 38.0f + 20.0f);

    // slots interpolate with the theme
    theme a = default_dark(), b = a;
    b.section.body_gap = a.section.body_gap + 10.0f;
    const theme mid = theme_lerp(a, b, 0.5f);
    CHECK(mid.section.body_gap > a.section.body_gap && mid.section.body_gap < b.section.body_gap);
}

PUI_TEST(test_segmented_hover_and_outer_radii)
{
    // Every segment shows a hover highlight (the selected one too), and the control
    // can take outer radii: the first segment's left corners and the last one's right
    // corners follow them (so it fits a rounded card), corners between segments and
    // the default stay small.
    vertex_log_device dev;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    segmented_style st = c->active_theme.segmented;
    st.bg = {0, 0, 0, 0};
    st.selected = {10, 20, 30, 255};
    st.hover = {200, 9, 8, 255};
    st.pad = 2.0f;
    st.radius = 6.0f;
    static const char *const items[] = {"All", "Active", "Done"};
    const rect area = rect::make(10, 10, 180, 40);
    const rect inner = area.pad(st.pad); // the segments: 12..188 x 12..48

    i32 selected = 0;
    f64 now = 0.0;
    auto run = [&](i32 frames, const opt<corner_radii> &radii)
    {
        for (i32 i = 0; i < frames; ++i)
        {
            dev.log.clear();
            now += 0.016;
            begin_frame(c, now, 0.016, rect::make(0, 0, 300, 200));
            {
                ui u(c);
                (void)comp::segmented(u, area, items, selected,
                                      {.id = "seg"_id, .radii = radii, .style = &st});
            }
            end_frame(c);
        }
        return dev.log;
    };
    auto hover_seen = [&](const std::vector<vertex> &log)
    {
        for (const vertex &v : log)
            if (v.c.r == 200 && v.c.g == 9 && v.c.b == 8 && v.c.a >= 200) return true;
        return false;
    };

    // no pointer: no highlight
    CHECK(!hover_seen(run(3, {})));
    // over an UNSELECTED segment (the middle one): the highlight eases in
    mouse_move(c, 100.0f, 30.0f);
    CHECK(hover_seen(run(25, {})));
    // over the SELECTED segment too
    mouse_move(c, 30.0f, 30.0f);
    CHECK(hover_seen(run(25, {})));
    // pointer away again: it fades out
    mouse_move(c, 280.0f, 190.0f);
    CHECK(!hover_seen(run(40, {})));

    // default: the first segment's corners are small (radius 6 - pad 2 = 4): the fill
    // does not reach the exact corner
    std::vector<vertex> plain = run(20, {});
    CHECK(!vertex_near(plain, inner.x, inner.y, 0.5f, 255));

    // outer radii {tl 0, tr 0, br 18, bl 18}: the selected FIRST segment is square at
    // its top-left and rounded at its bottom-left (18 - pad 2 = 16)
    const opt<corner_radii> bottom = some(corner_radii::bottom(18.0f));
    std::vector<vertex> first = run(20, bottom);
    CHECK(vertex_near(first, inner.x, inner.y, 0.01f, 255));
    CHECK(!vertex_near(first, inner.x, inner.bottom(), 3.0f, 255));

    // and the LAST segment, when selected, follows the right-hand radii
    selected = 2;
    std::vector<vertex> last = run(20, bottom);
    CHECK(vertex_near(last, inner.right(), inner.y, 0.01f, 255));
    CHECK(!vertex_near(last, inner.right(), inner.bottom(), 3.0f, 255));

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

// The drawer's and the palette's scrims span the whole host, panel included, but
// only a click outside the panel closes them; a click on the panel's own empty
// area does not.
PUI_TEST(test_scrim_ignores_clicks_inside_the_panel)
{
    {
        tf_env env;
        context *c = env.c;
        bool open = true;
        const rect host = rect::make(0, 0, 300, 200);
        auto frame = [&](f64 t)
        {
            env.frame(t, [&](ui &u)
                      { comp::drawer_scope dr(u, host, open, {.id = "drw"_id, .width = 200.0f}); });
        };
        frame(0.0);
        mouse_move(c, 100.0f, 100.0f); // inside the drawer, on no widget
        mouse_button(c, true);
        frame(0.016);
        mouse_button(c, false);
        frame(0.032);
        CHECK(open);
    }
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
        static const comp::palette_command cmds[] = {{"Open file", ""}, {"Save file", ""}};
        bool open = true;
        comp::palette_state st;
        const rect screen = rect::make(0, 0, 600, 400);
        auto frame = [&](f64 t)
        {
            begin_frame(c, t, 0.016, screen);
            {
                ui u(c);
                (void)comp::command_palette(u, screen, open, st,
                                            std::span<const comp::palette_command>(cmds),
                                            {.id = "pal"_id});
            }
            end_frame(c);
        };
        frame(0.0);
        frame(0.016);
        mouse_move(c, 300.0f, screen.h * 0.18f + 2.0f); // the panel's top padding
        mouse_button(c, true);
        frame(0.032);
        mouse_button(c, false);
        frame(0.048);
        CHECK(open);

        mouse_move(c, 5.0f, 5.0f); // outside: the scrim proper
        mouse_button(c, true);
        frame(0.064);
        mouse_button(c, false);
        frame(0.080);
        CHECK(!open);
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

// Hover moves the palette highlight only while the pointer moves, so with the
// pointer resting on a row the Up/Down keys keep the highlight they set.
PUI_TEST(test_palette_keys_beat_a_resting_pointer)
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
    static char names[10][16];
    static comp::palette_command cmds[10];
    for (i32 i = 0; i < 10; ++i)
    {
        std::snprintf(names[i], sizeof(names[i]), "Command %02d", i);
        cmds[i] = {names[i], ""};
    }
    bool open = true;
    comp::palette_state st;
    const rect screen = rect::make(0, 0, 600, 600);
    auto frame = [&](f64 t)
    {
        begin_frame(c, t, 0.016, screen);
        {
            ui u(c);
            (void)comp::command_palette(u, screen, open, st,
                                        std::span<const comp::palette_command>(cmds),
                                        {.id = "pal"_id});
        }
        end_frame(c);
    };
    frame(0.0);
    frame(0.016);
    const theme &th = c->active_theme;
    const f32 items_y = screen.h * 0.18f + th.padding * 0.75f + th.control_h + 6.0f;
    const f32 row_h = th.palette.item_h;
    mouse_move(c, 300.0f, items_y + row_h * 2.5f); // rests on the third row
    frame(0.032);
    CHECK(st.active == 2); // the pointer moved: hover highlights
    frame(0.048);
    key_event(c, key::DOWN, true);
    frame(0.064);
    key_event(c, key::DOWN, false);
    frame(0.080);
    frame(0.096);
    CHECK(st.active == 3); // the key won and the resting pointer left it alone
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

// A component that is already open/on at first sight (or again after the 5 s
// collection of its animation keys) starts at its target instead of animating in.
PUI_TEST(test_components_start_at_their_target)
{
    tf_env env;
    bool open = true;
    f32 content_h = -1.0f;
    env.frame(0.0,
              [&](ui &u)
              {
                  comp::accordion_scope acc(u, rect::make(10, 10, 200, 100), "Open", open,
                                            {.id = "acc"_id, .content_h = 60.0f});
                  content_h = acc.content().h;
              });
    CHECK(content_h == 60.0f); // fully open on the very first frame

    bool on = true;
    f32 knob = -1.0f;
    env.frame(0.016,
              [&](ui &u)
              {
                  (void)comp::switch_toggle(u, rect::make(10, 150, 120, 24), "Wi-Fi", on,
                                            {.id = "sw"_id});
                  // the knob's own key, read back at its (unchanged) target
                  knob = u.animate(id_child("sw"_id, "knob"_id), 1.0f, tween{});
              });
    CHECK(knob == 1.0f); // already "on": the knob never slid in from the off position
}

// A glass menu (translucent menu.bg + menu.blur) blurs what is behind it before
// its tint goes on; the stock opaque menu makes no blur targets at all.
PUI_TEST(test_glass_menu)
{
    static const char *items[2] = {"Open", "Close"};
    const rect anchor = rect::make(40, 40, 120, 60);
    auto open_menu = [&](f32 blur, color bg, bool &tint_seen, i32 &targets)
    {
        color_probe_device dev;
        dev.want = bg;
        dev.my_surface.w = 400;
        dev.my_surface.h = 400;
        context *c = create_context(&dev, dev.create_surface());
        set_violation_handler(c, capture_violation, nullptr);
        g_violation_events = 0;
        theme t = default_dark();
        t.menu.bg = bg;
        t.menu.blur = blur;
        set_theme(c, t);
        begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
        mouse_move(c, 80, 60);
        mouse_button(c, pointer_button::RIGHT, true);
        {
            ui u(c);
            (void)u.context_menu("cm"_id, anchor, items, 2);
        }
        end_frame(c); // the deferred menu surface paints here
        tint_seen = dev.seen;
        targets = dev.targets;
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    };
    bool tint = false;
    i32 targets = -1;
    open_menu(18.0f, {1, 2, 3, 200}, tint, targets);
    CHECK(tint);         // the translucent tint is drawn...
    CHECK(targets >= 3); // ...over a blurred backdrop
    open_menu(0.0f, {1, 2, 3, 255}, tint, targets);
    CHECK(tint);
    CHECK(targets == 0); // an opaque menu needs no blur
}

// ---- r107: calendar math, HSV, the pickers ------------------------------------
PUI_TEST(test_calendar_math)
{
    CHECK(days_in_month(2024, 2) == 29 && days_in_month(2023, 2) == 28);
    CHECK(days_in_month(1900, 2) == 28 && days_in_month(2000, 2) == 29);
    CHECK(days_in_month(2026, 4) == 30 && days_in_month(2026, 13) == 0);
    CHECK(weekday({2026, 10, 15}) == 3); // a Thursday
    CHECK(weekday({2000, 1, 1}) == 5);   // a Saturday
    CHECK((add_days({2026, 12, 31}, 1) == date{2027, 1, 1}));
    CHECK((add_days({2024, 3, 1}, -1) == date{2024, 2, 29}));
    CHECK((add_days({2026, 1, 15}, 400) == date{2027, 2, 19}));
    CHECK((add_days({2027, 2, 19}, -400) == date{2026, 1, 15}));
    CHECK((add_months({2026, 1, 31}, 1) == date{2026, 2, 28}));
    CHECK((add_months({2026, 1, 15}, -13) == date{2024, 12, 15}));
}

PUI_TEST(test_hsv_round_trip)
{
    const color samples[] = {{255, 0, 0, 255},     {0, 128, 255, 255}, {12, 200, 90, 255},
                             {128, 128, 128, 255}, {0, 0, 0, 255},     {255, 255, 255, 40}};
    for (const color c : samples)
    {
        const color back = from_hsv(to_hsv(c), c.a);
        CHECK(std::abs(back.r - c.r) <= 1 && std::abs(back.g - c.g) <= 1 &&
              std::abs(back.b - c.b) <= 1 && back.a == c.a);
    }
    CHECK(std::fabs(to_hsv({0, 255, 0, 255}).h - 120.0f) < 0.5f);
}

PUI_TEST(test_date_picker)
{
    tf_env env(400, 400);
    date d{2026, 10, 15};
    comp::edit_result r{};
    const auto draw = [&](ui &u)
    { r = comp::date_picker(u, rect::make(20, 20, 160, 28), d, {.id = "when"_id}); };
    tf_click(env.c, 60, 30, 0.0, draw); // open
    CHECK(!r.changed);
    tf_frame(env.c, 0.05, draw);
    CHECK(popup_present(env.c, id_child("when"_id, "grid"_id), true));
    tf_key(env.c, key::RIGHT, false, false, 0.10, draw); // one day on
    CHECK(r.changed && (d == date{2026, 10, 16}));
    tf_key(env.c, key::PAGE_DOWN, false, false, 0.15, draw); // a month on
    CHECK((d == date{2026, 11, 16}));
    tf_key(env.c, key::ENTER, false, false, 0.20, draw); // closes
    tf_frame(env.c, 0.25, draw);
    tf_frame(env.c, 0.30, draw);
    CHECK(!popup_present(env.c, id_child("when"_id, "grid"_id), true));
    // Reopen and click a day: November 2026, Monday-first; the 1st is a
    // Sunday, so it sits in the first row's last column.
    tf_click(env.c, 60, 30, 0.40, draw);
    tf_frame(env.c, 0.45, draw);
    const f32 cell = 28.0f, pad = default_dark().spacing;
    const f32 gx = 20.0f + pad,
              gy = 20.0f + 28.0f + 4.0f + pad + cell * 2.0f; // below title + names
    tf_click(env.c, gx + cell * 6.5f, gy + cell * 0.5f, 0.50, draw);
    CHECK((d == date{2026, 11, 1}));
}

PUI_TEST(test_color_picker)
{
    tf_env env; // tf_frame draws a 300x200 window
    color c{255, 0, 0, 255};
    comp::edit_result r{};
    // square 10..142, hue bar, alpha bar, then the info column (x ~200..290)
    const rect area = rect::make(10, 10, 280, 150);
    const auto draw = [&](ui &u) { r = comp::color_picker(u, area, c, {.id = "pick"_id}); };
    tf_frame(env.c, 0.0, draw);
    // Drag to the bottom of the square: value ~0 (black), the hue stays red.
    mouse_move(env.c, 10 + 60, 10 + 131); // the square is 132 px: its last row
    mouse_button(env.c, true);
    tf_frame(env.c, 0.05, draw);
    mouse_button(env.c, false);
    tf_frame(env.c, 0.10, draw);
    CHECK(c.r < 5 && c.g < 5 && c.b < 5); // the bottom row: value ~0 (black)
    // Drag back up: red again (the remembered hue), not grey.
    mouse_move(env.c, 10 + 130, 12);
    mouse_button(env.c, true);
    tf_frame(env.c, 0.15, draw);
    mouse_button(env.c, false);
    tf_frame(env.c, 0.20, draw);
    CHECK(c.r > 200 && c.g < 60 && c.b < 60);

    // Eyedropper: a device that can read pixels answers with its pixel.
    env.nd.readable = true;
    env.nd.pixel = color{10, 200, 30, 255};
    tf_frame(env.c, 0.25, draw); // the button appears now that the device can read
    // the info column: right of the square and the two bars
    bool armed = false;
    for (f32 y = 60; y < 150 && !armed; y += 6)
    {
        tf_click(env.c, 245, y, 0.30 + y * 0.001, draw);
        armed = popup_present(env.c, id_child("pick"_id, "pickmode"_id), false);
    }
    CHECK(armed);
    tf_click(env.c, 100, 185, 1.0, draw); // anywhere in the window: sampled at end_frame
    tf_frame(env.c, 1.1, draw);           // delivered
    CHECK(c.g > 150 && c.r < 40 && c.b < 60);
    CHECK(!popup_present(env.c, id_child("pick"_id, "pickmode"_id), false)); // disarmed
}

// r107: table columns - hidden, reordered by dragging a header, toggled from
// the header menu, scrolled sideways with a sticky first column.
namespace
{
struct table_fixture
{
    tf_env env;
    static constexpr i32 COLS = 4;
    const char *headers[COLS] = {"Name", "Size", "Kind", "Date"};
    comp::table_column cols[COLS] = {
        {.width = 80.0f}, {.width = 80.0f}, {.width = 80.0f}, {.width = 80.0f}};
    i32 order[COLS] = {0, 1, 2, 3};
    comp::table_props props{};
    std::vector<std::pair<i32, f32>> cells; // (column, x) of row 0, draw order
    comp::table_result res{};
    table_fixture()
    {
        props.id = "tbl"_id;
        props.columns = cols;
        props.order = order;
    }
    void frame(f64 t)
    {
        cells.clear();
        env.frame(t,
                  [&](ui &u)
                  {
                      res = comp::table(
                          u, rect::make(0, 0, 300, 160), headers, 3,
                          [&](ui &, rect r, i32 row, i32 col)
                          {
                              if (row == 0) cells.push_back({col, r.x});
                          },
                          props);
                  });
    }
};
} // namespace

PUI_TEST(test_table_hidden_and_order)
{
    table_fixture f;
    f.cols[1].hidden = true;
    f.order[0] = 2; // Kind, Name, Size(hidden), Date
    f.order[1] = 0;
    f.order[2] = 1;
    f.order[3] = 3;
    f.frame(0.0);
    f.frame(0.016);
    CHECK(f.cells.size() == 3);
    CHECK(f.cells[0].first == 2 && f.cells[1].first == 0 && f.cells[2].first == 3);
    CHECK(f.cells[0].second < f.cells[1].second);
}

PUI_TEST(test_table_header_drag_reorders)
{
    table_fixture f;
    f.frame(0.0);
    // Drag "Name" (x 0..80) to between "Kind" and "Date" (Date starts at 240).
    mouse_move(f.env.c, 30, 10);
    mouse_button(f.env.c, true);
    f.frame(0.016);
    mouse_move(f.env.c, 230, 10);
    f.frame(0.032);
    mouse_button(f.env.c, false);
    f.frame(0.048);
    CHECK(f.res.columns_changed);
    CHECK(f.order[0] == 1 && f.order[1] == 2 && f.order[2] == 0 && f.order[3] == 3);
    CHECK(!f.res.sort_changed); // a drag is not a sort click
}

PUI_TEST(test_table_horizontal_sticky)
{
    table_fixture f;
    f.props.horizontal = true;
    f.props.sticky_first = true;
    f.cols[1].width = 150.0f; // 80 + 150 + 80 + 80 = 390 > 300
    f.frame(0.0);
    f.frame(0.016);
    const f32 kind_x = f.cells[2].second;
    // Shift+wheel over the table scrolls sideways.
    mouse_move(f.env.c, 150, 80);
    mods_event(f.env.c, true, false);
    mouse_wheel(f.env.c, 0.0f, -1.0f);
    f.frame(0.032);
    mods_event(f.env.c, false, false);
    f.frame(0.048);
    CHECK(f.cells[0].first == 0 && f.cells[0].second == 0.0f + 8.0f); // the sticky column
    bool moved = false;
    for (const auto &cx : f.cells)
        if (cx.first == 2) moved = cx.second < kind_x;
    CHECK(moved);
}

PUI_TEST(test_table_column_menu)
{
    table_fixture f;
    f.frame(0.0);
    // Right-press on the header opens the menu at the pointer.
    mouse_move(f.env.c, 100, 10);
    mouse_button(f.env.c, pointer_button::RIGHT, true);
    f.frame(0.016);
    mouse_button(f.env.c, pointer_button::RIGHT, false);
    f.frame(0.032);
    f.frame(0.048);
    // Item 1 ("Size"): 6 px pad, 26 px rows, 2 px gaps. (No font here: the menu
    // is only as wide as its padding, x 100..128.)
    const f32 iy = 10.0f + 6.0f + 26.0f + 2.0f + 13.0f;
    tf_click(f.env.c, 110, iy, 0.064,
             [&](ui &u)
             {
                 f.res = comp::table(
                     u, rect::make(0, 0, 300, 160), f.headers, 3, [](ui &, rect, i32, i32) {},
                     f.props);
             });
    f.frame(0.1); // the pick lands (the rows of this frame were laid out before it)
    CHECK(f.cols[1].hidden && f.res.columns_changed);
    f.frame(0.116);
    CHECK(f.cells.size() == 3);
}
