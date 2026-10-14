#include "test_util.h"

PUI_TEST(test_disabled_scope_and_read_only)
{
    tf_env env;
    CHECK(!tf_needs_font(env.c));
    std::string value = "original";
    bool flag = false;
    f32 number = 0.5f;
    mouse_move(env.c, 20, 20);
    mouse_button(env.c, true);
    // Seed a window before requesting focus; requests belong to that window.
    env.frame(-0.1, [](ui &) {});
    first_window(env.c)->focus_request = "field"_id;
    text_input_event(env.c, "bad");
    env.frame(0,
              [&](ui &u)
              {
                  auto disabled = u.disabled();
                  auto nested = u.disabled(false);
                  CHECK(!u.button({0, 0, 80, 30}, "Save", "save"_id));
                  CHECK(!u.checkbox({0, 40, 80, 30}, "Flag", flag, "flag"_id));
                  CHECK(!u.slider_float({0, 80, 80, 30}, "", number, 0, 1, "number"_id));
                  CHECK(!u.text_field({0, 120, 80, 30}, value, "field"_id));
                  CHECK(env.c->focus == 0);
              });
    CHECK(!flag && number == 0.5f && value == "original");
    CHECK(env.c->disabled_depth == 0);
    mouse_button(env.c, false);
    first_window(env.c)->focus_request = "field"_id;
    env.frame(0.1,
              [&](ui &u)
              {
                  CHECK(!u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true}));
                  CHECK(env.c->focus == "field"_id);
              });
    text_input_event(env.c, "bad");
    key_event(env.c, key::BACKSPACE, true);
    env.frame(0.2, [&](ui &u)
              { CHECK(!u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true})); });
    CHECK(value == "original");
    value = "model";
    env.frame(0.3, [&](ui &u)
              { CHECK(!u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true})); });
    CHECK(value == "model");
    test_clipboard clip;
    set_clipboard(env.c, &clip);
    mods_event(env.c, false, true);
    key_event(env.c, key::A, true);
    env.frame(0.4, [&](ui &u)
              { u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true}); });
    key_event(env.c, key::A, false);
    key_event(env.c, key::C, true);
    env.frame(0.5, [&](ui &u)
              { CHECK(!u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true})); });
    CHECK(clip.data == "model");
    clip.data = "replacement";
    key_event(env.c, key::C, false);
    key_event(env.c, key::V, true);
    key_event(env.c, key::X, true);
    env.frame(0.6, [&](ui &u)
              { CHECK(!u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true})); });
    CHECK(value == "model" && clip.data == "replacement");
    env.frame(0.7,
              [&](ui &u)
              {
                  env.c->text_drag.active = true;
                  env.c->text_drag.source_id = "field"_id;
                  env.c->text_drag.text = "model";
                  u.text_field({0, 120, 160, 30}, value, "field"_id, {.read_only = true});
                  CHECK(!env.c->text_drag.active);
              });
    set_clipboard(env.c, nullptr);
}

PUI_TEST(test_double_and_vector_fields)
{
    tf_env env;
    CHECK(!tf_needs_font(env.c));
    f64 value = 1.23456789012345;
    f32 xyz[] = {1, 2, 3};
    auto fields = [&](ui &u)
    {
        u.double_field({0, 0, 300, 30}, value, "double"_id);
        u.vector_field({0, 40, 300, 30}, xyz, "xyz"_id);
    };
    env.frame(0, fields);
    first_window(env.c)->focus_request = "double"_id;
    env.frame(0.1, fields);
    CHECK(value == 1.23456789012345);
    mods_event(env.c, false, true);
    key_event(env.c, key::A, true);
    text_input_event(env.c, "2.34567890123456");
    env.frame(0.2, fields);
    CHECK(value == 2.34567890123456);
    key_event(env.c, key::A, false);
    key_event(env.c, key::A, true);
    text_input_event(env.c, "nan");
    env.frame(0.3, fields);
    CHECK(value == 2.34567890123456);
    first_window(env.c)->focus_request = id_child("xyz"_id, 2);
    env.frame(0.4, fields);
    key_event(env.c, key::A, false);
    key_event(env.c, key::A, true);
    text_input_event(env.c, "4.5");
    env.frame(0.5, fields);
    CHECK(xyz[0] == 1 && xyz[1] == 4.5f && xyz[2] == 3);
    value = 7.0;
    text_input_event(env.c, "bad");
    env.frame(
        0.6, [&](ui &u)
        { CHECK(!u.double_field({0, 0, 300, 30}, value, "double"_id, {.read_only = true})); });
    CHECK(value == 7.0);
}

PUI_TEST(test_shortcut_modifiers_and_consumption)
{
    tf_env env;
    mods_event(env.c, false, true, true, false);
    key_event(env.c, key::S, true);
    env.frame(0,
              [&](ui &u)
              {
                  CHECK(!u.shortcut({key::S, MOD_CTRL}));
                  {
                      auto disabled = u.disabled();
                      CHECK(!u.shortcut({key::S, MOD_CTRL | MOD_ALT}));
                  }
                  CHECK(!u.shortcut({key::S, MOD_CTRL | MOD_ALT}, {.focus = "other"_id}));
                  CHECK(u.shortcut({key::S, MOD_CTRL | MOD_ALT}));
                  CHECK(!u.shortcut({key::S, MOD_CTRL | MOD_ALT}));
              });
}

PUI_TEST(test_disabled_deferred_menus)
{
    tf_env env;
    CHECK(!tf_needs_font(env.c));
    const char *items[] = {"First", "Second"};
    i32 selected = 0;
    env.frame(0,
              [&](ui &u)
              {
                  const uiid dropdown = id_child("combo"_id, "dropdown"_id);
                  env.c->defer_result_id = dropdown;
                  env.c->defer_result_pick = 1;
                  env.c->defer_menu_id = dropdown;
                  auto disabled = u.disabled();
                  CHECK(!u.combo({0, 0, 100, 30}, "", items, 2, selected, "combo"_id));
                  CHECK(selected == 0 && env.c->defer_result_id == 0 && env.c->defer_menu_id == 0);
                  env.c->defer_result_id = id_child("menu"_id, "menu"_id);
                  env.c->defer_menu_id = env.c->defer_result_id;
                  CHECK(u.context_menu("menu"_id, {0, 0, 100, 30}, items, 2) == -1);
                  CHECK(env.c->defer_result_id == 0 && env.c->defer_menu_id == 0);
              });
}

PUI_TEST(test_drag_value_preserves_idle_model)
{
    tf_env env;
    f64 value = 200;
    env.frame(0,
              [&](ui &u)
              {
                  CHECK(!comp::drag_value(u, {0, 0, 100, 30}, "", value,
                                          {.id = "value"_id, .min = 0, .max = 100})
                             .changed);
              });
    CHECK(value == 200);
}

PUI_TEST(test_table_stable_identity_and_sort)
{
    tf_env env;
    CHECK(!tf_needs_font(env.c));
    const char *headers[] = {"Name", "Value"};
    uiid records[] = {101, 202};
    comp::table_column columns[] = {{100}, {100}};
    comp::table_state state;
    auto key_for = [&](i32 i) { return records[i]; };
    auto frame = [&](f64 t)
    {
        env.frame(
            t,
            [&](ui &u)
            {
                (void)comp::table(
                    u, {0, 0, 200, 150}, headers, 2, {},
                    {.id = "table"_id, .row_key = key_for, .columns = columns, .state = &state});
            });
    };
    frame(0);
    mouse_move(env.c, 20, 42);
    mouse_button(env.c, true);
    frame(0.1);
    mouse_button(env.c, false);
    frame(0.2);
    CHECK(state.selected == 101);
    const uiid focus = env.c->focus;
    std::swap(records[0], records[1]);
    frame(0.3);
    CHECK(state.selected == 101 && env.c->focus == focus);
    mouse_move(env.c, 20, 12);
    mouse_button(env.c, true);
    frame(0.4);
    mouse_button(env.c, false);
    frame(0.5);
    CHECK(state.sort_column == 0 && !state.descending);
    mouse_button(env.c, true);
    frame(0.6);
    mouse_button(env.c, false);
    frame(0.7);
    CHECK(state.descending);
    mouse_move(env.c, 100, 12);
    mouse_button(env.c, true);
    frame(0.8);
    mouse_move(env.c, 140, 12);
    frame(0.9);
    mouse_button(env.c, false);
    frame(1.0);
    CHECK(columns[0].width == 140);
}

PUI_TEST(test_editor_components_keyboard_and_precision)
{
    tf_env env;
    CHECK(!tf_needs_font(env.c));
    i64 integer = 9007199254740993LL;
    f64 number = 0.0;
    bool open = false;
    key_event(env.c, key::RIGHT, true);
    env.frame(0,
              [&](ui &u)
              {
                  env.c->focus = "tree"_id;
                  auto result =
                      comp::tree_node(u, {0, 0, 200, 30}, "Folder", open, {.id = "tree"_id});
                  CHECK(result.toggled && open);
                  CHECK(!u.int_field({0, 40, 200, 30}, integer, "integer"_id));
              });
    CHECK(integer == 9007199254740993LL);
    key_event(env.c, key::RIGHT, false);
    first_window(env.c)->focus_request = "integer"_id;
    env.frame(0.1, [&](ui &u) { (void)u.int_field({0, 40, 200, 30}, integer, "integer"_id); });
    mods_event(env.c, false, true);
    key_event(env.c, key::A, true);
    text_input_event(env.c, "9223372036854775807");
    env.frame(0.2, [&](ui &u) { CHECK(u.int_field({0, 40, 200, 30}, integer, "integer"_id)); });
    CHECK(integer == std::numeric_limits<i64>::max());
    key_event(env.c, key::A, false);
    key_event(env.c, key::A, true);
    text_input_event(env.c, "9223372036854775808");
    env.frame(0.3, [&](ui &u) { CHECK(!u.int_field({0, 40, 200, 30}, integer, "integer"_id)); });
    CHECK(integer == std::numeric_limits<i64>::max());
    mods_event(env.c, false, false);
    key_event(env.c, key::A, false);
    first_window(env.c)->focus_request = "drag"_id;
    key_event(env.c, key::RIGHT, true);
    env.frame(0.4,
              [&](ui &u)
              {
                  CHECK(comp::drag_value(u, {0, 80, 200, 30}, "Value", number,
                                         {.id = "drag"_id, .speed = 0.25}));
              });
    CHECK(number == 0.25);
    color rgba{10, 20, 30, 40};
    mouse_move(env.c, 20, 15);
    mouse_button(env.c, true);
    env.frame(0.5, [&](ui &u)
              { CHECK(comp::color_edit(u, {0, 0, 200, 30}, rgba, {.id = "color"_id})); });
    CHECK(rgba.r != 10 && rgba.a == 40);
}

PUI_TEST(test_dpi_geometry_clip_and_pointer)
{
    struct device : vertex_log_device
    {
        rect clipped{};
        void set_clip(const rect *r) override
        {
            if (r) clipped = *r;
        }
    } nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    window *w = add_window(c, nullptr, nullptr, {0, 0, 300, 200});
    set_window_scale(*w, 2.0f);
    CHECK(w->area.w == 150 && w->area.h == 100);
    mouse_move(*w, 40, 40);
    begin_frame(c, *w, 0, 0.016);
    {
        ui u(c);
        auto region = u.region({10, 10, 40, 30}, "clip"_id);
        CHECK(u.interact("hit"_id, {10, 10, 40, 30}).hovered);
        u.draw_rect({10, 10, 40, 30}, {255, 255, 255, 255});
    }
    end_frame(c);
    CHECK(nd.clipped.x == 20 && nd.clipped.w == 80);
    CHECK(vertex_near(nd.log, 20, 20, 0.01f, 255));
    CHECK(vertex_near(nd.log, 100, 80, 0.01f, 255));
    set_window_scale(*w, 1.5f, {.input_scale = 1.0f});
    CHECK(w->area.w == 300);
    set_window_scale(*w, 2.0f);
    begin_frame(c, 0.1, 0.016, {0, 0, 150, 100});
    CHECK(w->client.w == 300 && w->area.w == 150);
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_font_fallback_and_dpi_measurement)
{
    tf_env env;
    const font_handle bold = load_font(env.c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    const font_handle regular = load_font(env.c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    CHECK(bold != FONT_INVALID && regular != FONT_INVALID);
    theme t = default_dark();
    t.font = bold;
    set_theme(env.c, t);
    ui u(env.c);
    u32 missing = 0;
    for (u32 cp = 32; cp < 0x20000; ++cp)
    {
        t.font = bold;
        set_theme(env.c, t);
        const bool has_bold = u.has_glyph(cp);
        t.font = regular;
        set_theme(env.c, t);
        if (!has_bold && u.has_glyph(cp))
        {
            missing = cp;
            break;
        }
    }
    CHECK(missing != 0);
    t.font = bold;
    set_theme(env.c, t);
    CHECK(!u.has_glyph(missing));
    CHECK(set_font_fallbacks(env.c, std::span<const font_handle>(&regular, 1)));
    CHECK(u.has_glyph(missing));
    const f32 width = u.text_width("AV readable text");
    env.c->render_scale = 2.0f;
    CHECK(std::fabs(u.text_width("AV readable text") - width) < 0.001f);
    CHECK(set_font_fallbacks(env.c, {}));
    CHECK(!u.has_glyph(missing));
}
