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
